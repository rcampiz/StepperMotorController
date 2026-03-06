/**
 * @file arch_screen.cpp
 * @brief Architecture visualization screen implementation
 *
 * Three modes: DIAGRAM (static layer overview), MATRIX (live heatmap),
 * FLOW (SCPI command lifecycle through layers).
 */

#include "ui/screens/arch_screen.hpp"
#include "harness/pins/icanvas.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "harness/trace/interface_trace.hpp"
#include <stdio.h>
#include <string.h>

namespace UI {

static constexpr uint16_t MARGIN = 4;
static constexpr uint16_t TITLE_H = 16;
static constexpr uint16_t LINE_H = 12;
static constexpr uint16_t LINE_W = Harness::Canvas::WIDTH - (2 * MARGIN);
static constexpr uint16_t WHITE   = 0xFFFF;
static constexpr uint16_t BG      = 0x0000;
static constexpr uint16_t CYAN    = 0x07FF;
static constexpr uint16_t GREEN   = 0x07E0;
static constexpr uint16_t MAGENTA = 0xF81F;
static constexpr uint16_t RED     = 0xF800;
static constexpr uint16_t YELLOW  = 0xFFE0;
static constexpr uint16_t ORANGE  = 0xFD20;
static constexpr uint16_t DIM     = 0x4208;
static constexpr uint16_t PAUSE_COLOR = 0xFFE0;

// Layer colors: L1=cyan, L2=green, L3=orange, L4=magenta, L5=red, F=yellow
static constexpr uint16_t LAYER_COLORS[] = {
    CYAN, GREEN, ORANGE, MAGENTA, RED, YELLOW
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

// Map boundary to layer index (0-5)
static uint8_t boundaryToLayer(uint16_t boundary) {
    if (boundary & Harness::ITrace::L1_L2_TRANSPORT) return 0;  // L1
    if (boundary & Harness::ITrace::L2_L3_DISPATCH)  return 1;  // L2
    if (boundary & Harness::ITrace::L2_TELEMETRY)    return 1;  // L2 (telemetry response)
    if (boundary & Harness::ITrace::L3_L4_MOTOR)     return 3;  // L4 (driver call)
    if (boundary & Harness::ITrace::L3_L4_ENCODER)   return 3;  // L4 (driver call)
    if (boundary & Harness::ITrace::L4_LCD)          return 3;  // L4 (LCD driver)
    if (boundary & Harness::ITrace::L4_FLASH)        return 3;  // L4 (flash driver)
    if (boundary & Harness::ITrace::L4_L5_SPI)       return 4;  // L5 (SPI hardware)
    if (boundary & Harness::ITrace::L5_F_LOCK)       return 5;  // F  (RTOS mutex)
    if (boundary & Harness::ITrace::L3_F_SAFETY)     return 2;  // L3 (service)
    if (boundary & Harness::ITrace::L3_CMD_SINK)     return 2;  // L3 (command queue)
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

void ArchScreen::render(Harness::ICanvas& lcd)
{
    if (!m_titleDrawn) {
        lcd.fillScreen(BG);
        m_titleDrawn = true;
    }

    switch (m_mode) {
        case Mode::DIAGRAM: renderDiagram(lcd); break;
        case Mode::MATRIX:  renderMatrix(lcd);  break;
        case Mode::FLOW:    renderFlow(lcd);    break;
        case Mode::TASKS:   renderTasks(lcd);   break;
    }
    m_needsRedraw = false;
}

// =============================================================================
// DIAGRAM mode — static architecture overview
// =============================================================================

void ArchScreen::renderDiagram(Harness::ICanvas& lcd)
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
            uint16_t arrowX = Harness::Canvas::WIDTH / 2;
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
    lcd.blitTextLine(MARGIN, Harness::Canvas::HEIGHT - 10, LINE_W, 8,
                     "L:back  R:matrix", CYAN, BG);
}

// =============================================================================
// MATRIX mode — live layers x tasks heatmap
// =============================================================================

void ArchScreen::renderMatrix(Harness::ICanvas& lcd)
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
    lcd.blitTextLine(MARGIN, Harness::Canvas::HEIGHT - 10, LINE_W, 8,
                     "CTR:pause L:diag R:flow",
                     m_paused ? PAUSE_COLOR : CYAN, BG);
}

// =============================================================================
// FLOW mode — SCPI command lifecycle
// =============================================================================

void ArchScreen::renderFlow(Harness::ICanvas& lcd)
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

    // --- Scan trace entries — fully data-driven ---
    // Both per-layer info and per-boundary info come from actual trace
    // entries.  No static interface name assumptions.
    struct LayerInfo {
        uint8_t taskId;                  // RTOS task that executed (0=none)
        const char* method;              // method name from trace (nullptr=none)
        char detail[Trace::DETAIL_SIZE]; // detail string from trace
        bool active;                     // has trace entries
    };
    LayerInfo layerInfo[NUM_LAYERS] = {};

    // Boundary crossing info — derived from trace, not a static table.
    // Display boundaries: [0]=L1-L2, [1]=L2-L3, [2]=L3-L4, [3]=L4-L5, [4]=L5-F
    struct BoundaryData {
        bool crossed;          // was this boundary actually used?
        const char* iface;     // interface name (derived from boundary type)
        const char* method;    // actual method called
    };
    BoundaryData bndData[5] = {};

    char lastCmd[20] = "(none)";

    // Two-phase scan: find the current SCPI command, then collect data
    // filtered to that command's RTOS task.
    Trace::Entry entry;
    size_t cmdRi = SIZE_MAX;
    uint8_t cmdTaskId = 0;

    // Phase 1: Find the most recent L2_L3_DISPATCH = current command
    for (size_t ri = 0; ri < count; ri++) {
        size_t i = count - 1 - ri;
        if (!Trace::getEntry(i, entry)) continue;
        if (entry.boundary & Harness::ITrace::L2_L3_DISPATCH) {
            strncpy(lastCmd, entry.detail, sizeof(lastCmd) - 1);
            lastCmd[sizeof(lastCmd) - 1] = '\0';
            cmdRi = ri;
            cmdTaskId = entry.taskId;
            break;
        }
    }

    // Phase 2: Collect per-layer AND per-boundary info from trace data
    if (cmdRi != SIZE_MAX) {
        for (size_t ri = 0; ri < count && ri <= cmdRi + 12; ri++) {
            size_t i = count - 1 - ri;
            if (!Trace::getEntry(i, entry)) continue;
            if (entry.taskId != cmdTaskId) continue;

            // Fill per-layer info
            uint8_t layer = boundaryToLayer(entry.boundary);
            if (layer < NUM_LAYERS && !layerInfo[layer].active) {
                layerInfo[layer].taskId = entry.taskId;
                layerInfo[layer].method = entry.method;
                strncpy(layerInfo[layer].detail, entry.detail, Trace::DETAIL_SIZE - 1);
                layerInfo[layer].detail[Trace::DETAIL_SIZE - 1] = '\0';
                layerInfo[layer].active = true;
            }

            // Fill per-boundary info (map trace boundary → display boundary)
            if ((entry.boundary & Harness::ITrace::L1_L2_TRANSPORT) && !bndData[0].crossed) {
                bndData[0] = { true, "ITransport", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L2_L3_DISPATCH) && !bndData[1].crossed) {
                bndData[1] = { true, "ICommandDispatcher", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L3_L4_MOTOR) && !bndData[2].crossed) {
                bndData[2] = { true, "IMotorDriver", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L3_L4_ENCODER) && !bndData[2].crossed) {
                bndData[2] = { true, "IEncoder", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L4_L5_SPI) && !bndData[3].crossed) {
                bndData[3] = { true, "ISPIBus", entry.method };
            }
            if ((entry.boundary & Harness::ITrace::L5_F_LOCK) && !bndData[4].crossed) {
                bndData[4] = { true, "ILock", entry.method };
            }
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

    // Draw the flow diagram
    static constexpr uint16_t FLOW_X = 20;     // Left margin for layer labels
    static constexpr uint16_t ARROW_X = 14;    // Vertical line x
    static constexpr uint16_t TASK_X = 150;    // Task label x
    // Compact layout to fit L1-L5 + F_platform in 320px
    static constexpr uint16_t LAYER_H = 16;    // Layer name + method
    static constexpr uint16_t ARROW_GAP = 20;  // Arrow + interface name + data desc
    static constexpr uint16_t STEP_H = LAYER_H + ARROW_GAP;
    static constexpr uint16_t F_SECTION_H = ARROW_GAP + LAYER_H; // L5→F boundary + F box

    // Clear fixed flow area (5 layers + arrows + F_platform section)
    static constexpr uint16_t MAX_FLOW_H = (SHOW_COUNT * STEP_H) + F_SECTION_H;
    lcd.fillRect(MARGIN, y, LINE_W, MAX_FLOW_H, BG);

    uint8_t prevTaskId = 0;
    for (uint8_t s = 0; s < SHOW_COUNT; s++) {
        uint8_t layer = SHOW_LAYERS[s];
        uint16_t color = LAYER_COLORS[layer];
        bool active = layerInfo[layer].active;
        uint16_t labelColor = active ? color : dimColor(color);

        // --- Layer box: two rows ---
        //   Row 1: Layer name (e.g. "L3 Services")  — layer color
        //   Row 2: Live trace method or static desc  — white (live) / dim (static)
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

            // Interface name — data-driven from trace entries.
            // Only show when the boundary was actually crossed.
            if (bndData[s].crossed) {
                uint16_t bndColor = LAYER_COLORS[SHOW_LAYERS[s]];
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 bndData[s].iface, bndColor, BG);
                // Show the actual method that crossed this boundary
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 bndData[s].method ? bndData[s].method : "",
                                 DIM, BG);
            } else {
                // Clear the boundary text area (not crossed)
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 "", BG, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 "", BG, BG);
            }
            y += ARROW_GAP;
        }

        if (taskId != 0) prevTaskId = taskId;
    }

    // --- L5 → F_platform boundary (data-driven, same as above) ---
    lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
    lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

    if (bndData[4].crossed) {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                         bndData[4].iface, RED, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                         bndData[4].method != nullptr ? bndData[4].method : "", DIM, BG);
    } else {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
    }
    y += ARROW_GAP;

    // F_platform layer box
    bool fActive = layerInfo[5].active;
    lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                 fActive ? dimColor(YELLOW) : 0x2104);
    lcd.blitTextLine(FLOW_X, y, 120, 8, "F  Platform",
                     fActive ? YELLOW : dimColor(YELLOW), BG);
    if (fActive && (layerInfo[5].method != nullptr)) {
        snprintf(buf, sizeof(buf), " %s()", layerInfo[5].method);
        lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8, buf, WHITE, BG);
    } else {
        lcd.blitTextLine(FLOW_X + 4, y + 8, 130, 8,
                         "FreeRTOSMutex impl", dimColor(YELLOW), BG);
    }

    // Footer
    lcd.blitTextLine(MARGIN, Harness::Canvas::HEIGHT - 10, LINE_W, 8,
                     "L:matrix  R:tasks  Handoff=yellow",
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
        case Mode::TASKS:   return handleTasksInput(dir);
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
            m_mode = Mode::TASKS;
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

// =============================================================================
// TASKS mode — per-task layer activity
// =============================================================================

// Task name table (indexed by m_selectedTask)
static constexpr const char* TASK_NAMES[] = { "Motor", "Encoder", "Comms", "Display", "Timer" };

void ArchScreen::renderTasks(Harness::ICanvas& lcd)
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
    char buf[40];
    uint8_t selTaskId = TASK_IDS[m_selectedTask];

    // Title: "Task: Motor" etc.
    if (m_paused) {
        snprintf(buf, sizeof(buf), "PAUSED  Task: %s", TASK_NAMES[m_selectedTask]);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, PAUSE_COLOR, BG);
    } else {
        snprintf(buf, sizeof(buf), "Task: %s", TASK_NAMES[m_selectedTask]);
        lcd.blitTextLine(MARGIN, y, LINE_W, 8, buf, CYAN, BG);
    }
    y += TITLE_H;
    lcd.drawHLine(MARGIN, y, LINE_W, CYAN);
    y += 4;

    // Scan trace entries filtered by selected taskId
    struct LayerInfo {
        const char* method;
        char detail[Trace::DETAIL_SIZE];
        bool active;
    };
    LayerInfo layerInfo[NUM_LAYERS] = {};

    // Boundary crossing data: [0]=L1-L2, [1]=L2-L3, [2]=L3-L4, [3]=L4-L5, [4]=L5-F
    struct BoundaryData {
        bool crossed;
        const char* iface;
        const char* method;
    };
    BoundaryData bndData[5] = {};

    // "Last" shows the most recent entry for this task
    char lastMethod[24] = "(idle)";

    Trace::Entry entry;
    bool foundFirst = false;
    for (size_t ri = 0; ri < count; ri++) {
        size_t i = count - 1 - ri;
        if (!Trace::getEntry(i, entry)) continue;
        if (entry.taskId != selTaskId) continue;

        // Capture the most recent entry as "Last"
        if (!foundFirst) {
            if (entry.detail[0] != '\0') {
                snprintf(lastMethod, sizeof(lastMethod), "%s(%s)",
                         entry.method != nullptr ? entry.method : "?", entry.detail);
            } else {
                snprintf(lastMethod, sizeof(lastMethod), "%s()",
                         entry.method != nullptr ? entry.method : "?");
            }
            foundFirst = true;
        }

        // Fill per-layer info
        uint8_t layer = boundaryToLayer(entry.boundary);
        if (layer < NUM_LAYERS && !layerInfo[layer].active) {
            layerInfo[layer].method = entry.method;
            strncpy(layerInfo[layer].detail, entry.detail, Trace::DETAIL_SIZE - 1);
            layerInfo[layer].detail[Trace::DETAIL_SIZE - 1] = '\0';
            layerInfo[layer].active = true;
        }

        // Fill per-boundary info
        if ((entry.boundary & Harness::ITrace::L1_L2_TRANSPORT) != 0 && !bndData[0].crossed) {
            bndData[0] = { true, "ITransport", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L2_L3_DISPATCH) != 0 && !bndData[1].crossed) {
            bndData[1] = { true, "ICommandDispatch", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L3_L4_MOTOR) != 0 && !bndData[2].crossed) {
            bndData[2] = { true, "IMotorDriver", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L3_L4_ENCODER) != 0 && !bndData[2].crossed) {
            bndData[2] = { true, "IEncoder", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L4_L5_SPI) != 0 && !bndData[3].crossed) {
            bndData[3] = { true, "ISPIBus", entry.method };
        }
        if ((entry.boundary & Harness::ITrace::L5_F_LOCK) != 0 && !bndData[4].crossed) {
            bndData[4] = { true, "ILock", entry.method };
        }

        // Stop after scanning enough entries for this task
        if (ri > 100) { break; }
    }

    // Show last activity
    snprintf(buf, sizeof(buf), "Last: %s", lastMethod);
    lcd.blitTextLine(MARGIN, y, LINE_W, LINE_H, buf, WHITE, BG);
    y += LINE_H + 2;

    // Layout constants (same as FLOW)
    static constexpr uint16_t FLOW_X = 20;
    static constexpr uint16_t ARROW_X = 14;
    static constexpr uint16_t LAYER_H = 16;
    static constexpr uint16_t ARROW_GAP = 20;
    static constexpr uint16_t STEP_H = LAYER_H + ARROW_GAP;
    static constexpr uint8_t SHOW_LAYERS[] = { 0, 1, 2, 3, 4 };
    static constexpr uint8_t SHOW_COUNT = 5;
    static constexpr uint16_t F_SECTION_H = ARROW_GAP + LAYER_H;
    static constexpr uint16_t MAX_FLOW_H = (SHOW_COUNT * STEP_H) + F_SECTION_H;

    lcd.fillRect(MARGIN, y, LINE_W, MAX_FLOW_H, BG);

    for (uint8_t s = 0; s < SHOW_COUNT; s++) {
        uint8_t layer = SHOW_LAYERS[s];
        uint16_t color = LAYER_COLORS[layer];
        bool active = layerInfo[layer].active;
        uint16_t labelColor = active ? color : dimColor(color);

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

        // Method/detail line
        if (active && layerInfo[layer].method != nullptr) {
            if (layerInfo[layer].detail[0] != '\0') {
                snprintf(buf, sizeof(buf), " %s(%s)",
                         layerInfo[layer].method, layerInfo[layer].detail);
            } else {
                snprintf(buf, sizeof(buf), " %s()", layerInfo[layer].method);
            }
            lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, buf, WHITE, BG);
        } else {
            lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, "(no trace)", DIM, BG);
        }
        y += LAYER_H;

        // Arrow + boundary between layers
        if (s < SHOW_COUNT - 1) {
            lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
            lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);

            if (bndData[s].crossed) {
                uint16_t bndColor = LAYER_COLORS[SHOW_LAYERS[s]];
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                                 bndData[s].iface, bndColor, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                                 bndData[s].method != nullptr ? bndData[s].method : "",
                                 DIM, BG);
            } else {
                lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
                lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
            }
            y += ARROW_GAP;
        }
    }

    // L5 → F boundary
    lcd.fillRect(ARROW_X, y, 1, ARROW_GAP - 2, DIM);
    lcd.fillRect(ARROW_X - 1, y + ARROW_GAP - 6, 3, 1, DIM);
    if (bndData[4].crossed) {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8,
                         bndData[4].iface, RED, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8,
                         bndData[4].method != nullptr ? bndData[4].method : "", DIM, BG);
    } else {
        lcd.blitTextLine(FLOW_X, y + 1, LINE_W - FLOW_X, 8, "", BG, BG);
        lcd.blitTextLine(FLOW_X + 4, y + 10, LINE_W - FLOW_X - 4, 8, "", BG, BG);
    }
    y += ARROW_GAP;

    // F_platform layer box
    bool fActive = layerInfo[5].active;
    lcd.fillRect(ARROW_X - 3, y, 7, LAYER_H - 2,
                 fActive ? dimColor(YELLOW) : 0x2104);
    lcd.blitTextLine(FLOW_X, y, 120, 8, "F  Platform",
                     fActive ? YELLOW : dimColor(YELLOW), BG);
    if (fActive && (layerInfo[5].method != nullptr)) {
        snprintf(buf, sizeof(buf), " %s()", layerInfo[5].method);
        lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, buf, WHITE, BG);
    } else {
        lcd.blitTextLine(FLOW_X + 4, y + 8, 150, 8, "(no trace)", DIM, BG);
    }

    // Footer
    lcd.blitTextLine(MARGIN, Harness::Canvas::HEIGHT - 10, LINE_W, 8,
                     "L:flow R:diag UD:task CTR:pause",
                     m_paused ? PAUSE_COLOR : CYAN, BG);
}

InputResult ArchScreen::handleTasksInput(JoyDirection dir)
{
    switch (dir) {
        case JoyDirection::LEFT:
            m_mode = Mode::FLOW;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::RIGHT:
            m_mode = Mode::DIAGRAM;
            m_titleDrawn = false;
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::UP:
            m_selectedTask = (m_selectedTask == 0) ? (NUM_TASKS - 1) : (m_selectedTask - 1);
            m_needsRedraw = true;
            return InputResult::HANDLED;
        case JoyDirection::DOWN:
            m_selectedTask = (m_selectedTask + 1) % NUM_TASKS;
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
