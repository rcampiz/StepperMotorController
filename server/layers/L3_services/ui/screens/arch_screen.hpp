/**
 * @file arch_screen.hpp
 * @brief Architecture visualization screen
 *
 * Three view modes accessible via RIGHT button:
 *
 * DIAGRAM — Static architecture overview
 *   Colored boxes showing L1-L5 layer stack with F_platform (tasks)
 *   shown separately to illustrate orthogonality.
 *   LEFT: Exit screen
 *   RIGHT: Switch to Matrix mode
 *
 * MATRIX — Live layers x tasks heatmap
 *   Grid where rows = layers, columns = tasks.
 *   Cell intensity shows trace activity for that combination.
 *   CENTER: Pause/resume
 *   LEFT: Back to Diagram
 *   RIGHT: Switch to Flow mode
 *
 * FLOW — SCPI command lifecycle
 *   Shows the most recent command's path through layers
 *   with task handoff visualization.
 *   LEFT: Back to Matrix
 *   RIGHT: Switch to Diagram
 */

#ifndef ARCH_SCREEN_HPP
#define ARCH_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>
#include <stddef.h>

class LCD;

namespace UI {

class ArchScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::STATUS; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    bool needsFullRedraw() const override { return false; }
    void clearRedrawFlag() override { }

private:
    enum class Mode : uint8_t { DIAGRAM, MATRIX, FLOW };

    void renderDiagram(LCD& lcd);
    void renderMatrix(LCD& lcd);
    void renderFlow(LCD& lcd);
    InputResult handleDiagramInput(JoyDirection dir);
    InputResult handleMatrixInput(JoyDirection dir);
    InputResult handleFlowInput(JoyDirection dir);

    Mode m_mode = Mode::DIAGRAM;
    bool m_needsRedraw = true;
    bool m_titleDrawn = false;
    bool m_paused = false;
    size_t m_lastTotal = 0;

    // Matrix mode: 6 layers x 5 tasks count grid
    uint8_t m_cellCounts[6][5] = {};
};

} // namespace UI

#endif // ARCH_SCREEN_HPP
