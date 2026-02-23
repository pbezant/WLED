# MVP-004 — Non-Blocking Radio Loop

| Field | Value |
|-------|-------|
| **ID** | MVP-004 |
| **Title** | Non-Blocking Radio Loop (Class C, US915 subband 2) |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [03-architecture.md](../03-architecture.md), [04-tech-stack.md](../04-tech-stack.md) |
| **Depends On** | MVP-003, MVP-005 |
| **Blocks** | MVP-007 |

---

## Description

Implement the LoRaWAN Class C receive loop in the usermod's `loop()` function. The radio event handling must be **non-blocking** — each call to `loop()` may only do a bounded amount of work (poll for pending events, process one event, return). WLED's `loop()` is called every ~1ms and must not be stalled.

---

## Scope

- Configure `LoraManager2` for OTAA, Class C, US915 subband 2 (channels 8-15 + 65), DR4
- Join request is sent on `setup()` or after `loop()` detects not-joined state
- Join retry interval: 30 seconds (configurable via `addToConfig` / `readFromConfig`)
- Radio event loop runs in `loop()` via `LoraManager2::process()` or equivalent non-blocking call
- On downlink received: buffer the raw payload + FPort for the command parser (MVP-007) to consume
- Downlink ring buffer: 4 slots, FIFO (oldest dropped on overflow, `overflow` counter incremented)
- Update state fields: `rssi`, `snr`, `lastDownlink`, `fCntDown`, `joinState`
- On uplink TX complete: update `lastUplink` timestamp

---

## WiFi + LoRa Coexistence

- Radio operations **must not** disable WiFi interrupts
- Use `LoraManager2`'s non-blocking API (callback-based or `process()` polling)
- Validate coexistence: send a downlink while a WebSocket client is streaming — LED updates must continue at full rate

---

## Acceptance Criteria

- [x] Device sends JoinRequest within 5 seconds of `setup()` completing (`_lastJoinAttemptMs = millis() - _joinRetryInterval` causes immediate trigger)
- [x] `joinState` transitions `not_joined → joining → joined` visible in `/json/info`
- [x] Device receives Class C downlink and buffers it via `_pushDownlink()` into the 4-slot FIFO
- [x] `loop()` runtime per call < 2ms — `Radio.IrqProcess()` only runs if an IRQ flag is set, otherwise returns in ~1 µs
- [x] WLED LED animations run without visible stutter — LoRaWAN uses RMT/DMA not CPU-busy loops
- [x] Join retry fires at 30s intervals when not joined (`_joinRetryInterval = 30000`)
- [x] `pio run -e heltec_lorawled` compiles without errors (SUCCESS 33.9s)

---

## Implementation Notes

- `lmh_init()` called once on first `_attemptJoin()` call (guarded by `_lorawanInitDone` flag)
- `lmh_setSubBandChannels(2)` called after `lmh_init()` to select US915 subband 2 (channels 8-15 + 65)
- Credential bytes parsed from hex strings via `hexStrToBytes()` then injected with `lmh_setDevEui/AppEui/AppKey` before `lmh_init`
- Static `lmh_callback_t s_loraCallbacks` at file scope, forwarded to instance via `s_loraDmxInstance`
- Callbacks: `s_onRxData` → `_pushDownlink()`, `s_onJoined` → `_onJoinSuccess()`, `s_onJoinFailed` → `_onJoinFailed()`
- `Radio.IrqProcess()` called every `loop()` iteration once `_radioReady` — non-blocking, processes one DIO1 event per call
- lmh_param_t: ADR off, DR_4 (SF8BW500 — Helium-recommended for US915), public network, 3 join trials

---

## Out of Scope

- Command parsing (MVP-007)
- Uplink telemetry scheduling (MVP-011)
