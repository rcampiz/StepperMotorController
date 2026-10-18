"""
Tests panel: run the core scenarios from the GUI (CLIENT_REVIEW section 9, rule 4).

The list, the parameters and the result format are smc_core.runner's; the CLI
(`smc test run NAME`) runs the same functions and prints the same ScenarioResult.
"""

import json
import logging

from PySide6.QtCore import Signal, Slot
import pyqtgraph as pg

from smc_gui import theme
from PySide6.QtWidgets import (QCheckBox, QFileDialog, QHBoxLayout, QLabel, QListWidget, QPlainTextEdit,
                               QPushButton, QSplitter, QVBoxLayout, QWidget)
from PySide6.QtCore import Qt

from smc_core import runner

log = logging.getLogger("tests_panel")


class StepsAxis(pg.AxisItem):
    """Top axis of the ripple graph: full steps per second for the RPM below it. The ratio is
    the motor's full steps per revolution, so one axis serves every microstep mode."""

    def __init__(self, full_steps: int = 200):
        super().__init__(orientation="top")
        self.full_steps = full_steps

    def tickStrings(self, values, scale, spacing):
        return ["%.0f" % (v / 60.0 * self.full_steps) for v in values]


class TestsPanel(QWidget):
    run_requested = Signal(str, dict)       # (scenario name, params)
    cancel_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._specs = {s.name: s for s in runner.list_scenarios()}
        self._results = []
        self._running = None

        self._list = QListWidget()
        for name in self._specs:
            self._list.addItem(name)
        self._desc = QLabel()
        self._desc.setWordWrap(True)
        self._params = QPlainTextEdit()
        self._params.setPlaceholderText("{}")
        self._run_btn = QPushButton("Run")
        self._cancel_btn = QPushButton("Cancel")
        self._cancel_btn.setEnabled(False)
        self._save_btn = QPushButton("Save results...")
        self._output = QPlainTextEdit()
        self._output.setReadOnly(True)

        left = QWidget()
        lv = QVBoxLayout(left)
        lv.addWidget(QLabel("Scenarios"))
        lv.addWidget(self._list)
        right = QWidget()
        rv = QVBoxLayout(right)
        rv.addWidget(self._desc)
        rv.addWidget(QLabel("Parameters (JSON; defaults shown)"))
        rv.addWidget(self._params, 1)
        row = QHBoxLayout()
        for b in (self._run_btn, self._cancel_btn, self._save_btn):
            row.addWidget(b)
        row.addStretch()
        rv.addLayout(row)
        rv.addWidget(QLabel("Results"))
        rv.addWidget(self._output, 2)
        # Ripple graph for resonance_sweep / resonance_map: RPM on the bottom axis, one curve
        # per microstep mode; the top axis (full steps/s) is optional, same ratio for every mode
        graph_row = QHBoxLayout()
        graph_row.addWidget(QLabel("Ripple vs speed"))
        self._steps_axis_cb = QCheckBox("full steps/s on the top axis")
        self._steps_axis_cb.toggled.connect(self._replot)
        graph_row.addWidget(self._steps_axis_cb)
        self._save_graph_btn = QPushButton("Save graph...")
        self._save_graph_btn.clicked.connect(self._on_save_graph)
        graph_row.addWidget(self._save_graph_btn)
        graph_row.addStretch()
        rv.addLayout(graph_row)
        self._plot = pg.PlotWidget()
        self._plot.setBackground(theme.BASE)
        self._plot.setLabel("left", "velocity ripple (std, counts/s)")
        self._plot.setLabel("bottom", "RPM")
        self._plot.showGrid(x=True, y=True, alpha=0.3)
        self._legend = self._plot.addLegend(offset=(10, 10))
        self._steps_axis = StepsAxis()
        self._plot.getPlotItem().setAxisItems({"top": self._steps_axis})
        self._plot.getPlotItem().showAxis("top", False)
        self._graph_result = None
        rv.addWidget(self._plot, 2)
        split = QSplitter(Qt.Horizontal)
        split.addWidget(left)
        split.addWidget(right)
        split.setSizes([180, 520])
        QVBoxLayout(self).addWidget(split)

        self._list.currentTextChanged.connect(self._on_select)
        self._run_btn.clicked.connect(self._on_run)
        self._cancel_btn.clicked.connect(self.cancel_requested.emit)
        self._save_btn.clicked.connect(self._on_save)
        if self._list.count():
            self._list.setCurrentRow(0)

    @Slot(str)
    def _on_select(self, name: str):
        spec = self._specs.get(name)
        if spec is None:
            return
        warn = "  MOVES THE MOTOR." if spec.moves_motor else ""
        self._desc.setText(spec.description + warn)
        self._params.setPlainText(json.dumps(spec.params, indent=1))

    @Slot()
    def _on_run(self):
        name = self._list.currentItem().text() if self._list.currentItem() else None
        if not name:
            return
        try:
            params = json.loads(self._params.toPlainText() or "{}")
        except json.JSONDecodeError as e:
            self._output.appendPlainText(f"bad parameters: {e}")
            return
        self.start(name, params)

    def start(self, name: str, params: dict):
        self._running = name
        self._run_btn.setEnabled(False)
        self._cancel_btn.setEnabled(True)
        self._output.appendPlainText(f"> {name} {json.dumps(params)}")
        self.run_requested.emit(name, params)

    def show_result(self, result):
        self._running = None
        self._run_btn.setEnabled(True)
        self._cancel_btn.setEnabled(False)
        if isinstance(result, Exception):
            self._output.appendPlainText(f"  ERROR {result}")
            return
        self._results.append(result)
        self._output.appendPlainText("  " + result.summary)
        for c in result.checks:
            self._output.appendPlainText(f"    [{'ok' if c.ok else 'FAIL'}] {c.name} {c.detail}")
        shown = {k: v for k, v in result.measurements.items() if k not in ("samples", "traceback")}
        if shown:
            self._output.appendPlainText("    " + json.dumps(shown))
        if (any(k.startswith(("rpm_", "mode_")) for k in result.measurements)
                or "capture" in result.measurements or "bode" in result.measurements):
            self._graph_result = result
            self._replot()

    _PENS = ("#4a9eff", "#00cc66", "#ffcc00", "#ff6b6b", "#c678dd", "#56b6c2", "#e5c07b", "#ffffff")

    @staticmethod
    def curves_of(measurements: dict):
        """[(label, [(rpm, std)...])] from a resonance_sweep or resonance_map result."""
        out = []
        modes = sorted((v for k, v in measurements.items() if k.startswith("mode_") and isinstance(v, dict)),
                       key=lambda v: v.get("ratio", 0))
        for v in modes:
            pts = sorted((int(r), float(sd)) for r, sd in v.get("std", {}).items())
            label = "1/%d (%d \u00b5steps/rev, peak %s rpm)" % (v.get("ratio", 0), v.get("usteps_per_rev", 0),
                                                                v.get("peak_rpm", "?"))
            out.append((label, pts))
        single = sorted((int(k[4:]), float(v["vel_std"])) for k, v in measurements.items()
                        if k.startswith("rpm_") and isinstance(v, dict) and "vel_std" in v)
        if single:
            out.append(("sweep (peak %s rpm)" % measurements.get("worst_rpm", "?"), single))
        return out

    @Slot()
    def _replot(self):
        self._plot.clear()
        self._legend.clear()
        res = self._graph_result
        if res is None:
            return
        bode = res.measurements.get("bode")
        if isinstance(bode, dict) and bode:
            # filter_bode: lag of the reported velocity against frequency, one curve per chain
            self._plot.getPlotItem().showAxis("top", False)
            self._plot.setLabel("bottom", "frequency (Hz)")
            self._plot.setLabel("left", "lag of the reported velocity (ms)")
            rec = res.measurements.get("recommended")
            for i, (name, r) in enumerate(bode.items()):
                label = "%s: %.0f ms at %.1f Hz, gain %.2f, noise %.1f%s" % (
                    name, r.get("lag_at_target_ms", 0), res.measurements.get("target_hz", 0),
                    r.get("gain_at_target", 0), r.get("noise_mean", 0), "  (recommended)" if name == rec else "")
                pen = self._PENS[i % len(self._PENS)]
                self._plot.plot(r["freqs"], r["lag_ms"], name=label, pen=pg.mkPen(pen, width=2 if name == rec else 1),
                                symbol="o", symbolSize=5, symbolBrush=pen)
            return
        cap = res.measurements.get("capture")
        if isinstance(cap, dict) and cap.get("t_ms"):
            # position_capture: encoder minus commanded position against time
            self._plot.getPlotItem().showAxis("top", False)
            self._plot.setLabel("bottom", "time (ms)")
            self._plot.setLabel("left", "position error (encoder counts)")
            label = "%s rpm, %d Hz: std %.1f, peak %.0f Hz (amp %.1f)" % (
                cap.get("rpm", "?"), cap.get("rate_hz", 0), cap.get("err_std", 0),
                cap.get("peak_hz", 0), cap.get("peak_amp", 0))
            self._plot.plot(cap["t_ms"], cap["err"], name=label, pen=pg.mkPen(self._PENS[0], width=1))
            return
        self._plot.setLabel("bottom", "RPM")
        self._plot.setLabel("left", "velocity ripple (std, counts/s)")
        self._steps_axis.full_steps = int(res.measurements.get("full_steps", 200))
        self._plot.getPlotItem().showAxis("top", self._steps_axis_cb.isChecked())
        self._plot.setLabel("top", "full steps/s" if self._steps_axis_cb.isChecked() else None)
        for i, (label, pts) in enumerate(self.curves_of(res.measurements)):
            if not pts:
                continue
            self._plot.plot([p[0] for p in pts], [p[1] for p in pts], name=label,
                            pen=pg.mkPen(self._PENS[i % len(self._PENS)], width=2), symbol="o", symbolSize=5,
                            symbolBrush=self._PENS[i % len(self._PENS)])

    @Slot()
    def _on_save_graph(self):
        if self._graph_result is None:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save graph", "ripple.png", "PNG (*.png)")
        if path:
            from pyqtgraph.exporters import ImageExporter
            ImageExporter(self._plot.getPlotItem()).export(path)
            self._output.appendPlainText(f"graph saved to {path}")

    @property
    def results(self):
        return list(self._results)

    @Slot()
    def _on_save(self):
        if not self._results:
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save results", "results.jsonl",
                                              "JSON lines (*.jsonl)")
        if path:
            runner.write_jsonl(path, self._results)
            self._output.appendPlainText(f"saved {len(self._results)} result(s) to {path}")
