/**
 * @file comms_service_init.cpp
 * @brief Implements Services::initCommsSubsystem() — L3 drives init cascade
 *
 * Layer-clean: only includes L3 + harness.
 * All Platform deps injected as parameters.
 */

#include "L3_services/comms/comms_service_init.hpp"

// L4 byte channel init through harness (L3 → L4 → L5 cascade)
#include "harness/pins/byte_channel_init.hpp"

// L2 protocol init through harness (L3 → L2 → L1 cascade)
#include "harness/pins/comms_protocol_init.hpp"

// Task init through harness
#include "harness/pins/comms_task_init.hpp"

// L3 internal
#include "L3_services/dispatch/dispatch_init.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/dispatch/command_queue/command_queue.hpp"

// Debug commands (harness — CMSIS code stays in harness)
#include "harness/debug_commands.hpp"

namespace Services {

bool initCommsSubsystem(const CommsServiceDeps& deps,
                        Harness::IClock& clock,
                        Harness::IScheduler& scheduler)
{
    // 1. L3 dispatch init
    DispatchDeps dispDeps;
    dispDeps.encoder       = deps.encoder;
    dispDeps.encFilter     = deps.encFilter;
    dispDeps.motorCtrl     = deps.motorCtrl;
    dispDeps.remoteDisplay = deps.remoteDisplay;
    dispDeps.clock         = &clock;

    Harness::DispatchInterfaces dispatches;
    if (!initDispatches(dispatches, dispDeps)) {
        return false;
    }

    // 2. Debug commands (harness — CMSIS code stays in harness)
    static DebugCommandHandler debugCommands;

    // 3. L4 byte channel init (L3 → L4 → L5 cascade)
    Drivers::ByteChannelResult chResult;
    if (!Drivers::initByteChannel(chResult, Drivers::ChannelType::VCP_UART)) {
        return false;
    }

    // 4. L2 protocol init (L3 → L2 → L1 cascade, passes byte channel down)
    Protocol::CommsProtocol protocol;
    if (!Protocol::initCommsProtocol(protocol, dispatches, &debugCommands,
                                     *chResult.channel, clock)) {
        return false;
    }

    // 4. Comms task init (through harness → F_platform)
    Harness::CommsTaskResult commsResult;
    if (!Harness::initCommsTask(*protocol.transport, *protocol.parser,
                                 clock,
                                 motion.stepper.event, g_commandQueue,
                                 scheduler, commsResult)) {
        return false;
    }

    // 5. Late-bind event seq function (comms task → system dispatch)
    setDispatchEventSeqFn(commsResult.getEventSeqFn);

    return true;
}

} // namespace Services
