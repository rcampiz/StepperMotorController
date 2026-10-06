"""
Tests panel: run the core scenarios from the GUI (CLIENT_REVIEW section 9, rule 4).

The list, the parameters and the result format are smc_core.runner's; the CLI
(`smc test run NAME`) runs the same functions and prints the same ScenarioResult.
"""

import json
import logging

from PySide6.QtCore import Signal, Slot
from PySide6.QtWidgets import (QFileDialog, QHBoxLayout, QLabel, QListWidget, QPlainTextEdit,
                               QPushButton, QSplitter, QVBoxLayout, QWidget)
from PySide6.QtCore import Qt

from smc_core import runner

log = logging.getLogger("tests_panel")


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
        rv.addWidget(self._output, 3)
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
