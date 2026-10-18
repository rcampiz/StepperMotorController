"""
Typed command API: every command in docs/PROTOCOL.md as a function that returns a Request.

Frontends never format a command string (CLIENT_REVIEW section 9, rule 1): they call
these builders and hand the Request to a Board. Builders are pure; they validate the
command name against the generated catalogue (protocol_spec.py) and the framing
limits (line length, argument count and size, no spaces inside an argument), so a
command the firmware would reject or truncate fails on the host first.

tests/core/test_commands.py enforces that every catalogue command has a builder and
every builder names a catalogue command.
"""

from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Dict, Optional, Sequence, Set, Tuple

from .protocol_spec import COMMANDS, LIMITS


class Reply(Enum):
    SINGLE = "single"     # one reply line
    LINES = "lines"       # multi-line reply (F12): lines + OK <n> / JSON `lines`
    READY = "ready"       # OK READY <n>, raw bytes, then one final reply


@dataclass(frozen=True)
class Request:
    name: str                              # SCPI name, a key of COMMANDS (or a debug command)
    args: Tuple[str, ...] = ()
    reply: Reply = Reply.SINGLE
    payload: Optional[bytes] = None        # bytes sent after OK READY
    crc: bool = False                      # append CRC32 LE after the payload
    timeout_ms: Optional[int] = None       # None: the board's default
    tag: str = ""                          # free label for the frontend's routing

    @property
    def line(self) -> str:
        return " ".join((self.name,) + self.args)

    def tagged(self, tag: str) -> "Request":
        return Request(self.name, self.args, self.reply, self.payload, self.crc,
                       self.timeout_ms, tag)


class CommandError(ValueError):
    """A builder was given arguments the firmware would reject or truncate."""


def _fmt(a) -> str:
    if isinstance(a, bool):
        return "1" if a else "0"
    if isinstance(a, float):
        return ("%.4f" % a).rstrip("0").rstrip(".") if a != int(a) else str(int(a))
    return str(a)


def request(name: str, *args, payload: Optional[bytes] = None, crc: bool = False,
            timeout_ms: Optional[int] = None, debug: bool = False) -> Request:
    """Build a Request for a catalogue command (or, with debug=True, a raw debug command)."""
    spec = COMMANDS.get(name)
    if spec is None and not debug:
        raise CommandError(f"{name} is not in docs/PROTOCOL.md")
    sargs = tuple(_fmt(a) for a in args if a is not None)
    for a in sargs:
        if not a or any(c.isspace() for c in a):
            raise CommandError(f"{name}: argument {a!r} is empty or contains whitespace")
        if len(a) > LIMITS["arg_max_chars"]:
            raise CommandError(f"{name}: argument longer than {LIMITS['arg_max_chars']} chars")
        if any(ord(c) < 0x20 or ord(c) > 0x7E for c in a):
            raise CommandError(f"{name}: argument must be printable ASCII")
    if len(sargs) > LIMITS["max_args"]:
        raise CommandError(f"{name}: more than {LIMITS['max_args']} arguments")
    req = Request(name, sargs,
                  Reply.READY if spec is not None and spec.binary else
                  Reply.LINES if (spec is not None and spec.multiline) or debug else Reply.SINGLE,
                  payload, crc, timeout_ms)
    if len(req.line) > LIMITS["line_max_chars"]:
        raise CommandError(f"{name}: line longer than {LIMITS['line_max_chars']} chars")
    return req


# Registry: builder -> SCPI names it can emit (the coverage test reads this)
BUILDERS: Dict[str, Set[str]] = {}


def builds(*names: str) -> Callable:
    def deco(fn):
        BUILDERS[fn.__name__] = set(names)
        return fn
    return deco


def _dir(direction) -> int:
    d = int(direction)
    if d not in (0, 1):
        raise CommandError("direction must be 0 (reverse) or 1 (forward)")
    return d


# ---------------------------------------------------------------------------
# Session / identity (top level)
# ---------------------------------------------------------------------------
@builds("*IDN?")
def idn() -> Request: return request("*IDN?")

@builds("FMT")
def fmt(json_mode: bool) -> Request: return request("FMT", "JSON" if json_mode else "ASCII")

@builds("FMT?")
def fmt_query() -> Request: return request("FMT?")


# ---------------------------------------------------------------------------
# MOT: motion
# ---------------------------------------------------------------------------
@builds("MOT:MOVE")
def mot_move(steps: int, direction: int, speed: Optional[int] = None) -> Request:
    return request("MOT:MOVE", int(steps), _dir(direction), speed)

@builds("MOT:MOVE")
def mot_move_rev(rev: float, direction: int, speed: Optional[int] = None) -> Request:
    return request("MOT:MOVE", "%.4f" % rev, "REV", _dir(direction), speed)

@builds("MOT:GOTO")
def mot_goto(position: int, speed: Optional[int] = None) -> Request:
    return request("MOT:GOTO", int(position), speed)

@builds("MOT:GOTO")
def mot_goto_rev(rev: float, speed: Optional[int] = None) -> Request:
    return request("MOT:GOTO", "%.4f" % rev, "REV", speed)

@builds("MOT:RUN")
def mot_run(speed_sps: int, direction: int) -> Request:
    return request("MOT:RUN", int(speed_sps), _dir(direction))

@builds("MOT:RUN")
def mot_run_rpm(rpm: float, direction: int) -> Request:
    return request("MOT:RUN", "%.1f" % rpm, "RPM", _dir(direction))

@builds("MOT:STOP")
def mot_stop(hard: bool = False) -> Request:
    return request("MOT:STOP", "hard" if hard else None)

@builds("MOT:HOME")
def mot_home(speed: Optional[int] = None) -> Request: return request("MOT:HOME", speed)

@builds("MOT:CFG:ACCEL")
def mot_cfg_accel(steps_per_s2: int) -> Request: return request("MOT:CFG:ACCEL", int(steps_per_s2))

@builds("MOT:CFG:DECEL")
def mot_cfg_decel(steps_per_s2: int) -> Request: return request("MOT:CFG:DECEL", int(steps_per_s2))

@builds("MOT:CFG:MAXSPD")
def mot_cfg_maxspd(steps_per_s: int) -> Request: return request("MOT:CFG:MAXSPD", int(steps_per_s))

@builds("MOT:EN")
def mot_enable() -> Request: return request("MOT:EN")

@builds("MOT:DIS")
def mot_disable() -> Request: return request("MOT:DIS")

@builds("MOT:ZERO")
def mot_zero() -> Request: return request("MOT:ZERO")


# ---------------------------------------------------------------------------
# SYST: system, safety, events, telemetry
# ---------------------------------------------------------------------------
@builds("SYST:ESTOP")
def syst_estop() -> Request: return request("SYST:ESTOP")

@builds("SYST:FAULT:CLEAR")
def fault_clear() -> Request: return request("SYST:FAULT:CLEAR")

@builds("SYST:FAULT:FORCE")
def fault_force() -> Request: return request("SYST:FAULT:FORCE")

@builds("SYST:FAULT:STAT?")
def fault_status() -> Request: return request("SYST:FAULT:STAT?")

@builds("SYST:HB")
def heartbeat(seq: int) -> Request: return request("SYST:HB", int(seq))

@builds("SYST:HB:TIMEOUT")
def heartbeat_timeout(ms: int) -> Request: return request("SYST:HB:TIMEOUT", int(ms))

@builds("SYST:HB:STAT?")
def heartbeat_status() -> Request: return request("SYST:HB:STAT?")

@builds("SYST:ECHO")
def echo(on: bool) -> Request: return request("SYST:ECHO", "ON" if on else "OFF")

@builds("SYST:ECHO?")
def echo_query() -> Request: return request("SYST:ECHO?")

@builds("SYST:TLM:RATE")
def telemetry_rate(ms: int) -> Request: return request("SYST:TLM:RATE", int(ms))

@builds("SYST:TLM:RATE?")
def telemetry_rate_query() -> Request: return request("SYST:TLM:RATE?")

@builds("SYST:TLM:FMT")
def telemetry_format(compact: bool) -> Request:
    """JSON pushes carry the compact live object (state, motor position/speed/busy/hi_z,
    encoder count/velocity, following error, watchdog remaining) or the full DIAG:STAT? one."""
    return request("SYST:TLM:FMT", "COMPACT" if compact else "FULL")

@builds("SYST:TLM:FMT?")
def telemetry_format_query() -> Request: return request("SYST:TLM:FMT?")

@builds("SYST:BAUD")
def baud(rate: int) -> Request:
    if int(rate) not in (115200, 230400, 460800, 921600):
        raise CommandError("baud must be 115200, 230400, 460800 or 921600")
    return request("SYST:BAUD", int(rate))

@builds("SYST:EVT:EN")
def events_enable(mask: Optional[int] = None) -> Request:
    return request("SYST:EVT:EN", None if mask is None else "0x%02X" % (int(mask) & 0xFF))

@builds("SYST:EVT:DIS")
def events_disable() -> Request: return request("SYST:EVT:DIS")

@builds("SYST:EVT:STAT?")
def events_status() -> Request: return request("SYST:EVT:STAT?")

@builds("SYST:DELAY")
def transport_delay(ms: int) -> Request: return request("SYST:DELAY", int(ms))

@builds("SYST:DELAY?")
def transport_delay_query() -> Request: return request("SYST:DELAY?")

@builds("SYST:TICK?")
def tick() -> Request: return request("SYST:TICK?")

@builds("SYST:VER?")
def version() -> Request: return request("SYST:VER?")

@builds("SYST:HELP?")
def help_lines() -> Request: return request("SYST:HELP?", timeout_ms=5000)

@builds("SYST:ZERO")
def zero_all() -> Request: return request("SYST:ZERO")


# ---------------------------------------------------------------------------
# SYNC: staged queue and synchronised start
# ---------------------------------------------------------------------------
@builds("SYNC:QUEUE")
def queue_move(steps: int, direction: int) -> Request:
    return request("SYNC:QUEUE", "MOVE", int(steps), _dir(direction))

@builds("SYNC:QUEUE")
def queue_goto(position: int) -> Request:
    return request("SYNC:QUEUE", "GOTO", int(position))

@builds("SYNC:QUEUE")
def queue_run(speed_sps: int, direction: int) -> Request:
    return request("SYNC:QUEUE", "RUN", int(speed_sps), _dir(direction))

@builds("SYNC:ARM")
def sync_arm() -> Request: return request("SYNC:ARM")

@builds("SYNC:START")
def sync_start() -> Request: return request("SYNC:START")

@builds("SYNC:START:AT")
def sync_start_at(tick_us: int) -> Request: return request("SYNC:START:AT", int(tick_us))

@builds("SYNC:CLEAR")
def sync_clear() -> Request: return request("SYNC:CLEAR")


# ---------------------------------------------------------------------------
# DIAG:
# ---------------------------------------------------------------------------
@builds("DIAG:PING")
def ping(seq: int = 0) -> Request: return request("DIAG:PING", int(seq))

@builds("DIAG:STAT?")
def status() -> Request: return request("DIAG:STAT?")

@builds("DIAG:TASKS?")
def tasks_query() -> Request: return request("DIAG:TASKS?")   # the LCD Task Monitor table over serial


# ---------------------------------------------------------------------------
# CTRL: control mode, encoder, supervisor, trim, system identification
# ---------------------------------------------------------------------------
CONTROL_MODES = ("OPEN_LOOP", "MONITOR", "SPEED_TRIM", "PID")   # parseControlMode

@builds("CTRL:MODE")
def control_mode(mode: str) -> Request:
    if mode not in CONTROL_MODES:
        raise CommandError(f"control mode must be one of {CONTROL_MODES}")
    return request("CTRL:MODE", mode)

@builds("CTRL:MODE?")
def control_mode_query() -> Request: return request("CTRL:MODE?")

@builds("CTRL:ENC?")
def encoder() -> Request: return request("CTRL:ENC?")

@builds("CTRL:ENC:STAT?")
def encoder_status() -> Request: return request("CTRL:ENC:STAT?")

@builds("CTRL:ENC:ZERO")
def encoder_zero() -> Request: return request("CTRL:ENC:ZERO")

@builds("CTRL:ENC:DBG?")
def encoder_debug() -> Request: return request("CTRL:ENC:DBG?")

@builds("CTRL:ENC:FILT?")
def enc_filter_query() -> Request: return request("CTRL:ENC:FILT?")

@builds("CTRL:ENC:FILT")
def enc_filter_legacy(*args) -> Request:
    """CTRL:ENC:FILT <args...>: the pre-sub-command form, kept for completeness."""
    return request("CTRL:ENC:FILT", *args)

@builds("CTRL:ENC:FILT:WINDOW")
def enc_filter_window(ms: int) -> Request: return request("CTRL:ENC:FILT:WINDOW", int(ms))

@builds("CTRL:ENC:FILT:EMA")
def enc_filter_ema(enable: bool, alpha: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:EMA", bool(enable), alpha)

@builds("CTRL:ENC:FILT:SMA")
def enc_filter_sma(enable: bool, window: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:SMA", bool(enable), window)

@builds("CTRL:ENC:FILT:PADE")
def enc_filter_pade(enable: bool, gain_pct: Optional[int] = None,
                    max_corr: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:PADE", bool(enable), gain_pct, max_corr)

@builds("CTRL:ENC:FILT:BIQUAD")
def enc_filter_biquad(enable: bool, cutoff_hz: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:BIQUAD", bool(enable), cutoff_hz)

@builds("CTRL:ENC:FILT:NOTCH")
def enc_filter_notch(enable: bool, center_hz: Optional[int] = None,
                     q10: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:NOTCH", bool(enable), center_hz, q10)

@builds("CTRL:ENC:FILT:HOLT")
def enc_filter_holt(enable: bool, alpha: Optional[int] = None,
                    beta: Optional[int] = None) -> Request:
    return request("CTRL:ENC:FILT:HOLT", bool(enable), alpha, beta)

@builds("CTRL:ENC:FILT:RATE")
def enc_filter_rate(hz: int) -> Request: return request("CTRL:ENC:FILT:RATE", int(hz))

@builds("CTRL:ENC:FILT:SAVE")
def enc_filter_save() -> Request: return request("CTRL:ENC:FILT:SAVE")

@builds("CTRL:ENC:FILT:RESET")
def enc_filter_reset() -> Request: return request("CTRL:ENC:FILT:RESET")

@builds("CTRL:FOLLOW?")
def follow_status() -> Request: return request("CTRL:FOLLOW?")

@builds("CTRL:FOLLOW:THRESH?")
def follow_thresholds_query() -> Request: return request("CTRL:FOLLOW:THRESH?")

@builds("CTRL:FOLLOW:THRESH")
def follow_thresholds(e_move: int, t_move: int, e_hold: int, t_hold: int,
                      e_hard: int, n_retry: int) -> Request:
    return request("CTRL:FOLLOW:THRESH", int(e_move), int(t_move), int(e_hold), int(t_hold),
                   int(e_hard), int(n_retry))

@builds("CTRL:FOLLOW:SAVE")
def follow_save() -> Request: return request("CTRL:FOLLOW:SAVE")

@builds("CTRL:FOLLOW:CLEAR")
def follow_clear() -> Request: return request("CTRL:FOLLOW:CLEAR")

@builds("CTRL:TRIM?")
def trim_query() -> Request: return request("CTRL:TRIM?")

@builds("CTRL:TRIM:GAINS")
def trim_gains(kp: float, ki: float, decimals: int = 2) -> Request:
    return request("CTRL:TRIM:GAINS", "%.*f" % (decimals, kp), "%.*f" % (decimals, ki))

@builds("CTRL:TRIM:LIMITS")
def trim_limits(output_limit: int, integral_limit: int) -> Request:
    return request("CTRL:TRIM:LIMITS", int(output_limit), int(integral_limit))

@builds("CTRL:TRIM:MAXPCT")
def trim_max_pct(pct: int) -> Request:
    if not 1 <= int(pct) <= 50:
        raise CommandError("trim max percent must be 1..50")
    return request("CTRL:TRIM:MAXPCT", int(pct))

@builds("CTRL:TRIM:SAVE")
def trim_save() -> Request: return request("CTRL:TRIM:SAVE")

@builds("CTRL:TRIM:RESET")
def trim_reset() -> Request: return request("CTRL:TRIM:RESET")

@builds("CTRL:SYSID:STEP")
def sysid_step(speed_sps: int, direction: int, dur_ms: Optional[int] = None,
               settle_ms: Optional[int] = None) -> Request:
    return request("CTRL:SYSID:STEP", int(speed_sps), _dir(direction), dur_ms,
                   settle_ms if dur_ms is not None else None)

@builds("CTRL:SYSID:RAMP")
def sysid_ramp(start_sps: int, end_sps: int, direction: int, dur_ms: Optional[int] = None,
               settle_ms: Optional[int] = None) -> Request:
    return request("CTRL:SYSID:RAMP", int(start_sps), int(end_sps), _dir(direction), dur_ms,
                   settle_ms if dur_ms is not None else None)

@builds("CTRL:SYSID:SINE")
def sysid_sine(baseline_sps: int, amplitude_sps: int, freq_mhz: int, direction: int,
               dur_ms: Optional[int] = None, settle_ms: Optional[int] = None) -> Request:
    return request("CTRL:SYSID:SINE", int(baseline_sps), int(amplitude_sps), int(freq_mhz),
                   _dir(direction), dur_ms, settle_ms if dur_ms is not None else None)

@builds("CTRL:SYSID:TRAPEZOID")
def sysid_trapezoid(speed_sps: int, direction: int, dur_ms: Optional[int] = None,
                    settle_ms: Optional[int] = None) -> Request:
    return request("CTRL:SYSID:TRAPEZOID", int(speed_sps), _dir(direction), dur_ms,
                   settle_ms if dur_ms is not None else None)

@builds("CTRL:SYSID:RECT")
def sysid_rect(speed_sps: int, direction: int, dur_ms: Optional[int] = None,
               settle_ms: Optional[int] = None) -> Request:
    return request("CTRL:SYSID:RECT", int(speed_sps), _dir(direction), dur_ms,
                   settle_ms if dur_ms is not None else None)

@builds("CTRL:SYSID:STATUS?")
def sysid_status() -> Request: return request("CTRL:SYSID:STATUS?")

@builds("CTRL:SYSID:DATA?")
def sysid_data(offset: int = 0, count: int = 20) -> Request:
    return request("CTRL:SYSID:DATA?", int(offset), int(count))

@builds("CTRL:SYSID:ABORT")
def sysid_abort() -> Request: return request("CTRL:SYSID:ABORT")


# ---------------------------------------------------------------------------
# DEV: identity in a multi-board base
# ---------------------------------------------------------------------------
ROLES = ("FL", "FR", "RL", "RR", "NONE")

@builds("DEV:ID")
def device_id(dev_id: int) -> Request: return request("DEV:ID", int(dev_id))

@builds("DEV:ID?")
def device_id_query() -> Request: return request("DEV:ID?")

@builds("DEV:ROLE")
def role(r: str) -> Request:
    if r not in ROLES:
        raise CommandError(f"role must be one of {ROLES}")
    return request("DEV:ROLE", r)

@builds("DEV:ROLE?")
def role_query() -> Request: return request("DEV:ROLE?")


# ---------------------------------------------------------------------------
# UI: display, remote LCD, image flash
# ---------------------------------------------------------------------------
def _hex4(color: int) -> str:
    return "%04X" % (int(color) & 0xFFFF)

@builds("UI:MODE")
def ui_mode(remote: bool) -> Request: return request("UI:MODE", "REMOTE" if remote else "LOCAL")

@builds("UI:MODE?")
def ui_mode_query() -> Request: return request("UI:MODE?")

@builds("UI:DISP:CLEAR")
def disp_clear(color: Optional[int] = None) -> Request:
    return request("UI:DISP:CLEAR", None if color is None else _hex4(color))

@builds("UI:DISP:TEXT")
def disp_text(x: int, y: int, fg: int, bg: int, text: str) -> Request:
    return request("UI:DISP:TEXT", int(x), int(y), _hex4(fg), _hex4(bg), text)

@builds("UI:DISP:RECT")
def disp_rect(x: int, y: int, w: int, h: int, color: int, fill: bool = False) -> Request:
    return request("UI:DISP:RECT", int(x), int(y), int(w), int(h), _hex4(color),
                   1 if fill else None)

@builds("UI:DISP:LINE")
def disp_line(x1: int, y1: int, x2: int, y2: int, color: int) -> Request:
    return request("UI:DISP:LINE", int(x1), int(y1), int(x2), int(y2), _hex4(color))

@builds("UI:DISP:INDICATOR")
def disp_indicator(angle: int, rotation_dir: int, has_translation: bool) -> Request:
    if int(rotation_dir) not in (-1, 0, 1):
        raise CommandError("rotation_dir must be -1, 0 or 1")
    return request("UI:DISP:INDICATOR", int(angle) % 360, int(rotation_dir), bool(has_translation))

@builds("UI:DISP:BITMAP:B64")
def disp_bitmap_b64(x: int, y: int, w: int, h: int, b64: str) -> Request:
    return request("UI:DISP:BITMAP:B64", int(x), int(y), int(w), int(h), b64)

@builds("UI:DISP:BITMAP")
def disp_bitmap(x: int, y: int, w: int, h: int, rgb565: bytes, crc: bool = True) -> Request:
    return request("UI:DISP:BITMAP", int(x), int(y), int(w), int(h), "CRC" if crc else None,
                   payload=bytes(rgb565), crc=crc)

@builds("UI:DISP:BITMAP:RLE")
def disp_bitmap_rle(x: int, y: int, w: int, h: int, rle: bytes, crc: bool = True) -> Request:
    return request("UI:DISP:BITMAP:RLE", int(x), int(y), int(w), int(h), len(rle),
                   "CRC" if crc else None, payload=bytes(rle), crc=crc)

@builds("UI:FLASH:UPLOAD")
def flash_upload(slot: int, rgb565: bytes, crc: bool = True) -> Request:
    return request("UI:FLASH:UPLOAD", int(slot), "CRC" if crc else None,
                   payload=bytes(rgb565), crc=crc, timeout_ms=30000)

@builds("UI:FLASH:UPLOAD:RLE")
def flash_upload_rle(slot: int, rle: bytes, crc: bool = True) -> Request:
    return request("UI:FLASH:UPLOAD:RLE", int(slot), len(rle), "CRC" if crc else None,
                   payload=bytes(rle), crc=crc, timeout_ms=30000)

@builds("UI:FLASH:SHOW")
def flash_show(slot: int) -> Request: return request("UI:FLASH:SHOW", int(slot))

@builds("UI:FLASH:DUMP")
def flash_dump(slot: int, offset: int = 0, length: int = 64) -> Request:
    return request("UI:FLASH:DUMP", int(slot), int(offset), int(length))

@builds("UI:FLASH:INFO")
def flash_info() -> Request: return request("UI:FLASH:INFO")

@builds("UI:FLASH:ERASE_ALL")
def flash_erase_all() -> Request: return request("UI:FLASH:ERASE_ALL", timeout_ms=60000)

@builds("UI:FLASH:TEST")
def flash_test() -> Request: return request("UI:FLASH:TEST", timeout_ms=10000)


# ---------------------------------------------------------------------------
# DBG: bring-up and traces
# ---------------------------------------------------------------------------
@builds("DBG:MOTOR")
def motor_debug() -> Request: return request("DBG:MOTOR")

@builds("DBG:UNLOCK")
def debug_unlock() -> Request: return request("DBG:UNLOCK")

@builds("DBG:LOCK")
def debug_lock() -> Request: return request("DBG:LOCK")

@builds("DBG:TRACE:DUMP")
def trace_dump() -> Request: return request("DBG:TRACE:DUMP")

@builds("DBG:TRACE:RESET")
def trace_reset() -> Request: return request("DBG:TRACE:RESET")

def debug_raw(name: str, *args) -> Request:
    """A flat bring-up command (SPI_STOP, PS01_DIAG, ...): not in the catalogue, DBG:UNLOCK gated."""
    if ":" in name:
        raise CommandError("debug_raw is for flat bring-up commands; use the typed builders")
    return request(name, *args, debug=True)


# ---------------------------------------------------------------------------
# DRV: driver configuration
# ---------------------------------------------------------------------------
@builds("DRV:REINIT")
def driver_reinit() -> Request: return request("DRV:REINIT")

@builds("DRV:CFG?")
def driver_config() -> Request: return request("DRV:CFG?")

@builds("DRV:CFG:KVAL")
def cfg_kval(hold: int, run: int, acc: int, dec: int) -> Request:
    for v in (hold, run, acc, dec):
        if not 0 <= int(v) <= 0xFF:
            raise CommandError("KVAL values are 0x00..0xFF")
    return request("DRV:CFG:KVAL", *("%02X" % int(v) for v in (hold, run, acc, dec)))

@builds("DRV:CFG:DRIVE")
def cfg_drive_mode(current: bool) -> Request:
    """powerSTEP01 drive mode: current mode (TVAL references) or voltage mode (KVAL duty).
    Takes effect on DRV:CFG:APPLY with the motor idle (STEP_MODE's CM_VM bit needs Hi-Z)."""
    return request("DRV:CFG:DRIVE", "CURRENT" if current else "VOLTAGE")

@builds("DRV:CFG:TVAL")
def cfg_tval(hold: int, run: int, acc: int, dec: int) -> Request:
    for v in (hold, run, acc, dec):
        if not 0 <= int(v) <= 0x7F:
            raise CommandError("TVAL values are 0x00..0x7F (78 mA per unit)")
    return request("DRV:CFG:TVAL", *("%02X" % int(v) for v in (hold, run, acc, dec)))

@builds("DRV:CFG:PRED")
def cfg_pred(on: bool, tsw_us: Optional[int] = None) -> Request:
    """Current-mode predictive current control and the target switching period (4..128 us,
    4 us steps; None = chip default 48 us). CONFIG bits: applied by DRV:CFG:APPLY in Hi-Z."""
    if tsw_us is not None and not 4 <= int(tsw_us) <= 128:
        raise CommandError("tsw_us is 4..128")
    return request("DRV:CFG:PRED", "ON" if on else "OFF", None if tsw_us is None else int(tsw_us))

@builds("DRV:CFG:DECAY")
def cfg_decay(t_fast: int, ton_min: int, toff_min: int) -> Request:
    """Current-mode decay timing: T_FAST ([7:4] fast step, [3:0] fast decay, (v+1) x 2 us),
    TON_MIN and TOFF_MIN ((v+1) x 0.5 us). 0 = chip default."""
    return request("DRV:CFG:DECAY", "%02X" % (int(t_fast) & 0xFF), "%02X" % (int(ton_min) & 0x7F),
                   "%02X" % (int(toff_min) & 0x7F))

@builds("DRV:CFG:OCD")
def cfg_ocd(threshold: int) -> Request:
    if not 0 <= int(threshold) <= 31:
        raise CommandError("OCD threshold is 0..31")
    return request("DRV:CFG:OCD", int(threshold))

@builds("DRV:CFG:STALL")
def cfg_stall(threshold: int) -> Request:
    if not 0 <= int(threshold) <= 31:
        raise CommandError("stall threshold is 0..31")
    return request("DRV:CFG:STALL", int(threshold))

@builds("DRV:CFG:FAULT")
def cfg_fault_enables(ocd: bool, th_sd: bool, th_warn: bool, uvlo: bool,
                      stall_a: bool, stall_b: bool, cmd_err: bool) -> Request:
    return request("DRV:CFG:FAULT", *(bool(v) for v in (ocd, th_sd, th_warn, uvlo,
                                                       stall_a, stall_b, cmd_err)))

FAULT_ACTIONS = {"HardStop": 0, "HardHiZ": 1, "SoftStop": 2}

@builds("DRV:CFG:FAULT")
def cfg_fault_action(action: int) -> Request:
    if int(action) not in (0, 1, 2):
        raise CommandError("fault action is 0 (HardStop), 1 (HardHiZ) or 2 (SoftStop)")
    return request("DRV:CFG:FAULT", "ACTION", int(action))

@builds("DRV:CFG:MOTION")
def cfg_motion(acc_raw: int, dec_raw: int, maxspd_raw: int) -> Request:
    return request("DRV:CFG:MOTION", int(acc_raw), int(dec_raw), int(maxspd_raw))

@builds("DRV:CFG:STEPMODE")
def cfg_step_mode(mode: int) -> Request:
    if not 0 <= int(mode) <= 7:
        raise CommandError("step mode register value is 0..7")
    return request("DRV:CFG:STEPMODE", int(mode))

@builds("DRV:CFG:SAVE")
def cfg_save() -> Request: return request("DRV:CFG:SAVE")

@builds("DRV:CFG:LOAD")
def cfg_load() -> Request: return request("DRV:CFG:LOAD")

@builds("DRV:CFG:RESET")
def cfg_factory_reset() -> Request: return request("DRV:CFG:RESET")

@builds("DRV:CFG:APPLY")
def cfg_apply() -> Request: return request("DRV:CFG:APPLY")

@builds("DRV:STEP_MODE")
def step_mode(mode: int) -> Request:
    if not 0 <= int(mode) <= 7:
        raise CommandError("step mode is 0..7")
    return request("DRV:STEP_MODE", int(mode))

@builds("DRV:STEP_MODE?")
def step_mode_query() -> Request: return request("DRV:STEP_MODE?")

@builds("DRV:FULL_STEPS")
def full_steps(steps_per_rev: int) -> Request: return request("DRV:FULL_STEPS", int(steps_per_rev))

@builds("DRV:FULL_STEPS?")
def full_steps_query() -> Request: return request("DRV:FULL_STEPS?")

@builds("DRV:ENC_PPR")
def encoder_ppr(pulses_per_rev: int) -> Request: return request("DRV:ENC_PPR", int(pulses_per_rev))

@builds("DRV:ENC_PPR?")
def encoder_ppr_query() -> Request: return request("DRV:ENC_PPR?")


def covered() -> Set[str]:
    """SCPI names the typed builders can emit."""
    out: Set[str] = set()
    for names in BUILDERS.values():
        out |= names
    return out


def from_line(line: str) -> Request:
    """Parse a typed command line into a Request (raw consoles, and the GUI until its
    panels call the typed builders). Validated like every other Request; binary
    commands need their payload and cannot be typed."""
    parts = line.split()
    if not parts:
        raise CommandError("empty command")
    name = parts[0].upper()
    spec = COMMANDS.get(name)
    if spec is not None and spec.binary:
        raise CommandError(f"{name} carries a binary payload; use its typed builder")
    return request(name, *parts[1:], debug=spec is None)
