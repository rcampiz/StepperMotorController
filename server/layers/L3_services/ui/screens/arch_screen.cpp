/**
 * @file arch_screen.cpp
 * @brief Architecture visualization screen implementation
 *
 * Three modes: DIAGRAM (static layer overview), MATRIX (live heatmap),
 * FLOW (SCPI command lifecycle through layers).
 */

#include "ui/screens/arch_screen.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L3_services/infra/trace.hpp"
#include "F_util/interface_trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LINE_H = 12;
static constexpr uint16_t LINE_W = LCD::WIDTH - (2 * MARGIN);
static constexpr uint16_t WHITE   = 0xFFFF;
static constexpr uint16_t BG      = 0x0000;
static constexpr uint16_t CYAN    = 0x07FF;
static constexpr uint16_t GREEN   = 0x07E0;
static constexpr uint16_t MAGENTA = 0xF81F;
static constexpr uint16_t RED     = 0xF800;
static constexpr uint16_t YELLOW  = 0xFFE0;
static constexpr uint16_t DIM     = 0x4208;
static constexpr uint16_t PAUSE_COLOR = 0xFFE0;

// Layer colors: L1=cyan, L2=green, L3=white, L4=magenta, L5=red, F=yellow
static constexpr uint16_t LAYER_COLORS[] = {
    CYAN, GREEN, WHITE, MAGENTA, RED, YELLOW
};
static constexpr const char* LAYER_NAMES[] = {
    "L1 Transport", "L2 Protocol", "L3 Services",
    "L4 Drivers", "L5 Board/HAL", "F  Platform"
};
static constexpr const char* LAYER_CONTENTS[] = {
    "UART  RTT",
    "Parser  Telemetry",
    "Motion Safety Config",
    "Motor Enc LCD Flash",
    "GPIO SPI UART ADC",
    "Tasks are orthogonal"
};

// Task columns for matrix mode
static constexpr const char* TASK_COLS[] = { "Mot", "Enc", "Com", "Dsp", "Tmr" };
static constexpr uint8_t TASK_IDS[] = { 1, 2, 3, 4, 5 };
static constexpr uint8_t NUM_TASKS = 5;
static constexpr uint8_t NUM_LAYERS = 6;

// Dim a color to ~25% brightness
static uint16_t dimColor(uint16_t c) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b =  c        & 0x1F;
    return ((r / 4) << 11) | ((g / 4) << 5) | (b / 4);
}

// Map boundary byte to layer index (0-5)
static uint8_t boundaryToLayer(uint8_t boundary) {
    if (boundary & ITrace::L1_L2_TRANSPORT) return 0;  // L1
    if (boundary & ITrace::L2_L3_DISPATCH)  return 1;  // L2
    if (boundary & ITrace::L3_L4_MOTOR)     return 3;  // L4 (driver call)
    if (boundary & ITrace::L3_L4_ENCODER)   return 3;  // L4 (driver call)
    if (boundary & ITrace::L3_F_SAFETY)     return 2;  // L3 (service)
    if (boundary & ITrace::L3_CMD_SINK)     return 2;  // L3 (command queue)
    return 2;  // Default to L3 for entries with boundary=0
}

// Map taskId (1-5) to column index (0-4), -1 for unknown
static int8_t taskToCol(uint8_t taskId) {
    for (uint8_t i = 0; i < NUM_TASKS; i++) {
        if (TASK_IDS[i] == taskId) return static_cast<int8_t>(i);
    }
    return -1;
}

void ArchScreen::onActivate()
{
    m_needsRedraw = true;
    m_titleDrawn = false;
    m_mode = Mode::DIAGRAM;
    m_paused = false;
    m_lastTotal = 0;
    memset(m_cellCounts, 0, sizeof(m_cellCounts));
}

void ArchScreen::render(LCD& lcd)
{
    if (!m_titleDrawn) {
        lcd.fillScreen(BG);
        m_titleDrawn = true;
    }

    switch (m_mode) {
        case Mode::DIAGRAM: renderDiagram(lcd); break;
        case Mode::MATRIX:  renderMatrix(lcd);  break;
        case Mode::FLOW:    renderFlow(lcd);    break;
    }
    m_needsRedraw = false;
}

// =============================================================================
// DIAGRAM mode — static architecture overview
// =============================================================================

void ArchScreen::renderDiagram(LCD& lcd)
{
    if (!m_needsRedraw) return;

    uint16_t y = MARGIN;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "Firmware Architecture", CYAN, BG);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // Draw 6 layer boxes: L1-L5 + F_platform
    static constexpr uint16_t BOX_H = 36;
    static constexpr uint16_t BOX_W = LINE_W;
    static constexpr uint16_t BOX_GAP = 2;
    static constexpr uint16_t ARROW_H = 6;

    for (uint8_t i = 0; i < NUM_LAYERS; i++) {
        uint16_t color = LAYER_COLORS[i];

        // Draw separator before F_platform to show orthogonality
        if (i == 5) {
            lcd.drawHLine(MARGIN, y, BOX_W, DIM);
            y += 2;
            lcd.blitTextLine(MARGIN, y, LINE_W, 8, "  orthogonal", DIM, BG);
            y += 10;
            lcd.drawHLine(MARGIN, y, BOX_W, DIM);
            y += 2;
        }

        // Box outline
        lcd.drawRect(MARGIN, y, BOX_W, BOX_H, color);

        // Layer name (top line, colored)
        lcd.blitTextLine(MARGIN + 4, y + 4, BOX_W - 8, 8,
                         LAYER_NAMES[i], color, BG);

        // Contents (second line, dimmer)
        lcd.blitTextLine(MARGIN + 4, y + 16, BOX_W - 8, 8,
                         LAYER_CONTENTS[i], dimColor(color), BG);

        y += BOX_H;

        // Draw arrow between L1-L5 layers (not after L5 or F)
        if (i < 4) {
            uint16_t arrowX = LCD::WIDTH / 2;
            uint16_t midY = y + ARROW_H / 2;
            lcd.fillRect(arrowX, y, 1, ARROW_H, DIM);
            // Small arrowhead
            lcd.fillRect(arrowX - 2, midY, 5, 1, DIM);
            y += ARROW_H + BOX_GAP;
        } else if (i == 4) {
            y += BOX_GAP;
        }
    }

    // Footer
    lcd.blitTextLine(MARGIN, LCD::HEIGHT - 10, LINE_W, 8,
                     "L:back  R:matrix", CYAN, BG);
}

// =============================================================================
// MATRIX mode — live layers x tasks heatmap
// =============================================================================

void ArchScreen::renderMatrix(LCD& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) m_lastTotal = total;

    if (!m_needsRedraw && !newData) return;
    if (!m_needsRedraw && m_paused) return;

    // Throttle live updates
    if (!m_needsRedraw && newData && !m_paused) {
        static uint8_t frameSkip = 0;
        if (++frameSkip < 4) return;
        frameSkip = 0;
    }

    uint16_t y = MARGIN;
    char buf[40];

    // Title
    if (m_paused) {
        snprintf(buf, sizeof(buf), "PAUSED  %u entries", (unsigned)count);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, PAUSE_COLOR, BG);
    } else {
        snprintf(buf, sizeof(buf), "Layers x Tasks  %uent", (unsigned)count);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, CYAN, BG);
    }
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // Scan trace entries to build count matrix
    memset(m_cellCounts, 0, sizeof(m_cellCounts));
    Trace::Entry entry;
    for (size_t i = 0; i < count; i++) {
        if (!Trace::getEntry(i, entry)) continue;
        int8_t col = taskToCol(entry.taskId);
        if (col < 0) continue;
        uint8_t row = boundaryToLayer(entry.boundary);
        if (row < NUM_LAYERS) {
            if (m_cellCounts[row][col] < 255) {
                m_cellCounts[row][col]++;
            }
        }
    }

    // Layout constants
    static constexpr uint16_t LABEL_W = 30;   // Row label width
    static constexpr uint16_t CELL_W = 38;    // Cell width
    static constexpr uint16_t CELL_H = 28;    // Cell height
    static constexpr uint16_t HDR_H = 12;     // Column header height
    uint16_t gridX = MARGIN + LABEL_W + 2;

    // Column headers
    for (uint8_t c = 0; c < NUM_TASKS; c++) {
        uint16_t cx = gridX + c * CELL_W;
        lcd.blitTextLine(cx, y, CELL_W, HDR_H, TASK_COLS[c], WHITE, BG);
    }
    y += HDR_H;

    // Grid rows
    for (uint8_t r = 0; r < NUM_LAYERS; r++) {
        uint16_t color = LAYER_COLORS[r];

        // Row label
        const char* shortLabel;
        switch (r) {
            case 0: shortLabel = "L1"; break;
            case 1: shortLabel = "L2"; break;
            case 2: shortLabel = "L3"; break;
            case 3: shortLabel = "L4"; break;
            case 4: shortLabel = "L5"; break;
            default: shortLabel = "F"; break;
        }
        lcd.blitTextLine(MARGIN, y, LABEL_W, CELL_H, shortLabel, color, BG);

        // Cells
        for (uint8_t c = 0; c < NUM_TASKS; c++) {
            uint16_t cx = gridX + c * CELL_W;
            uint8_t cnt = m_cellCounts[r][c];

            if (cnt == 0) {
                // Empty cell: dark outline
                lcd.fillRect(cx + 1, y + 1, CELL_W - 2, CELL_H - 2, BG);
                lcd.drawRect(cx, y, CELL_W, CELL_H, DIM);
                lcd.blitTextLine(cx + 2, y + 8, CELL_W - 4, 8, ".", DIM, BG);
            } else {
                // Filled cell: brightness by count
                uint16_t fillColor;
                if (cnt >= 10) {
                    fillColor = color;                // Full brightness
                } else if (cnt >= 3) {
                    fillColor = dimColor(color);      // Medium (25%)
                    // Brighten slightly: add 50% of remaining
                    uint16_t rr = (fillColor >> 11) & 0x1F;
                    uint16_t gg = (fillColor >> 5)  & 0x3F;
                    uint16_t bb =  fillColor        & 0x1F;
                    rr = rr + (0x1F - rr) / 2;
                    gg = gg + (0x3F - gg) / 2;
                    bb = bb + (0x1F - bb) / 2;
                    fillColor = (rr << 11) | (gg << 5) | bb;
                } else {
                    fillColor = dimColor(color);      // Dim (25%)
                }

                lcd.fillRect(cx + 1, y + 1, CELL_W - 2, CELL_H - 2, fillColor);
                lcd.drawRect(cx, y, CELL_W, CELL_H, color);

                // Count label
                snprintf(buf, sizeof(buf), "%u", (unsigned)cnt);
                lcd.blitTextLine(cx + 2, y + 8, CELL_W - 4, 8, buf, BG, fillColor);
            }
        }
        y += CELL_H;
    }

    y += 4;

    // Legend
    lcd.drawHLine(MARGIN, y, LINE_W, DIM);
    y += 4;
    lcd.blitTextLine(MARGIN, y, LINE_W, 8,
                     " bright>10  dim 1-9  . none", DIM, BG);
    y += LINE_H;

    // Footer
    lcd.blitTextLine(MARGIN, LCD::HEIGHT - 10, LINE_W, 8,
                     "CTR:pause L:diag R:flow",
                     m_paused ? PAUSE_COLOR : CYAN, BG);
}

// =============================================================================
// FLOW mode — SCPI command lifecycle
// =============================================================================

void ArchScreen::renderFlow(LCD& lcd)
{
    size_t total = Trace::getTotal();
    size_t count = Trace::getCount();
    bool newData = (total != m_lastTotal);
    if (newData) m_lastTotal = total;

    if (!m_needsRedraw && !newData) return;
    if (!m_needsRedraw && m_paused) return;

    // Throttle live updates
    if (!m_needsRedraw && newData && !m_paused) {
        static uint8_t frameSkip = 0;
        if (++frameSkip < 8) return;
        frameSkip = 0;
    }

    uint16_t y = MARGIN;

    // Title
    lcd.blitTextLine(MARGIN, y, LINE_W, 8, "SCPI Flow", CYAN, BG);
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // --- Scan trace entries to gather per-layer info ---
    // Canonical SCPI flow always shows L1→L2→L3→L4→L5.
    // Trace entries for some layers (L2, L5) are rare or absent,
    // so we always show all layers and fill in live data where available.
    struct LayerInfo {
        uint8_t taskId;                  // most recent task seen (0=none)
        const char* method;              // method name from trace (nullptr=none)
        char detail[Trace::DETAIL_SIZE]; // detail string from trace
        bool active;                     // has trace entries
    };
    LayerInfo layerInfo[NUM_LAYERS] = {};
    char lastCmd[20] = "(none)";

    Trace::Entry entry;
    for (size_t ri = 0; ri < count; ri++) {
        size_t i = count - 1 - ri;
        if (!Trace::getEntry(i, entry)) continue;

        // Capture most recent command detail
        if (entry.detail[0] != '\0' && lastCmd[0] == '(') {
            strncpy(lastCmd, entry.detail, sizeof(lastCmd) - 1);
            lastCmd[sizeof(lastCmd) - 1] = '\0';
        }

        uint8_t layer = boundaryToLayer(entry.boundary);
        if (layer < NUM_LAYERS && !layerInfo[layer].active) {
            layerInfo[layer].taskId = entry.taskId;
            layerInfo[layer].method = entry.method;
            strncpy(layerInfo[layer].detail, entry.detail, Trace::DETAIL_SIZE - 1);
            layerInfo[layer].detail[Trace::DETAIL_SIZE - 1] = '\0';
            layerInfo[layer].active = true;
        }
    }

    // Canonical SCPI path: L1→L2→L3→L4→L5 (always shown)
    // F_platform shown separately as orthogonal dependency
    static constexpr uint8_t SHOW_LAYERS[] = { 0, 1, 2, 3, 4 };
    static constexpr uint8_t SHOW_COUNT = 5;

    // Show last command
    char buf[40];
    snprintf(buf, sizeof(buf), "Last: %s", lastCmd);
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG);
    y += LINE_H + 2;

    // --- Interface names at each layer boundary ---
    struct BoundaryInfo {
        const char* iface;  // Interface name (colored)
        const char* data;   // What data flows through it
    };
    static constexpr BoundaryInfo BOUNDARIES[] = {
        { "ITransport",         "bytes: read/write/print" },   // L1↔L2
        { "ICommandDispatcher", "DispatchResult: cmd args" },  // L2→L3
        { "IMotorDriver",       "run/move/goTo/setParam" },    // L3→L4
        { "SPIHardware",        "SPI xfer, GPIO set/clr" },    // L4→L5
    };

    // Draw the flow diagram
    static constexpr uint16_t FLOW_X = 20;     // Left margin for layer labels
    static constexpr uint16_t ARROW_X = 14;    // Vertical line x
    static constexpr uint16_t TASK_X = 150;    // Task label x
    // Compact layout to fit L1-L5 + F_platform in 320px
    static constexpr uint16_t LAYER_H = 16;    // Layer name + method
    static constexpr uint16_t ARROW_GAP = 20;  // Arrow + interface name + data desc
    static constexpr uint16_t STEP_H = LAYER_H + ARROW_GAP;
    static constexpr uint16_t F_SECTION_H = 30; // F_platform orthogonal section

    // Clear fixed flow area (5 layers + arrows + F_platform section)
    static constexpr uint16_t MAX_FLOW_H = (SHOW_COUNT * STEP_H) + F_SECTION_H;
    lcd.fillRect(MARGIN, y, LINE_W, MAX_FLOW_H, BG);

    uint8_t prevTaskId = 0;
    for (uint8_t s = 0; s < SHOW_COUNT; s++) {
        uint8_t layer = SHOW_LAYERS[s];
        uint16_t color = LAYER_COLORS[layer];
        bool active = layerInfo[layer].active;
        uint16_t labelColor = active ? color : dimColor(color);

        // --- Layer box ---
        // Colored sidebar
        lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                     active ? dimColor(color) : 0x2104);

        // Layer name
        const char* layerLabel;
        switch (layer) {
            case 0: layerLabel = "L1 Transport"; break;
            case 1: layerLabel = "L2 Protocol"; break;
            case 2: layerLabel = "L3 Services"; break;
            case 3: layerLabel = "L4 Drivers"; break;
            default: layerLabel = "L5 Board/HAL"; break;
        }
        lcd.blitTextLine(FLOW_X, y, 120, 8, layerLabel, labelColor, BG);

        // Method/detail line — shows what's actually happening
        // Use live trace data if available, otherwise show static description
        if (active && layerInfo[layer].method != nullptr) {
            // Live data: method(detail) or just method
            if (layerInfo[layer].detail[0] != '\0') {
                snprintf(buf, sizeof(buf), " %s(%s)",
                         layerInfo[layer].method, layerInfo[layer].detail);
            } else {
                snprintf(buf, sizeof(buf), " %s()", layerInfo[layer].method);
            }
            lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, buf, WHITE, BG);
        } else {
            // Static description for layers without trace data
            static constexpr const char* STATIC_DESC[] = {
                "UART rx/tx",         // L1
                "parse & dispatch",   // L2
                "service execute",    // L3
                "driver API call",    // L4
                "SPI/GPIO/ADC ops",   // L5
                "RTOS scheduling",    // F
            };
            const char* desc = (layer < NUM_LAYERS) ? STATIC_DESC[layer] : "";
            lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, desc, DIM, BG);
        }

        // Task label on the right side
        uint8_t taskId = layerInfo[layer].taskId;
        // For L5, inherit L4's task (L5 always runs in same task as L4 caller)
        if (layer == 4 && taskId == 0 && layerInfo[3].taskId != 0) {
            taskId = layerInfo[3].taskId;
        }
        // For L1/L2, default to Comms task if no trace data
        if ((layer == 0 || layer == 1) && taskId == 0) {
            taskId = 3;  // Comms
        }
        const char* taskName;
        switch (taskId) {
            case 1: taskName = "[Motor]"; break;
            case 2: taskName = "[Encdr]"; break;
            case 3: taskName = "[Comms]"; break;
            case 4: taskName = "[Displ]"; break;
            case 5: taskName = "[Timer]"; break;
            default: taskName = ""; break;
        }
        lcd.blitTextLine(TASK_X, y, 80, 8, taskName,
                         active ? WHITE : DIM, BG);

        // Task handoff indicator
        if (s > 0 && prevTaskId != 0 && taskId != 0 && taskId != prevTaskId) {
            snprintf(buf, sizeof(buf), "handoff");
            lcd.blitTextLine(TASK_X, y + 8, 80, 8, buf, YELLOW, BG);
        }

        y += LAYER_H;

        // --- Arrow + interface info between layers ---
        if (s < SHOW_COUNT - 1) {
            // Vertical arrow line
            lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
            // Small arrowhead
            lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

            // Interface name (colored by upper layer)
            lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                             BOUNDARIES[s].iface, LAYER_COLORS[SHOW_LAYERS[s]], BG);
            // Data description (dim)
            lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                             BOUNDARIES[s].data, DIM, BG);
            y += ARROW_GAP;
        }

        if (taskId != 0) prevTaskId = taskId;
    }

    // --- F_platform orthogonal section ---
    // Horizontal separator (dashed line to show orthogonal relationship)
    y += 2;
    for (uint16_t dx = MARGIN; dx < LINE_W; dx += 6) {
        lcd.fillRect(dx, y, 3, 1, DIM);
    }
    y += 4;

    // F_platform box — shown as sideways dependency from L5
    // Arrow from L5 pointing right into F_platform
    static constexpr uint16_t F_LABEL_X = FLOW_X + 90;
    // "L5 uses ILock from:" label
    lcd.blitTextLine(FLOW_X, y, F_LABEL_X - FLOW_X, 8,
                     "L5 uses:", dimColor(RED), BG);
    // F_platform label (yellow)
    lcd.blitTextLine(F_LABEL_X, y, LINE_W - F_LABEL_X, 8,
                     "F_platform", YELLOW, BG);
    y += 10;
    // Interface details
    lcd.blitTextLine(FLOW_X + 4, y, LINE_W - FLOW_X - 4, 8,
                     "ILock: acquire/release SPI mutex", DIM, BG);
    y += 10;
    lcd.blitTextLine(FLOW_X + 4, y, LINE_W - FLOW_X - 4, 8,
                     "FreeRTOSMutex injected at init", dimColor(YELLOW), BG);

    // Footer
    lcd.blitTextLine(MARGIN, LCD::HEIGHT - 10, LINE_W, 8,
                     "L:matrix  R:diag  Handoff=yellow",
                     m_paused ? PAUSE_COLOR : CYAN, BG);
}

// =============================================================================
// Input handling
// =============================================================================

InputResult ArchScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;

    switch (m_mode) {
        case Mode::DIAGRAM: return handleDiagramInput(dir);
        case Mode::MATRIX:  return handleMatrixInput(dir);
        case Mode::FLOW:    return handleFlowInput(dir);
    }
    return InputResult::HANDLED;
}

InputResult ArchScreen::handleDiagramInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:
            m_mode = Mode::MATRIX;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

InputResult ArchScreen::handleMatrixInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            m_mode = Mode::DIAGRAM;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::RIGHT:
            m_mode = Mode::FLOW;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::CENTER:
            m_paused = !m_paused;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

InputResult ArchScreen::handleFlowInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            m_mode = Mode::MATRIX;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::RIGHT:
            m_mode = Mode::DIAGRAM;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::CENTER:
            m_paused = !m_paused;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        default:
            return InputResult::HANDLED;
    }
}

} // namespace UI
