# Client Python Changes (For Review)

This file documents changes made to the Python client and the rationale, so another agent can review and discriminate the edits.

## Files Modified

- `client/src/protocol/client.py`

## Changes Made

1) Added binary bitmap streaming support
- **What:** Added `MotorClient.disp_bitmap()` to implement the `DISP_BITMAP` protocol (READY handshake, raw RGB565 streaming, final OK/ERROR).
- **Why:** The MCU now supports binary bitmap streaming in `cmdDispBitmap()`, so the client must handle the ASCII `OK READY <bytes>` handshake and send raw bytes rather than base64.

2) Parsed `OK READY <bytes>` response
- **What:** Extended `_parse_ascii_response()` to detect `OK READY <bytes>` and expose `ready_bytes` in the response data.
- **Why:** The `DISP_BITMAP` flow requires the client to know the expected byte count before streaming.

3) Stream chunking control
- **What:** Added `DEFAULT_STREAM_CHUNK` and used it in `disp_bitmap()` to send data in bounded chunks.
- **Why:** Prevents large single writes that can overflow UART/RTT buffers.

## Behavioral Notes

- `DISP_BITMAP` READY response is ASCII even if JSON mode is enabled; the client accepts ASCII for the READY handshake, then resumes normal parsing for the final OK/ERROR.
- `disp_bitmap()` validates byte count exactly and raises `ProtocolError` if the provided data length mismatches the expected size.

## Follow-Up Suggestions

- If RTT is used, consider larger chunk sizes; if VCP is used, keep chunk sizes small.
- Consider adding client-side range validation mirroring MCU limits for speed/accel/position to fail fast before sending.
