# Client-Side Python Concerns (Based on Current Code/Docs)

This document captures client-side concerns implied by the current firmware and documentation.
It is **not** an implementation guide; it is a checklist of risks and constraints to address in the Python client.

## Transport & Framing

- **Mixed ASCII + binary framing:** Commands are line-based ASCII, but binary bitmap streaming (`DISP_BITMAP`) uses raw bytes. The client must switch cleanly between line-mode parsing and binary streaming without leaking bytes between modes.
- **Response format negotiation:** The server now supports `SET_FORMAT <ASCII|JSON>` and `GET_FORMAT`. The client must explicitly select JSON if it requires structured responses and be prepared to parse ASCII until the format is switched.
- **RTT vs VCP differences:** RTT can sustain higher throughput, while UART VCP is buffer-limited and more susceptible to overrun. The client must throttle streaming based on transport.
- **Timeout handling:** The client must enforce timeouts for `READY`/`DONE` handshakes, telemetry gaps, and binary stream completion to avoid deadlocks.

## Display & Image Streaming

- **RGB565 packing:** Images must be pre-packed to RGB565 with correct byte order. The server will not scale or reformat.
- **Exact resolution:** Images must match display dimensions exactly; otherwise the server should reject them.
- **Binary streaming protocol:** `DISP_BITMAP <x> <y> <w> <h>` responds with `OK READY <bytes>`, then expects exactly `<bytes>` raw RGB565 bytes, followed by `OK` on success or `ERROR <reason>` on failure.
- **Base64 limit remains:** `DISP_BITMAP_B64` is limited to 512 decoded bytes; it is suitable for icons only.

## Command & Response Validation

- **JSON response requirement:** System requirements mandate JSON responses. The client should send `SET_FORMAT JSON` and treat any non-JSON response as an error once JSON mode is enabled.
- **Argument validation now enforced:** The server validates ranges for steps, position, speed, accel/decel, and max speed. Clients must stay within bounds or handle explicit errors.
- **Queue full errors:** Several commands return `Queue full` errors. The client should retry or back off rather than assuming success.

## Telemetry & Events

- **Telemetry format:** Periodic telemetry is emitted as a single line with key/value pairs. The client must parse robustly and handle missing fields.
- **GET_STATUS JSON payload:** In JSON mode, GET_STATUS returns nested motor/encoder fields; in ASCII mode it remains line-based. Clients must adapt to format mode.
- **Joystick event forwarding:** In REMOTE UI mode, joystick events are forwarded as `EVENT JOY ...` lines. The client must handle these asynchronously.

## Device Discovery & Port Selection

- **Windows COM ambiguity:** Multiple COM devices may exist; the client should identify the correct port by VID/PID or product string when possible.
- **Linux device churn:** `/dev/ttyACM*` ordering can change. Prefer stable udev rules or explicit selection.

## Reliability & Safety

- **Motion safety is server-enforced:** The client must not assume it can override limits. Expect explicit `ERROR` responses for unsafe commands.
- **Test mode restrictions:** When test mode is active, motion commands may be blocked or restricted. The client must query and respect current mode.
- **UART RX overflow detection:** The server tracks UART RX overflow counts. If the client sees frequent overflow conditions, it must reduce send rate.

## Performance Expectations

- **Full-frame updates are expensive:** Over UART, full-screen updates are slow. Favor partial updates or RTT where possible.
- **Backpressure handling:** The client must throttle streaming to avoid UART overruns and RTT buffer exhaustion.
