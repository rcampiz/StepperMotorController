/**
 * @file arch_screen.hpp
 * @brief Architecture visualization screen
 *
 * Five view modes, cycled with RIGHT (LEFT leaves the screen):
 *
 * DIAGRAM — Static architecture overview
 *   Colored boxes showing L1-L5 layer stack with F_platform (tasks)
 *   shown separately to illustrate orthogonality.
 *
 * MATRIX — Live harnesses x tasks heatmap (harness meter, every build)
 *   Rows = harnesses (H_TASKS_L3, H_L1_L2 .. H_L4_L5, X_L5_L1), columns = tasks.
 *   Cell = crossings per second by that task over that harness, intensity by rate.
 *   Until 2026-10-19 the rows were layers fed from the debug-only trace ring; the meter's
 *   byTask counters give the same picture on every build, per harness rather than per layer.
 *   CENTER: Pause/resume
 *
 * FLOW — SCPI command lifecycle (trace ring, debug builds)
 *   Shows the most recent command's path through layers
 *   with task handoff visualization.
 *
 * TASKS — Per-task layer activity (trace ring, debug builds)
 *   Shows which layers a selected RTOS task touches,
 *   including background loops with no SCPI stimulus.
 *   UP/DOWN: Cycle through tasks
 *   CENTER: Pause/resume
 *
 * HARNESS — the harness meter, live (every build)
 *   One row per metered harness signal, grouped under its harness: calls/s, bytes/s,
 *   busy % (time inside the crossing / wall time) with a bar, and the task that crossed
 *   most (M E C D T, ? = no task context, + = other tasks too). Rates are deltas between
 *   samples SAMPLE_US apart; only the cells whose text changed are repainted.
 *   CENTER: Pause/resume
 */

#ifndef ARCH_SCREEN_HPP
#define ARCH_SCREEN_HPP

#include "ui/screen.hpp"
#include "harness/trace/harness_meter.hpp"
#include <stdint.h>
#include <stddef.h>

namespace UI {

class ArchScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(Services::ICanvas& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return false; }
    void clearRedrawFlag() override { }
    bool usesChrome() const override { return true; }
    const char* title() const override { return "Architecture"; }
    const char* centerHint() const override { return "PAUSE"; }

private:
    enum class Mode : uint8_t { DIAGRAM, MATRIX, FLOW, TASKS, HARNESS };

    void renderDiagram(Services::ICanvas& lcd);
    void renderMatrix(Services::ICanvas& lcd);
    void renderFlow(Services::ICanvas& lcd);
    void renderTasks(Services::ICanvas& lcd);
    void renderHarness(Services::ICanvas& lcd);
    InputResult handleDiagramInput(JoyDirection dir);
    InputResult handleMatrixInput(JoyDirection dir);
    InputResult handleFlowInput(JoyDirection dir);
    InputResult handleTasksInput(JoyDirection dir);
    InputResult handleHarnessInput(JoyDirection dir);

    Mode m_mode = Mode::DIAGRAM;
    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    bool m_paused = false;
    size_t m_lastTotal = 0;
    uint8_t m_selectedTask = 0;  // Index into task table (0=Motor, wraps 0-4)

    // Diagram mode: live boundary traffic per layer, what is drawn (0xFFFF = nothing yet)
    uint16_t m_layerDrawn[6] = {};
    void switchMode(Mode m);

public:
    /// Meter sampling period: rates are deltas over this much time
    static constexpr uint32_t SAMPLE_US = 500000;
    static constexpr uint8_t NETS = Harness::Meter::NET_COUNT;
    static constexpr uint8_t HARNESSES = 6;        ///< H_TASKS_L3, H_L1_L2 .. H_L4_L5, X_L5_L1
    static constexpr uint8_t MATRIX_TASKS = 5;       ///< Motor, Encoder, Comms, Display, Timer

private:
    // --- harness meter sampling (MATRIX and HARNESS) ---
    struct NetRate {
        uint32_t calls;      ///< per second
        uint32_t bytes;      ///< per second
        uint16_t busyPm;     ///< busy time per wall time, permille
        uint8_t  task;       ///< task slot that crossed most (0xFF: nobody)
        bool     mixed;      ///< other tasks crossed too
    };
    struct RowDrawn {        ///< what a HARNESS row shows now (diff repaint)
        char    calls[8];
        char    bytes[8];
        char    busy[8];
        uint8_t bar;
        uint8_t task;        ///< task slot | 0x80 when mixed; 0xFE nothing, 0xFF never drawn
    };
    void sampleMeter(bool baselineOnly);
    bool meterDue() const;

    Harness::Meter::Counters m_prev[NETS] = {};
    NetRate  m_rate[NETS] = {};
    uint32_t m_taskRate[HARNESSES][MATRIX_TASKS] = {};   ///< calls/s by task, per harness
    uint32_t m_sampleUs = 0;
    bool     m_haveRates = false;
    RowDrawn m_rowDrawn[NETS] = {};
    uint32_t m_cellDrawn[HARNESSES][MATRIX_TASKS] = {};  ///< rate drawn per matrix cell
};

} // namespace UI

#endif // ARCH_SCREEN_HPP
