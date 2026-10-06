/**
 * @file main.cpp
 * @brief Virtual Nucleo: the real L2 protocol and L3 services, and the real Motor, Comms and
 *        Encoder task bodies, running on a desktop over a TCP "serial port".
 *
 * This is a composition root (rule 5: it may see every sheet). It mirrors
 * server/system_init.cpp phase by phase; only the hardware differs:
 *   L1/L5 USART2 + RX ring   -> Sim::TcpTransport (same 1024-byte ring, overflow counted)
 *   L4 powerSTEP01 over SPI  -> Sim::FakePowerStep (datasheet register model, motion physics)
 *   L4 TIM encoder + DMA     -> Sim::FakeEncoderHw (driven by the fake motor's position)
 *   L4 internal-flash config -> Sim::FakeConfigStore (RAM, or a file with --state)
 *   L4 SPI NOR flash         -> Sim::FakeNorFlash (images in RAM; device identity persisted)
 *   L3 remote LCD            -> Sim::FakeRemoteDisplay
 *   FreeRTOS tasks           -> std::thread running vMotorTask / vCommsTask / vEncoderTask
 *
 *   vnucleo [--port 5761] [--bind 127.0.0.1] [--serial VN000001] [--state DIR]
 *           [--motion-log FILE]
 *
 * Differences from the board (none touch the protocol):
 *   - g_deviceConfig is initialised here; the firmware's composition root never does
 *     (docs/review/LINK_BUDGET.md finding 1), so DEV:ID / DEV:ROLE work only on the sim.
 *   - No LCD, joystick or display task; UI:DISP:* draw into a counter.
 *   - The boot id is random per process; the serial comes from --serial.
 */

#include "fake_peripherals.hpp"
#include "fake_powerstep.hpp"
#include "host_platform.hpp"
#include "tcp_transport.hpp"

#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "harness/defs/harness_l2_l3.hpp"
#include "harness/pins/async_event.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/motor_command.hpp"
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/service_dispatcher.hpp"
#include "L3_services/comms/comms_service/comms_service.hpp"
#include "L3_services/config/device_config/device_config.hpp"
#include "L3_services/debug/debug_service.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"
#include "L3_services/dispatch/dispatch_init.hpp"
#include "L3_services/infra/flash_image_service/flash_image_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/motion/encoder_service/encoder_service.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/motion/system_service_init.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

namespace Sim {
std::string g_serial = "VN000001";
uint32_t g_bootId = 0;
}

namespace {

void usage() {
    std::printf("vnucleo [--port N] [--bind ADDR] [--serial ID] [--state DIR]\n"
                "  --port    TCP port to listen on (default 5761)\n"
                "  --bind    address (default 127.0.0.1; 0.0.0.0 to accept other hosts)\n"
                "  --serial  device serial reported by *IDN? (default VN000001)\n"
                "  --state   directory for the persistent config and identity (default: RAM)\n");
}

[[noreturn]] void fail(const char* what) {
    std::fprintf(stderr, "vnucleo: %s\n", what);
    std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 5761;
    std::string bind = "127.0.0.1";
    std::string state;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(2); }
            return argv[++i];
        };
        if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
        else if (a == "--bind") bind = next();
        else if (a == "--serial") Sim::g_serial = next().substr(0, 8);
        else if (a == "--state") state = next();
        else if (a == "--motion-log") Sim::FakePowerStep::openMotionLog(next().c_str());
        else { usage(); return a == "--help" || a == "-h" ? 0 : 2; }
    }
    std::random_device rd;
    Sim::g_bootId = (static_cast<uint32_t>(rd()) << 1) ^ static_cast<uint32_t>(rd());
    (void)Sim::epoch();

    // --- Phase 1: hardware ---------------------------------------------------------------
    static Sim::HostClock clock;
    static Sim::HostMicrosecondClock usClock;
    Services::TickTimer_Init(usClock);
    Harness::setMicrosecondClock(&usClock);

    static Sim::TcpTransport transport(bind, port);
    if (!transport.listen()) fail("cannot listen (port in use?)");

    const std::string prefix = state.empty() ? std::string() : state + "/" + Sim::g_serial;
    static Sim::FakePowerStep motor;
    static Sim::FakeEncoderHw encoderHw(motor);
    static Sim::FakeRemoteDisplay lcd;
    static Sim::FakeConfigStore configStore(prefix.empty() ? "" : prefix + "_motorcfg.bin");
    static Sim::FakeNorFlash identityFlash(64 * 1024, prefix.empty() ? "" : prefix + "_identity.bin");
    static Sim::FakeNorFlash imageFlash(2 * 1024 * 1024, "");

    // --- Phase 2: platform ---------------------------------------------------------------
    static Sim::HostLock controlModeLock, telemetryLock, uiModeLock, commandQueueLock, deviceLock;
    static Sim::HostCritical critical;
    static Sim::HostQueue<AsyncEvent, 8> eventQueue;
    static Sim::HostQueue<Services::MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH> motorQueue;

    // --- Phase 3: L3 shared state --------------------------------------------------------
    if (!Services::initSystemServices(controlModeLock, eventQueue, telemetryLock, uiModeLock, clock)) {
        fail("system services");
    }

    // --- Phase 4: subsystems (initMotorSubsystem with the fake L4) -----------------------
    motor.init();
    Services::motion.stepper.controller.bind(motor, clock);
    Services::motion.configStore.init(configStore);
    Services::g_motorService.init(Services::motion.stepper.controller,
                                  Services::motion.stepper.controller,
                                  motorQueue, clock, nullptr, Services::g_telemetryStore);
    Services::g_commandQueue.init(commandQueueLock, clock, Services::g_motorService,
                                  &Services::g_motorService);

    // initEncoderSubsystem with the fake encoder
    const auto& encCfg = Services::motion.encoder.config;
    Services::motion.encoder.processor.setFilterConfig(
        encCfg.getEncFilterType(), encCfg.getEncFilterParam(), encCfg.getEncSmaWindow(),
        0, 0, 0, 0, 0, 0, 0);
    const bool encoderOk = Services::g_encoderService.init(
        encoderHw, critical, usClock, Services::motion.encoder.processor,
        Services::motion.control.mode, encCfg.getEncMeasWindowMs(), encCfg.getEncSampleRateHz());

    // Images and device identity (the firmware does not initialise g_deviceConfig)
    Services::g_flashImageService.init(&imageFlash);
    Services::g_deviceConfig.init(identityFlash, deviceLock);

    // initCommsSubsystem: dispatchers, L2 over the TCP transport, H_L2_L3, comms service
    Services::g_debugService.init(nullptr, &Services::g_motorService, nullptr);
    Services::DispatchDeps deps;
    deps.encoder = encoderOk ? &Services::g_encoderService : nullptr;
    deps.encFilter = &Services::g_encoderService;
    deps.motorCtrl = &Services::g_motorService;
    deps.remoteDisplay = &lcd;
    deps.clock = &clock;
    const Harness::L3Dispatchers* dispatchers = Services::initDispatches(deps);
    if (dispatchers == nullptr) fail("dispatchers");

    static Protocol::ServiceDispatcher dispatcher;
    dispatcher.bind(*dispatchers);
    static Protocol::CommandParser parser(transport, dispatcher);
    const Harness::L2L3Harness& l2l3 = Harness::connectL2L3(*dispatchers, parser);
    Services::g_commsService.init(l2l3.processor, clock, Services::motion.stepper.event,
                                  Services::g_commandQueue, Services::g_telemetryStore,
                                  &UI::g_uiMode, &Services::g_motorService);
    Services::setDispatchEventSeqFn(&Services::CommsService::lastEventSeqStatic);
    Services::setDispatchTelemetryFns(&Services::CommsService::setTelemetryPeriodStatic,
                                      &Services::CommsService::getTelemetryPeriodStatic);

    // --- Phase 5: tasks ------------------------------------------------------------------
    if (!Scheduler::MotorTask_Init(Services::g_motorService, motorQueue, clock)) fail("motor task");
    if (!Scheduler::CommsTask_Init(Services::g_commsService, clock, nullptr)) fail("comms task");
    std::thread motorTask(Scheduler::vMotorTask, nullptr);
    std::thread commsTask(Scheduler::vCommsTask, nullptr);
    if (encoderOk && Scheduler::EncoderTask_Init(Services::g_encoderService, clock)) {
        std::thread(Scheduler::vEncoderTask, nullptr).detach();
    }

    std::printf("vnucleo %s listening on %s:%u (boot %08X)\n", Sim::g_serial.c_str(),
                bind.c_str(), static_cast<unsigned>(port), static_cast<unsigned>(Sim::g_bootId));
    std::fflush(stdout);
    motorTask.join();          // the task loops never return; Ctrl-C / kill ends the board
    commsTask.join();
    return 0;
}
