"""
FakeBoard: a small pure-Python protocol responder for tests and demos (protocol 3 by default;
FakeBoard(t, protocol=2) answers like a protocol 2 board: no DRV:CAPS?, no physical commands).

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
from .version import __version__


class FakeBoard:
    FW = __version__ + "-sim"

    def __init__(self, transport, serial: str = "SIM00001", boot: int = 0x5EED0001,
                 push_threads: bool = True, protocol: int = PROTOCOL_VERSION):
        self.t = transport
        self.protocol = int(protocol)
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
        self.tval = (0x04, 0x11, 0x14, 0x14)      # firmware defaults: 23HS22-2804-ME1K
        # the rest of the driver settings, kept as the powerSTEP01 codes they round to (the
        # physical commands land on the same grid, so the fake quantises like the chip)
        self.kval = (0x29, 0x29, 0x29, 0x29)
        self.acc, self.dec, self.maxspd = 500, 500, 800
        self.ocd_th, self.stall_th = 0x08, 0x10
        self.alarms = ["overcurrent", "undervoltage", "thermal_shutdown", "thermal_warning",
                       "command_error"]
        self.fault_action = "HardHiZ"
        self.cm = {"pred": True, "tsw_us": 48, "t_fast": 0x19, "ton_min": 0x29, "toff_min": 0x29}
        self.full_steps = 200
        self.direction = 1
        self.enc_ppr = 1600                   # counts/rev: one count per 1/8 microstep
        self.hb_timeout_ms = 0
        self.last_hb = time.monotonic()
        self.tlm_ms = 0
        self.tlm_compact = False
        self.enc_rate = 1000
        # encoder filter chain as CTRL:ENC:FILT? reports it; each stage adds lag in the sine model
        self.enc_filter = {"meas_window_ms": 40, "sample_rate_hz": 1000, "ema_enabled": True, "ema_alpha": 100,
                           "sma_enabled": True, "sma_window": 8, "pade_enabled": True, "pade_gain": 25,
                           "pade_max_corr": 50, "biquad_enabled": True, "biquad_cutoff_hz": 10,
                           "notch_enabled": False, "notch_center_hz": 25, "notch_q10": 50,
                           "holt_enabled": False, "holt_alpha": 51, "holt_beta": 13}
        self.sysid_sine = None                     # (base_sps, amp_sps, f_hz, dur_ms) of the last SINE
        self.idle_release_ms = 0
        # DIAG:MEM? as the release build reports it: bytes per region, then (words) per task
        self.mem = {"flash": (288252, 512 * 1024), "ram": (82892, 96 * 1024),
                    "stack_pool": (3968 * 4, 4096 * 4), "msp": (612, 1024)}
        self.mem_tasks = [("Motor", 1024, 412), ("Comms", 2048, 1310), ("Display", 640, 150),
                          ("Encoder", 256, 96), ("IDLE", 128, 94)]
        self._cap_frozen = False
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
                                                                        self.protocol),
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
                      "busy": self.speed != 0 or self.target is not None, "hi_z": self.hi_z, "direction": self.direction,
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
        } if self.protocol < 3 else self._status_p3()

    def _status_p3(self) -> Dict[str, Any]:
        """Protocol 3 adds motor.faults, motor.driver_raw, control.base_fsps / final_fsps."""
        p, self.protocol = self.protocol, 2
        try:
            st = self.status()
        finally:
            self.protocol = p
        st["motor"].update({"faults": 0, "driver_raw": 0x7E03})
        st["control"].update({"base_fsps": 0.0, "final_fsps": 0.0})
        return st

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
        if self.protocol < 3 and name in self.PROTOCOL_3_ONLY:
            self._err(name, "UNKNOWN_CMD", "Unknown DRV command")
            return
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
                                  "serial": self.serial, "fw": self.FW, "proto": self.protocol})
        else:
            self._ok(name, "SMC,NUCLEO-F401RE-IHM03A1,%s,%s,%d" % (self.serial, self.FW,
                                                                  self.protocol))

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

    def _c_DIAG_MEM_Q(self, name, args):
        lines = ["%s %s=%d total=%d" % (k, "static" if k == "ram" else "used", u, t)
                 for k, (u, t) in self.mem.items()]
        lines += ["task %s stack=%d hwm=%d" % t for t in self.mem_tasks]
        self._lines("MEM", lines)

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
        self.move_usteps_per_s = max(sps, 1) * (1 << self._eff_step_mode())
        self.target = target
        self.hi_z = False

    def _eff_step_mode(self) -> int:
        """The resolution in force: in current mode the powerSTEP01 runs 1/16 at most."""
        return min(self.step_mode, 4) if self.drive_current else self.step_mode

    def _usteps_per_rev(self) -> int:
        return self.full_steps * (1 << self._eff_step_mode())

    def _enc_count(self) -> int:
        """The encoder follows the motor: enc_ppr counts per revolution."""
        return round(self.position * self.enc_ppr / self._usteps_per_rev())

    def _c_MOT_GOTO(self, name, args):
        if self._motion_allowed(name):
            self._start_move(int(float(args[0])), args[2] if "REV" in args else (args[1] if len(args) > 1 else None))
            self._ok(name)

    def _c_MOT_RUN(self, name, args):
        if self._motion_allowed(name):
            if len(args) > 1 and args[1].upper() == "RPM":
                # MOT:RUN <rpm> RPM <dir>: full steps per second from the revolutions
                self.speed = int(round(float(args[0]) * self.full_steps / 60.0))
                self.direction = int(args[2]) if len(args) > 2 else 1
            else:
                self.speed = int(float(args[0]))
                self.direction = int(args[1]) if len(args) > 1 else 1
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
        self._ok(name, data={"step_mode": self._eff_step_mode(), "microsteps": 1 << self._eff_step_mode(),
                             "drive_mode": "current" if self.drive_current else "voltage"})

    def _c_DRV_FULL_STEPS_Q(self, name, args):
        self._ok(name, data={"full_steps_per_rev": self.full_steps})

    def _c_DRV_ENC_PPR_Q(self, name, args):
        self._ok(name, data={"encoder_ppr": self.enc_ppr})

    def _c_DRV_CFG_Q(self, name, args):
        a = set(self.alarms)
        self._lines("MCONFIG", [
            "Motor Config (defaults)", "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X" % self.kval,
            "Drive: %s" % ("CURRENT" if self.drive_current else "VOLTAGE"),
            "TVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X" % self.tval,
            "CM: PRED=%d TSW=%dus T_FAST=%02X TON_MIN=%02X TOFF_MIN=%02X" % (
                self.cm["pred"], self.cm["tsw_us"], self.cm["t_fast"], self.cm["ton_min"], self.cm["toff_min"]),
            "OCD_TH=%02X STALL_TH=%02X" % (self.ocd_th, self.stall_th),
            "ACC=%d DEC=%d MAXSPD=%d MINSPD=0 FS_SPD=1023" % (self.acc, self.dec, self.maxspd),
            "Faults: OCD=%d TH_SD=%d TH_W=%d UVLO=%d STALL_A=%d STALL_B=%d CMD=%d" % tuple(
                int(n in a) for n in ("overcurrent", "thermal_shutdown", "thermal_warning",
                                      "undervoltage", "stall", "stall", "command_error")),
            "Action: %s" % self.fault_action, "StepMode: %d (1/%d)" % (self.step_mode, 1 << self.step_mode),
            "IdleRelease: %d ms" % self.idle_release_ms])

    def _c_DBG_MOTOR(self, name, args):
        self._lines("MOTOR_DEBUG", [
            "STATUS=7E03 (%s Idle)" % ("HiZ" if self.hi_z else "Active"),
            "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X" % self.kval,
            "ACC=%d DEC=%d MAXSPD=%d POS=%d" % (self.acc, self.dec, self.maxspd, self.position),
            "ADC_OUT=2 CONFIG=%04X T_FAST=%02X" % (
                (0x8000 if self.cm["pred"] else 0) | ((self.cm["tsw_us"] // 4 - 1) << 10) | 0x088, self.cm["t_fast"])])

    def _c_SYST_VER_Q(self, name, args):
        self._lines("VERSION", ["Stepper Motor Controller v%s" % self.FW, "Build: sim",
                                "Protocol: %d (boot %08X)" % (self.protocol, self.boot),
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

    def _c_UI_MODE(self, name, args):
        if args:
            self.ui_mode = args[0].upper()
        self._ok(name, getattr(self, "ui_mode", "LOCAL"))

    def _c_UI_MODE_Q(self, name, args):
        self._ok(name, getattr(self, "ui_mode", "LOCAL"))

    def _c_UI_LCD_REG(self, name, args):
        reg = int(args[0], 16) if args else -1
        if len(args) < 2 or reg not in (0xB2, 0xB7, 0xBB, 0xC0, 0xC2, 0xC3, 0xC4, 0xC6, 0xD0, 0xE0, 0xE1):
            self._err(name, "INVALID_PARAM", "Register not tunable")
            return
        self.panel_regs = getattr(self, "panel_regs", {})
        self.panel_regs[reg] = [int(a, 16) for a in args[1:]]
        self._ok(name, "")

    def _c_UI_DISP_INDICATOR(self, name, args):
        if getattr(self, "ui_mode", "LOCAL") != "REMOTE":
            self._err(name, "ERROR", "Not in REMOTE mode")          # as the firmware does
            return
        if len(args) < 3:
            self._err(name, "INVALID_PARAM", "Usage: UI:DISP:INDICATOR <angle> <rot_dir> <has_trans> [speed_pct] [rot_pct]")
            return
        pct = [int(a) for a in args[3:5]] + [100, 100]
        if not (0 <= pct[0] <= 100 and 0 <= pct[1] <= 100):
            self._err(name, "INVALID_PARAM", "Invalid percent")
            return
        self.indicator = (int(args[0]), int(args[1]), args[2] not in ("0", "false"), pct[0], pct[1])
        self._ok(name, "")

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

    def _c_DRV_CFG_KVAL(self, name, args):
        self.kval = tuple(int(a, 16) & 0xFF for a in args[:4])
        self._ok(name, "KVAL set")

    def _c_DRV_CFG_MOTION(self, name, args):
        self.acc, self.dec, self.maxspd = (int(a) for a in args[:3])
        self._ok(name, "Motion params set")

    def _c_DRV_CFG_OCD(self, name, args):
        self.ocd_th = int(args[0]) & 0x1F
        self._ok(name, "OCD threshold set: %d (not saved)" % self.ocd_th)

    def _c_DRV_CFG_STALL(self, name, args):
        self.stall_th = int(args[0]) & 0x1F
        self._ok(name, "Stall threshold set: %d (not saved)" % self.stall_th)

    def _c_DRV_CFG_FAULT(self, name, args):
        if args and args[0].upper() == "ACTION":
            self.fault_action = ("HardStop", "HardHiZ", "SoftStop")[int(args[1])]
            self._ok(name, "Fault action set")
            return
        f = [a != "0" for a in args[:7]]
        on = {n for n, x in zip(("overcurrent", "thermal_shutdown", "thermal_warning",
                                 "undervoltage", "stall", "stall", "command_error"), f) if x}
        self.alarms = [n for n in self.FAULTS if n in on]
        self._ok(name, "Fault enables set (not saved)")

    # --- protocol 3: the driver in physical units (a powerSTEP01's grid and limits) -------
    FAULTS = ("overcurrent", "undervoltage", "thermal_shutdown", "thermal_warning", "stall",
              "command_error")
    CAPS = {"voltage_mode": True, "current_mode": True, "current_settable": True,
            "max_microsteps_voltage": 128,
            "max_microsteps_current": 16, "stall_detect": True, "standby_pin": True,
            "hardware_positioning": True, "sync_pulses_per_full_step": 8,
            "fault_bits": list(FAULTS), "faults_reported": list(FAULTS), "faults_alarmable": list(FAULTS),
            "max_current_a": 9.906, "current_step_a": 0.078, "duty_step": 0.00390625,
            "overcurrent_sense": "voltage", "overcurrent_ohms": 0.0012,
            "overcurrent_min_a": 26.0417, "overcurrent_max_a": 833.333, "overcurrent_step_a": 26.0417,
            "stall_step": 0.0322581, "max_speed_fsps": 15625, "max_speed_limit_fsps": 15609.7,
            "speed_limit_step_fsps": 15.2588, "accel_min_fsps2": 14.552, "accel_max_fsps2": 59590.4,
            "accel_step_fsps2": 14.552, "min_position_usteps": -2097152, "max_position_usteps": 2097151}
    PROTOCOL_3_ONLY = {"DRV:CAPS?", "DRV:CFG:PHYS?", "DRV:USTEP", "DRV:USTEP?"} | {
        "DRV:CFG:%s%s" % (s, q) for s in ("CURRENT", "DUTY", "USTEP", "ACCEL", "DECEL", "MAXSPD",
                                          "OVERCURRENT", "STALL_TH", "ALARMS") for q in ("", "?")}
    NOT_SAVED = "(not saved; DRV:CFG:APPLY puts it in force)"

    @staticmethod
    def _g(v: float) -> float:
        return float("%.6g" % v)

    def _phys(self) -> Dict[str, Any]:
        """The settings as DRV:CFG:PHYS? reports them (codes back to physical units)."""
        d: Dict[str, Any] = {"source": "defaults",
                             "drive_mode": "current" if self.drive_current else "voltage",
                             "microsteps_asked": 1 << self.step_mode,
                             "microsteps": 1 << self._eff_step_mode()}
        for k, c in zip(("hold_a", "run_a", "acc_a", "dec_a"), self.tval):
            d[k] = self._g(c * 0.078)
        for k, c in zip(("hold_duty", "run_duty", "acc_duty", "dec_duty"), self.kval):
            d[k] = self._g(c / 256.0)
        d.update({"accel_fsps2": self._g(self.acc * 14.552), "decel_fsps2": self._g(self.dec * 14.552),
                  "max_speed_fsps": self._g(self.maxspd * 15625.0 / 1024.0),
                  "overcurrent_a": self._g((self.ocd_th + 1) * 0.03125 / 0.0012),
                  "overcurrent_v": self._g((self.ocd_th + 1) * 0.03125),
                  "stall_threshold": self._g(self.stall_th / 31.0),
                  "alarms": list(self.alarms), "fault_action": self.fault_action,
                  "idle_release_ms": self.idle_release_ms, "full_steps_per_rev": self.full_steps})
        return d

    @staticmethod
    def _txt(v) -> str:
        if isinstance(v, bool):
            return "1" if v else "0"
        if isinstance(v, list):
            return ",".join(v) if v else "NONE"
        return str(v)

    def _kv(self, name: str, what: str, d: Dict[str, Any], note: str = "") -> None:
        """A named-value reply: one JSON object, or `OK <WHAT> key=value ... [note]`."""
        if self.json:
            self._reply(json.dumps({"status": "ok", "command": name, "data": d}, separators=(",", ":")))
            return
        words = ["OK", what] + ["%s=%s" % (k, self._txt(v)) for k, v in d.items()]
        self._reply(" ".join(words + ([note] if note else [])))

    def _kv_lines(self, name: str, d: Dict[str, Any]) -> None:
        if self.json:
            self._reply(json.dumps({"status": "ok", "command": name, "data": d}, separators=(",", ":")))
            return
        for k, v in d.items():
            self._reply("%s=%s" % (k, self._txt(v)))
        self._reply("OK %d" % len(d))

    _GROUPS = {"CURRENT": ("hold_a", "run_a", "acc_a", "dec_a", "drive_mode"),
               "DUTY": ("hold_duty", "run_duty", "acc_duty", "dec_duty", "drive_mode"),
               "USTEP": ("microsteps_asked", "microsteps", "drive_mode"),
               "ACCEL": ("accel_fsps2",), "DECEL": ("decel_fsps2",), "MAXSPD": ("max_speed_fsps",),
               "OVERCURRENT": ("overcurrent_a", "overcurrent_v"), "STALL_TH": ("stall_threshold",),
               "ALARMS": ("alarms",)}

    def _group(self, name: str, what: str, changed: bool) -> None:
        p = self._phys()
        self._kv(name, what, {k: p[k] for k in self._GROUPS[what]}, self.NOT_SAVED if changed else "")

    def _setter(self, name, args, what, n, usage, check, store):
        """A physical setter, or its query when the name ends with '?'."""
        if name.endswith("?"):
            self._group(name, what, False)
            return
        try:
            v = [float(a) for a in args[:n]]
        except ValueError:
            v = []
        if len(v) < n:
            self._err(name, "USAGE", "Usage: " + usage)
            return
        bad = check(v)
        if bad:
            self._err(name, "OUT_OF_RANGE", ("%s must be %s-%s %s" % bad).strip()
                      if len(bad) == 4 else "%s must be %s-%s" % bad)
            return
        store(v)
        self._group(name, what, True)

    def _c_DRV_CAPS_Q(self, name, args):
        self._kv_lines(name, dict(self.CAPS))

    def _c_DRV_CFG_PHYS_Q(self, name, args):
        self._kv_lines(name, self._phys())

    def _c_DRV_CFG_CURRENT(self, name, args):
        self._setter(name, args, "CURRENT", 4, "DRV:CFG:CURRENT <hold_A> <run_A> <acc_A> <dec_A>",
                     lambda v: ("current", 0, 9.906, "A") if any(x < 0 or x > 9.945 for x in v) else None,
                     lambda v: setattr(self, "tval", tuple(min(127, int(x / 0.078 + 0.5)) for x in v)))
    _c_DRV_CFG_CURRENT_Q = _c_DRV_CFG_CURRENT

    def _c_DRV_CFG_DUTY(self, name, args):
        self._setter(name, args, "DUTY", 4, "DRV:CFG:DUTY <hold> <run> <acc> <dec> (0..1)",
                     lambda v: ("duty", 0, 1) if any(x < 0 or x > 1 for x in v) else None,
                     lambda v: setattr(self, "kval", tuple(min(255, int(x * 256 + 0.5)) for x in v)))
    _c_DRV_CFG_DUTY_Q = _c_DRV_CFG_DUTY

    def _c_DRV_CFG_USTEP(self, name, args):
        def check(v):
            n = int(v[0])
            ok = n == v[0] and 1 <= n <= 128 and not n & (n - 1)
            return None if ok else ("microsteps (a power of two)", 1, 128)
        self._setter(name, args, "USTEP", 1, "DRV:CFG:USTEP <microsteps 1|2|4|...>", check,
                     lambda v: setattr(self, "step_mode", int(v[0]).bit_length() - 1))
    _c_DRV_CFG_USTEP_Q = _c_DRV_CFG_USTEP

    def _ramp(self, name, args, what, attr):
        self._setter(name, args, what, 1, "DRV:CFG:%s <full_steps/s^2>" % what,
                     lambda v: None if 7.27 < v[0] <= 59597 else (what.lower(), 14.552, 59590.4, "full steps/s^2"),
                     lambda v: setattr(self, attr, max(1, min(4095, int(v[0] * 1000 + 7) // 14552))))

    def _c_DRV_CFG_ACCEL(self, name, args): self._ramp(name, args, "ACCEL", "acc")
    def _c_DRV_CFG_DECEL(self, name, args): self._ramp(name, args, "DECEL", "dec")
    _c_DRV_CFG_ACCEL_Q = _c_DRV_CFG_ACCEL
    _c_DRV_CFG_DECEL_Q = _c_DRV_CFG_DECEL

    def _c_DRV_CFG_MAXSPD(self, name, args):
        self._setter(name, args, "MAXSPD", 1, "DRV:CFG:MAXSPD <full_steps/s>",
                     lambda v: None if 7.6 < v[0] <= 15617.4 else ("max speed", 15.2588, 15609.7, "full steps/s"),
                     lambda v: setattr(self, "maxspd", max(1, min(1023, int(v[0] * 1024) // 15625))))
    _c_DRV_CFG_MAXSPD_Q = _c_DRV_CFG_MAXSPD

    def _c_DRV_CFG_OVERCURRENT(self, name, args):
        self._setter(name, args, "OVERCURRENT", 1, "DRV:CFG:OVERCURRENT <amps>",
                     lambda v: None if 13.02 <= v[0] <= 846.36 else ("overcurrent", 26.0417, 833.333, "A"),
                     lambda v: setattr(self, "ocd_th", max(0, min(31, int(v[0] / 26.041667 + 0.5) - 1))))
    _c_DRV_CFG_OVERCURRENT_Q = _c_DRV_CFG_OVERCURRENT

    def _c_DRV_CFG_STALL_TH(self, name, args):
        self._setter(name, args, "STALL_TH", 1, "DRV:CFG:STALL_TH <0..1>",
                     lambda v: None if 0 <= v[0] <= 1 else ("stall threshold", 0, 1),
                     lambda v: setattr(self, "stall_th", min(31, int(v[0] * 31 + 0.5))))
    _c_DRV_CFG_STALL_TH_Q = _c_DRV_CFG_STALL_TH

    def _c_DRV_CFG_ALARMS(self, name, args):
        if name.endswith("?"):
            self._group(name, "ALARMS", False)
            return
        if not args:
            self._err(name, "USAGE", "Usage: DRV:CFG:ALARMS <fault> [fault...] | NONE | ALL")
            return
        got = set()
        for a in args:
            a = a.lower()
            if a == "none":
                continue
            if a == "all":
                got |= set(self.FAULTS)
            elif a in self.FAULTS:
                got.add(a)
            else:
                self._err(name, "INVALID_PARAM", "unknown or not alarmable fault (DRV:CAPS? faults_alarmable)")
                return
        self.alarms = [f for f in self.FAULTS if f in got]
        self._group(name, "ALARMS", True)
    _c_DRV_CFG_ALARMS_Q = _c_DRV_CFG_ALARMS

    def _c_DRV_USTEP(self, name, args):
        try:
            n = int(args[0])
        except (ValueError, IndexError):
            self._err(name, "USAGE", "Usage: DRV:USTEP <microsteps 1|2|4|...>")
            return
        if not 1 <= n <= 128 or n & (n - 1):
            self._err(name, "OUT_OF_RANGE", "microsteps (a power of two) must be 1-128")
            return
        self.step_mode = n.bit_length() - 1
        self._kv(name, "USTEP", {"microsteps_asked": n, "microsteps": 1 << self._eff_step_mode(),
                                 "microsteps_per_rev": self._usteps_per_rev()})

    def _c_DRV_USTEP_Q(self, name, args):
        self._kv(name, "USTEP", {"microsteps": 1 << self._eff_step_mode(),
                                 "microsteps_per_rev": self._usteps_per_rev(),
                                 "drive_mode": "current" if self.drive_current else "voltage"})

    def _c_CTRL_ENC_FILT_RATE(self, name, args):
        self.enc_rate = max(100, min(10000, int(args[0])))
        self.enc_filter["sample_rate_hz"] = self.enc_rate
        self._ok("ENC_FILT", "Sample rate %d Hz" % self.enc_rate)

    def _c_CTRL_ENC_FILT_Q(self, name, args):
        self._ok("ENC_FILT", data=dict(self.enc_filter))

    def _filt_stage(self, name, args, key, *fields):
        on = args[0] in ("1", "ON", "on") if args else True
        self.enc_filter[key + "_enabled"] = on
        for f, a in zip(fields, args[1:]):
            self.enc_filter[f] = int(a)
        self._ok("ENC_FILT", "%s %s" % (key, "on" if on else "off"))

    def _c_CTRL_ENC_FILT_EMA(self, name, args): self._filt_stage(name, args, "ema", "ema_alpha")
    def _c_CTRL_ENC_FILT_SMA(self, name, args): self._filt_stage(name, args, "sma", "sma_window")
    def _c_CTRL_ENC_FILT_PADE(self, name, args): self._filt_stage(name, args, "pade", "pade_gain", "pade_max_corr")
    def _c_CTRL_ENC_FILT_BIQUAD(self, name, args): self._filt_stage(name, args, "biquad", "biquad_cutoff_hz")
    def _c_CTRL_ENC_FILT_NOTCH(self, name, args): self._filt_stage(name, args, "notch", "notch_center_hz", "notch_q10")
    def _c_CTRL_ENC_FILT_HOLT(self, name, args): self._filt_stage(name, args, "holt", "holt_alpha", "holt_beta")

    def _c_CTRL_ENC_FILT_WINDOW(self, name, args):
        self.enc_filter["meas_window_ms"] = int(args[0])
        self._ok("ENC_FILT", "window %s ms" % args[0])

    def _filter_lag_ms(self) -> float:
        """The fake's lag model: the window plus a fixed cost per enabled stage."""
        f = self.enc_filter
        lag = f["meas_window_ms"] / 2.0
        lag += 25.0 if f["ema_enabled"] else 0.0
        lag += 15.0 if f["sma_enabled"] else 0.0
        lag += 10.0 if f["pade_enabled"] else 0.0
        lag += (160.0 / max(1, f["biquad_cutoff_hz"])) if f["biquad_enabled"] else 0.0
        lag += 8.0 if f["holt_enabled"] else 0.0
        return lag

    CAP_SAMPLES = 4096

    def _c_CTRL_CAP_FREEZE(self, name, args):
        self._cap_frozen = True
        self._ok("CAP_FREEZE", data={"samples": self.CAP_SAMPLES, "rate_hz": self.enc_rate,
                                     "sync": True, "pulses_per_full_step": 8})

    def _c_CTRL_CAP_DATA_Q(self, name, args):
        if not self._cap_frozen:
            self._err("CAP_DATA", "INVALID_STATE", "No frozen capture at that offset (CTRL:CAP:FREEZE first)")
            return
        import math
        offset = int(args[0]) if args else 0
        count = min(int(args[1]) if len(args) > 1 else 200, 256, self.CAP_SAMPLES - offset)
        # a 60 rpm cruise (4000 counts/s) with a 120 Hz, 4-count oscillation on the encoder
        cpp = self.enc_ppr / (self.full_steps * 8.0)       # counts per SYNC pulse, as reported
        lines = []
        for i in range(offset, offset + count):
            t = i / self.enc_rate
            sync = int(round(t * self.full_steps * 8))               # 8 pulses per full step, 1 rev/s
            enc = int(round(sync * cpp + 4.0 * math.sin(2 * math.pi * 120.0 * t)))
            lines.append("%d,%d" % (enc & 0xFFFF, sync & 0xFFFF))
        self._lines("CAP_DATA", lines, extra={"offset": offset})

    def _c_CTRL_CAP_RESUME(self, name, args):
        self._cap_frozen = False
        self._ok("CAP_RESUME", "Sampling resumed")

    def _c_DRV_CFG_IDLE(self, name, args):
        self.idle_release_ms = max(0, min(60000, int(args[0])))
        self._ok(name, "Idle release after %d ms stopped (not saved)" % self.idle_release_ms)

    def _c_DRV_CFG_PRED(self, name, args):
        self.cm["pred"] = args[0].upper() in ("ON", "1")
        self.cm["tsw_us"] = int(args[1]) if len(args) > 1 else 48
        self._ok(name, "PRED set")

    def _c_DRV_CFG_DECAY(self, name, args):
        tf, ton, toff = (int(a, 16) for a in args[:3])
        self.cm["t_fast"] = tf or 0x19
        self.cm["ton_min"] = (ton & 0x7F) or 0x29
        self.cm["toff_min"] = (toff & 0x7F) or 0x29
        self._ok(name, "DECAY set")

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

    _c_CTRL_SYSID_STEP = _c_CTRL_SYSID_RAMP = _sysid_start
    _c_CTRL_SYSID_TRAPEZOID = _c_CTRL_SYSID_RECT = _sysid_start

    def _c_CTRL_SYSID_SINE(self, name, args):
        if not self._motion_allowed(name):
            return
        base, amp, f_mhz = int(args[0]), int(args[1]), int(args[2])
        dur = int(args[4]) if len(args) > 4 else 10000
        self.sysid_sine = (base, amp, f_mhz / 1000.0, dur)
        self.sysid_phase, self.sysid_done_at = 2, time.monotonic() + self.sysid_run_s
        self._ok(name, "SYSID started")

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
        if self.sysid_sine is not None:
            # a sine test: setpoint around the baseline, the reported velocity 0.95x and lagging
            # by the filter model, in encoder ticks/s (cpr / full steps per full step)
            import math
            base, amp, f_hz, dur = self.sysid_sine
            tps = self.enc_ppr / float(self.full_steps)
            dt = dur / 1000.0 / float(total)            # seconds per sample
            lag_s = self._filter_lag_ms() / 1000.0
            rows = []
            for i in range(offset, offset + count):
                t = i * dt
                sp = (base + amp * math.sin(2 * math.pi * f_hz * t)) * tps
                ac = (base + 0.95 * amp * math.sin(2 * math.pi * f_hz * (t - lag_s))) * tps
                ac += 3.0 * math.sin(37.0 * t) * (0.5 if self.enc_filter["biquad_enabled"] else 1.0)
                rows.append("%d,%d,%d,%d" % (int(round(t * 1000)), int(round(sp)), int(round(ac)), 0))
        else:
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
