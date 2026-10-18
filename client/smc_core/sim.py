"""
FakeBoard: a small pure-Python protocol-2 responder for tests and demos.

It is NOT the firmware: the virtual Nucleo (real L2 + L3 compiled for the host) is
the fidelity target. This fake covers what the core needs to be tested without
hardware: both reply formats, echo, JSON envelopes, multi-line replies, binary
uploads with CRC, pushed telemetry, sequenced events, the heartbeat watchdog,
the ESTOP latch, and injectable faults (reset, silence, lost events, delays).

Attach it to a MemoryTransport:  t = MemoryTransport(); fake = FakeBoard(t)
"""

import base64
import binascii
import json
import struct
import threading
import time
import zlib
from typing import Any, Dict, List, Optional

from .protocol_spec import COMMANDS, PROTOCOL_VERSION


class FakeBoard:
    FW = "0.4.0-sim"

    def __init__(self, transport, serial: str = "SIM00001", boot: int = 0x5EED0001,
                 push_threads: bool = True):
        self.t = transport
        transport.attach(self)
        self.serial = serial
        self.boot = boot
        self.json = False
        self.echo = True
        self.state = "IDLE"
        self.position = 0
        self.speed = 0
        self.target = None                    # MOT:MOVE/GOTO in progress: position moves toward it
        self.move_usteps_per_s = 0.0
        self.hi_z = True
        self.role = "NONE"
        self.dev_id = 0
        self.step_mode = 3
        self.drive_current = True
        self.tval = (0x0D, 0x1A, 0x1D, 0x1D)      # firmware defaults: 23HS22-2804-ME1K
        self.full_steps = 200
        self.enc_ppr = 1600                   # counts/rev: one count per 1/8 microstep
        self.hb_timeout_ms = 0
        self.last_hb = time.monotonic()
        self.tlm_ms = 0
        self.tlm_compact = False
        self.events_on = False
        self.seq = 0
        self.received: List[str] = []           # every command line, in order
        self.silent = False                      # stop answering (heartbeat-loss tests)
        self.delay_next_s = 0.0                  # delay the next reply (late-reply tests)
        self.skip_next_seq = 0                   # lose N event lines before the next one
        self._buf = bytearray()
        self._binary_left = 0
        self._binary_name = ""
        self._binary_crc = False
        self._binary_data = bytearray()
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self.uploads: List[bytes] = []
        if push_threads:
            threading.Thread(target=self._tick_loop, daemon=True, name="fakeboard").start()

    # ------------------------------------------------------------------
    def stop(self) -> None:
        self._stop.set()

    def boot_banner(self) -> None:
        """Power-up / reset: the !RESET line, the banner, back to ASCII with echo."""
        with self._lock:
            self.json, self.echo = False, True
            self.tlm_ms, self.events_on, self.seq, self.hb_timeout_ms = 0, False, 0, 0
            self.state = "IDLE"
            for line in ("!RESET boot=%08X fw=%s proto=%d build=sim" % (self.boot, self.FW,
                                                                        PROTOCOL_VERSION),
                         "", "========================================",
                         "   Stepper Motor Controller v%s" % self.FW,
                         "========================================",
                         "Type 'SYST:HELP?' for available commands", ""):
                self.t.feed_line(line)

    def reboot(self, new_boot: Optional[int] = None) -> None:
        self.boot = new_boot if new_boot is not None else (self.boot + 1) & 0xFFFFFFFF
        self.boot_banner()

    def emit_event(self, typ: str, status: int = 0, detail: Optional[str] = None) -> None:
        with self._lock:
            if not self.events_on:
                return
            self.seq += 1 + self.skip_next_seq
            self.skip_next_seq = 0
            if self.json:
                data: Dict[str, Any] = {"status": status}
                if detail is not None:
                    data["detail"] = detail
                self.t.feed_line(json.dumps({"kind": "event", "type": typ, "seq": self.seq,
                                             "ts_ms": int(time.monotonic() * 1000) & 0xFFFFFFFF,
                                             "data": data}, separators=(",", ":")))
            else:
                self.t.feed_line("!%s status=0x%04X%s" % (typ, status,
                                                         " detail=%s" % detail if detail else ""))

    def status(self) -> Dict[str, Any]:
        return {
            "state": self.state, "tick": int(time.monotonic() * 1e6) & 0xFFFFFFFF,
            "queue_depth": 0,
            "motor": {"position": self.position, "speed": self.speed,
                      "busy": self.speed != 0 or self.target is not None, "hi_z": self.hi_z, "direction": 1,
                      "mot_status": 0, "status_reg": "7E03", "cmd_err": False, "ocd": False,
                      "thermal_sd": False, "thermal_warn": False, "uvlo": False,
                      "stall_a": False, "stall_b": False},
            "encoder": {"count": self._enc_count(), "velocity": self.speed, "index_seen": False,
                        "revolutions": 0, "index_period_us": 0, "vel_quality": 0},
            "control": {"following_error": 0, "setpoint": self.position, "mode": 0,
                        "tracking": False, "trim_out": 0, "p": 0, "i": 0, "d": 0,
                        "sup_state": 0, "tier": 0, "vel_error": 0, "retries": 0,
                        "base_spd": 0, "trim_spd": 0, "final_spd": 0, "trim_frozen": 0,
                        "vel_quality": 0},
            "heartbeat": {"enabled": self.hb_timeout_ms > 0, "timeout_ms": self.hb_timeout_ms,
                          "remaining_ms": self._hb_remaining(), "timed_out": False},
            "system": {"uptime_ticks": 0, "free_heap": 512, "rx_overflows": 0,
                       "rx_overflow_lines": 0},
            "mode": "OPEN_LOOP", "encoder_status": "READY", "error": False,
        }

    # ------------------------------------------------------------------
    def on_host_bytes(self, data: bytes) -> None:
        with self._lock:
            for b in data:
                if self._binary_left > 0:
                    if b in (0x0D, 0x0A, 0x20, 0x09):
                        continue                  # the firmware's base64 reader skips whitespace
                    self._binary_data.append(b)
                    self._binary_left -= 1
                    if self._binary_left == 0:
                        self._finish_binary()
                    continue
                if b in (0x0D, 0x0A):
                    if self._buf:
                        line = self._buf.decode("ascii", "replace")
                        self._buf.clear()
                        self._line(line)
                    continue
                if 0x20 <= b < 0x7F:          # the firmware drops other bytes (0x18 included)
                    self._buf.append(b)

    def _reply(self, line: str) -> None:
        if self.silent:
            return
        if self.delay_next_s > 0:
            d, self.delay_next_s = self.delay_next_s, 0.0
            threading.Timer(d, self.t.feed_line, args=(line,)).start()
        else:
            self.t.feed_line(line)

    def _ok(self, cmd: str, message: str = "", data: Optional[Dict[str, Any]] = None) -> None:
        if self.json:
            payload = data if data is not None else ({"message": message} if message else {})
            self._reply(json.dumps({"status": "ok", "command": cmd, "data": payload},
                                   separators=(",", ":")))
        else:
            self._reply(("OK " + message).rstrip() if message or data is None else
                        "OK " + " ".join("%s=%s" % kv for kv in data.items()))

    def _err(self, cmd: str, code: str, message: str) -> None:
        if self.json:
            self._reply(json.dumps({"status": "error", "command": cmd, "code": code,
                                    "message": message}, separators=(",", ":")))
        else:
            self._reply("ERROR %s %s" % (code, message))

    def _lines(self, name: str, lines: List[str], extra: Optional[Dict[str, Any]] = None) -> None:
        if self.json:
            data = dict(extra or {})
            data["lines"] = lines
            data["count"] = len(lines)
            self._reply(json.dumps({"status": "ok", "command": name, "data": data},
                                   separators=(",", ":")))
        else:
            for l in lines:
                self._reply(l)
            self._reply("OK %d" % len(lines))

    # ------------------------------------------------------------------
    def _line(self, line: str) -> None:
        self.received.append(line)
        if self.echo and not self.silent:
            self.t.feed_line(line)
        parts = line.split()
        name, args = parts[0].upper(), parts[1:]
        h = getattr(self, "_c_" + name.replace(":", "_").replace("?", "_Q").replace("*", "S"), None)
        if h is not None:
            h(name, args)
        elif name in COMMANDS:
            self._ok(name)
        else:
            self._err(name, "UNKNOWN_CMD", "Unknown command")

    def _finish_binary(self) -> None:
        try:
            data = base64.b64decode(bytes(self._binary_data), validate=True)
        except (binascii.Error, ValueError):
            self._err(self._binary_name, "USAGE", "Bad base64 at byte 0/0")
            return
        if self._binary_crc:
            body, crc = data[:-4], struct.unpack("<I", data[-4:])[0]
            if zlib.crc32(body) & 0xFFFFFFFF != crc:
                self._err(self._binary_name, "CRC", "CRC mismatch")
                return
            data = body
        self.uploads.append(data)
        self._ok(self._binary_name, "Image received")

    def _start_binary(self, name: str, n: int, crc: bool) -> None:
        """The board expects n decoded bytes (+ CRC32) as base64 text: 4 chars per 3 bytes."""
        self._binary_name, self._binary_crc = name, crc
        self._binary_left = 4 * ((n + (4 if crc else 0) + 2) // 3)
        self._binary_data = bytearray()
        self._reply("OK READY %d" % n)

    # --- handlers ------------------------------------------------------------
    def _c_SIDN_Q(self, name, args):
        if self.json:
            self._ok("IDN", data={"vendor": "SMC", "model": "NUCLEO-F401RE-IHM03A1",
                                  "serial": self.serial, "fw": self.FW, "proto": PROTOCOL_VERSION})
        else:
            self._ok(name, "SMC,NUCLEO-F401RE-IHM03A1,%s,%s,%d" % (self.serial, self.FW,
                                                                  PROTOCOL_VERSION))

    def _c_FMT(self, name, args):
        if args and args[0].upper() == "JSON":
            self.json, self.echo = True, False
            self._ok("SET_FORMAT", data={"format": "JSON"})
        else:
            self.json, self.echo = False, True
            self._ok(name, "ASCII")

    def _c_SYST_HB(self, name, args):
        self.last_hb = time.monotonic()
        seq = int(args[0]) if args else 0
        if self.json:
            self._ok("HEARTBEAT", data={"seq": seq, "mcu_tick": 0,
                                        "remaining_ms": self._hb_remaining(), "boot": self.boot})
        else:
            self._reply("HEARTBEAT_ACK %d 0 %d %08X" % (seq, self._hb_remaining(), self.boot))

    def _c_SYST_HB_TIMEOUT(self, name, args):
        ms = int(args[0])
        if ms == 0 and not self.hi_z:                      # F2: disarm only in Hi-Z
            self._err("SET_HEARTBEAT", "INVALID_STATE",
                      "Watchdog can be disarmed only with the motor in Hi-Z (MOT:DIS); widen it instead")
            return
        self.hb_timeout_ms = 0 if ms == 0 else max(100, min(5000, ms))
        self.last_hb = time.monotonic()
        self._ok("SET_HEARTBEAT", data={"requested": ms, "accepted": self.hb_timeout_ms,
                                        "enabled": self.hb_timeout_ms > 0})

    def _c_SYST_TLM_FMT(self, name, args):
        self.tlm_compact = args[0].upper() == "COMPACT"
        self._ok("TLM_FMT", data={"format": "compact" if self.tlm_compact else "full",
                                  "period_ms": self.tlm_ms, "min_ms": 42 if self.tlm_compact else 139})

    def _c_SYST_TLM_FMT_Q(self, name, args):
        self._ok("TLM_FMT", data={"format": "compact" if self.tlm_compact else "full",
                                  "min_ms": 42 if self.tlm_compact else 139})

    def _c_SYST_TLM_RATE(self, name, args):
        ms = int(args[0])
        self.tlm_ms = 0 if ms == 0 else max(20, min(5000, ms))
        self._ok("TLM_RATE", data={"requested": ms, "accepted": self.tlm_ms,
                                   "enabled": self.tlm_ms > 0})

    def _c_SYST_EVT_EN(self, name, args):
        self.events_on = True
        self._ok(name, "EVENTS_ENABLED")

    def _c_SYST_EVT_DIS(self, name, args):
        self.events_on = False
        self._ok(name)

    def _c_SYST_EVT_STAT_Q(self, name, args):
        self._ok(name, data={"mask": 255, "sent": self.seq, "lost_critical": 0, "lost_info": 0,
                             "last_seq": self.seq, "depth": 0})

    def _c_SYST_ESTOP(self, name, args):
        self.state, self.speed, self.hi_z = "ESTOP", 0, True
        self._ok(name, "ESTOP")

    def _c_SYST_FAULT_CLEAR(self, name, args):
        self.state = "IDLE"
        self._ok(name, "IDLE")

    def _c_DIAG_STAT_Q(self, name, args):
        if self.json:
            self._ok("GET_STATUS", data=self.status())
        else:
            self._reply("STATUS %s 0 0 OPEN_LOOP READY %d %d" % (self.state, self.position,
                                                                 self.speed))

    def _c_DIAG_PING(self, name, args):
        self._ok("PING", data={"seq": int(args[0]) if args else 0, "rx_tick": 0, "tx_tick": 0,
                               "state": self.state})

    def _motion_allowed(self, name) -> bool:
        if self.state in ("ESTOP", "FAULT"):
            self._err(name, "INVALID_STATE", "motion refused while latched")
            return False
        return True

    def _c_MOT_MOVE(self, name, args):
        if self._motion_allowed(name):
            steps = int(float(args[0]) * self._usteps_per_rev()) if "REV" in args else int(args[0])
            d = int(args[2] if "REV" in args else args[1])
            spd = args[3] if "REV" in args else (args[2] if len(args) > 2 else None)
            self._start_move(self.position + (steps if d else -steps), spd)
            self._ok(name)

    move_speed_sps = 400                       # MOT:MOVE without a speed: full steps/s

    def _start_move(self, target: int, speed_arg) -> None:
        """Positioning moves take time: the tick loop walks position to target."""
        sps = int(float(speed_arg)) if speed_arg not in (None, "") else self.move_speed_sps
        self.move_usteps_per_s = max(sps, 1) * (1 << self.step_mode)
        self.target = target
        self.hi_z = False

    def _usteps_per_rev(self) -> int:
        return self.full_steps * (1 << self.step_mode)

    def _enc_count(self) -> int:
        """The encoder follows the motor: enc_ppr counts per revolution."""
        return round(self.position * self.enc_ppr / self._usteps_per_rev())

    def _c_MOT_GOTO(self, name, args):
        if self._motion_allowed(name):
            self._start_move(int(float(args[0])), args[2] if "REV" in args else (args[1] if len(args) > 1 else None))
            self._ok(name)

    def _c_MOT_RUN(self, name, args):
        if self._motion_allowed(name):
            self.speed = int(float(args[0]))
            self.hi_z = False
            self._ok(name)

    def _c_MOT_STOP(self, name, args):
        self.speed = 0
        self._ok(name)

    def _c_MOT_EN(self, name, args):
        self.hi_z = False
        self._ok(name, "ENABLED")

    def _c_MOT_DIS(self, name, args):
        self.hi_z, self.speed = True, 0
        self._ok(name, "HI-Z")

    def _c_DRV_STEP_MODE_Q(self, name, args):
        self._ok(name, data={"step_mode": self.step_mode, "microsteps": 1 << self.step_mode,
                             "drive_mode": "current" if self.drive_current else "voltage"})

    def _c_DRV_FULL_STEPS_Q(self, name, args):
        self._ok(name, data={"full_steps_per_rev": self.full_steps})

    def _c_DRV_ENC_PPR_Q(self, name, args):
        self._ok(name, data={"encoder_ppr": self.enc_ppr})

    def _c_DRV_CFG_Q(self, name, args):
        self._lines("MCONFIG", [
            "Motor Config (defaults)", "KVAL: HOLD=29 RUN=29 ACC=29 DEC=29",
            "Drive: %s" % ("CURRENT" if self.drive_current else "VOLTAGE"),
            "TVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X" % self.tval,
            "OCD_TH=08 STALL_TH=10", "ACC=500 DEC=500 MAXSPD=800 MINSPD=0 FS_SPD=1023",
            "Faults: OCD=1 TH_SD=1 TH_W=1 UVLO=1 STALL_A=0 STALL_B=0 CMD=1",
            "Action: HardHiZ", "StepMode: %d (1/%d)" % (self.step_mode, 1 << self.step_mode)])

    def _c_DBG_MOTOR(self, name, args):
        self._lines("MOTOR_DEBUG", [
            "STATUS=7E03 (%s Idle)" % ("HiZ" if self.hi_z else "Active"),
            "KVAL: HOLD=29 RUN=29 ACC=29 DEC=29",
            "ACC=500 DEC=500 MAXSPD=800 POS=%d" % self.position])

    def _c_SYST_VER_Q(self, name, args):
        self._lines("VERSION", ["Stepper Motor Controller v%s" % self.FW, "Build: sim",
                                "Protocol: %d (boot %08X)" % (PROTOCOL_VERSION, self.boot),
                                "Device: %d (%s)" % (self.dev_id, self.role),
                                "Mode: OPEN_LOOP (encoder: READY)"])

    def _c_CTRL_ENC_Q(self, name, args):
        self._ok("ENC", data={"count": self._enc_count(), "velocity": self.speed,
                              "index_seen": False, "index_tick": 0, "revolutions": 0,
                              "index_period_us": 0})

    def _c_DEV_ROLE(self, name, args):
        self.role = args[0]
        self._ok(name, "Role saved")

    def _c_DEV_ROLE_Q(self, name, args):
        self._ok("GET_ROLE", data={"role": self.role, "role_id": 0})

    def _c_UI_DISP_BITMAP_RLE(self, name, args):
        self._start_binary(name, int(args[4]), "CRC" in args[5:])

    def _c_UI_FLASH_UPLOAD_RLE(self, name, args):
        self._start_binary(name, int(args[1]), "CRC" in args[2:])

    def _c_UI_FLASH_UPLOAD(self, name, args):
        self._start_binary(name, 240 * 320 * 2, "CRC" in args[1:])

    def _c_UI_DISP_BITMAP(self, name, args):
        self._start_binary(name, int(args[2]) * int(args[3]) * 2, "CRC" in args[4:])

    def _c_DRV_CFG_DRIVE(self, name, args):
        self.drive_current = args[0].upper() in ("CURRENT", "1")
        self._ok(name, "Drive mode %s set" % ("CURRENT" if self.drive_current else "VOLTAGE"))

    def _c_DRV_CFG_TVAL(self, name, args):
        self.tval = tuple(int(a, 16) & 0x7F for a in args[:4])
        self._ok(name, "TVAL set")

    def _c_DRV_CFG_STEPMODE(self, name, args):
        self.step_mode = int(args[0]) & 7
        self._ok(name, "StepMode %d" % self.step_mode)

    def _c_DRV_STEP_MODE(self, name, args):
        self.step_mode = int(args[0]) & 7
        self._ok(name)

    # --- system identification: a test "runs" for sysid_run_s, then has N samples --------
    sysid_run_s = 0.15
    sysid_samples = 95

    def _sysid_start(self, name, args):
        if not self._motion_allowed(name):
            return
        self.sysid_phase, self.sysid_done_at = 2, time.monotonic() + self.sysid_run_s
        self._ok(name, "SYSID started")

    _c_CTRL_SYSID_STEP = _c_CTRL_SYSID_RAMP = _c_CTRL_SYSID_SINE = _sysid_start
    _c_CTRL_SYSID_TRAPEZOID = _c_CTRL_SYSID_RECT = _sysid_start

    def _sysid_phase_now(self) -> int:
        if getattr(self, "sysid_phase", 0) == 2 and time.monotonic() >= self.sysid_done_at:
            self.sysid_phase = 4
        return getattr(self, "sysid_phase", 0)

    def _c_CTRL_SYSID_STATUS_Q(self, name, args):
        phase = self._sysid_phase_now()
        n = self.sysid_samples if phase == 4 else 0
        if self.json:
            self._ok("CTRL:SYSID:STATUS", data={"phase": phase, "samples": n})
        else:
            self._ok(name, "phase=%d samples=%d" % (phase, n))

    def _c_CTRL_SYSID_DATA_Q(self, name, args):
        if self._sysid_phase_now() != 4:
            self._err(name, "ERROR", "No data ready (test not DONE)")
            return
        offset = int(args[0]) if args else 0
        count = int(args[1]) if len(args) > 1 else 20
        total = self.sysid_samples
        if offset >= total:
            self._err(name, "ERROR", "offset past end")
            return
        count = min(count, total - offset)
        rows = ["%d,%d,%d,%d" % (i * 5, 800, 800 - max(0, 40 - i), max(0, 40 - i) // 4)
                for i in range(offset, offset + count)]
        if self.json:
            self._lines("SYSID_DATA", rows, {"offset": offset, "samples": count, "total": total})
        else:
            self._lines("SYSID_DATA", ["SYSID_DATA %d %d %d" % (offset, count, total)] + rows)

    def _c_CTRL_SYSID_ABORT(self, name, args):
        self.sysid_phase = 0
        self._ok(name, "SYSID aborted")

    # ------------------------------------------------------------------
    def _hb_remaining(self) -> int:
        if self.hb_timeout_ms <= 0:
            return 0
        left = self.hb_timeout_ms - (time.monotonic() - self.last_hb) * 1000.0
        return max(0, int(left))

    def _tick_loop(self) -> None:
        next_tlm = last = time.monotonic()
        while not self._stop.wait(0.005):
            with self._lock:
                now = time.monotonic()
                if self.target is not None:
                    step = max(1, int(self.move_usteps_per_s * (now - last)))
                    if abs(self.target - self.position) <= step:
                        self.position, self.target = self.target, None
                    else:
                        self.position += step if self.target > self.position else -step
                last = now
                if (self.hb_timeout_ms > 0 and self.state != "ESTOP"
                        and (now - self.last_hb) * 1000.0 > self.hb_timeout_ms):
                    self.state, self.speed, self.hi_z = "ESTOP", 0, True   # comms-timeout stop
                if self.tlm_ms > 0 and now >= next_tlm and not self.silent:
                    next_tlm = now + self.tlm_ms / 1000.0
                    if self.json:
                        st = self.status()
                        if self.tlm_compact:
                            st = {"state": st["state"], "tick": st["tick"],
                                  "motor": {k: st["motor"][k] for k in ("position", "speed", "busy", "hi_z")},
                                  "encoder": {k: st["encoder"][k] for k in ("count", "velocity")},
                                  "control": {"following_error": st["control"]["following_error"]},
                                  "heartbeat": {"remaining_ms": st["heartbeat"]["remaining_ms"]}}
                        self.t.feed_line(json.dumps({"kind": "telemetry", "data": st},
                                                    separators=(",", ":")))
                    else:
                        self.t.feed_line("TELEM: pos=%d spd=%d enc=%d vel=%d" % (
                            self.position, self.speed, self._enc_count(), self.speed))
