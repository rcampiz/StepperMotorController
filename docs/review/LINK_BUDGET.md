# Link budget: polled status vs pushed telemetry, and the watchdog default (S-5)

Context: CLIENT_REVIEW changes B asked to replace the 100 ms `DIAG:STAT?` poll with pushed
telemetry *before* lowering the default heartbeat timeout, and to report the link budget
before and after. Computed 2026-10-06 from the firmware's own format strings
(`renderStatusJson`, the heartbeat ack) with typical values for a running board. Link:
USART2 over the ST-LINK VCP at 115200 baud 8N1 = 11,520 bytes/s per direction.

## Line sizes

| Line | Bytes (incl. CR LF) |
|---|---|
| `DIAG:STAT?` reply (JSON) | 920 |
| Telemetry push `{"kind":"telemetry",...}` (same `data` object) | 902 |
| `SYST:HB <n>` request | 15 |
| Heartbeat ack (JSON, with `boot`) | 117 |

## Before and after

"Command slot" is the share of time the single in-flight request slot is occupied. While it
is busy, every other command, including the confirmation of an ESTOP, waits.

| | Before: protocol 1, old GUI | After the refactor, old rates | After, new defaults |
|---|---|---|---|
| Status | polled, 10 Hz | pushed, 10 Hz | pushed, 5 Hz |
| Heartbeat | 20 Hz, echoed | 20 Hz, no echo | 10 Hz, no echo |
| Host to board | 420 B/s (3.6 %) | 300 B/s (2.6 %) | 150 B/s (1.3 %) |
| Board to host | 11,960 B/s (104 %, oversubscribed) | 11,360 B/s (99 %) | 5,680 B/s (49 %) |
| Command slot busy | 107 % (poll 82 % + heartbeat 26 %) | 23 % | 11 % |
| Longest wait for a free slot | 82 ms | 11 ms | 11 ms |

Reading it:
- Polling was the main problem: the status poll alone held the command slot 82 % of the
  time, and the receive direction was oversubscribed, so replies queued indefinitely.
- Pushing the same 900-byte object at 10 Hz frees the command slot but still fills the
  board-to-host direction (99 %): replies and events queue behind telemetry lines, and the
  firmware's Comms task spends most of its time transmitting.
- The new defaults (status every 200 ms, heartbeat every 100 ms) bring the receive
  direction to about half the link. Both are visible settings in the GUI connection bar
  and `--tlm-ms` on the CLI. A compact telemetry line (a few fields, about 80 bytes) would
  allow 10 Hz again for under 10 % of the link; that is a firmware/protocol proposal, not
  part of this change.

## Watchdog default (S-5)

New default: **500 ms** (was `max(2000, 6 x period)` = 2000 ms), with heartbeats every
100 ms (five periods inside the timeout). Constraints checked:

| Blocking operation | Duration | Handling |
|---|---|---|
| One heartbeat round trip | ~11 ms on the wire | fine |
| Image band (`UI:DISP:BITMAP:RLE`) | 20 rows: ~0.84 s | bands cut to 5 rows: at most ~2.4 KB, ~0.21 s |
| Flash-writing commands: `DRV:CFG:APPLY`, `DRV:CFG:SAVE`, `DRV:CFG:RESET`, `CTRL:TRIM:SAVE`, `CTRL:FOLLOW:SAVE`, `CTRL:ENC:FILT:SAVE`, `DEV:ID`, `DEV:ROLE` | motor config: erase of internal sector 7 (128 KB), roughly 1 s typical, 2 s maximum per ST's figures | the Board widens the watchdog to 5000 ms (the firmware maximum) around these commands and restores it afterwards; never disarmed. The list is generated from the firmware source (`persists` in protocol.json). |
| `UI:FLASH:UPLOAD` / `UI:FLASH:ERASE_ALL` (external NOR) | up to ~13 s / 60 s | longer than the firmware maximum: unchanged A3 rule, allowed only while idle in Hi-Z; the expected comms-timeout stop is cleared afterwards |

## Firmware findings from this analysis (reported, not changed)

1. **`DEV:ID` and `DEV:ROLE` crash the board.** `Services::g_deviceConfig.init()` is never
   called (the call was commented out in `f874dd5`, February, and later removed), so
   `DeviceConfigManager::setDeviceId/setRole` call `m_lock->acquire()` on a null pointer
   (HardFault on the MCU); `saveToFlash()` would also find no flash. The getters do not
   lock, so `DEV:ID?`, `DEV:ROLE?` and `SYST:VER?` work and report 0 / NONE. This blocks
   per-corner identity for the four-board base.
2. **Every config apply stalls the MCU for about a second.** `DRV:CFG:APPLY` (sent by the GUI
   on every KVAL, motion, protection and step-mode apply) saves to internal flash sector 7,
   a 128 KB sector. The erase routine runs from flash, so instruction fetch should stall
   for the whole erase (expected on this single-bank part, not yet measured): the Motor task,
   the Comms task and flash-resident interrupt handlers are frozen, including ESTOP
   handling. Proposal: keep `APPLY` in RAM and save only on `DRV:CFG:SAVE`; move the config
   to a 16 KB sector (sectors 1 to 3, erase about 250 ms) or to the external NOR; run the
   erase from RAM. Measure on the bench first (Phase 4 Step 0 can time one `DRV:CFG:SAVE`).
   **Resolved 2026-10-17:** the store is an append-only journal in sector 7
   (`InternalFlashJournalStore`); a save programs 64 bytes and never erases. The sector
   erase runs at boot when fewer than 16 saves remain, or at run time only with the motor
   stopped (`STORE_FULL` otherwise). The client's watchdog widening stays as a belt for
   that rare run-time erase.
3. **Upload handlers can drop the first payload byte.** The CR/LF drain loop after
   `OK READY` (`display_handlers.cpp`) reads and discards the first byte that is not CR/LF
   if it has already arrived. Harmless today because clients wait for `OK READY` before
   sending; it becomes reachable with faster links or pipelining.
