# Firmware Rules

Apply these rules when editing files in `usermods/wledcloud/`.

## Non-Negotiable Safety Rules

1. **Never block.** No `delay()` > 10ms. Use `millis()` comparisons for timing.
2. **Never call `serializeConfig()` from callbacks.** Set `pendingConfigSave = true`; call only from `loop()`.
3. **Check `strip.isUpdating()`** before any heavy operation in `loop()`.
4. **WiFi work goes in `connected()`**, not `setup()`. WiFi is not ready when `setup()` runs.
5. **Use `CALL_MODE_NOTIFICATION`** when applying cloud commands to prevent echo back to cloud.

## Code Style

- C++, 2-space indent, `camelCase` variables/methods, `PascalCase` classes.
- `PROGMEM` for all constant strings — saves significant RAM.
- ArduinoJson 7 (`JsonDocument`).
- `WebSocketsClient` from Links2004 (raw WS, not Socket.IO).

## Usermod Hook Responsibilities

| Hook | What to do |
|------|-----------|
| `setup()` | Set `initDone = true`. Defer all network work. |
| `loop()` | WS client loop, reconnect, periodic sync, claim polling, pending config save |
| `connected()` | WiFi ready — start WS connection or begin claim flow |
| `onStateChange(mode)` | Set `stateChanged = true`. Skip if `mode == CALL_MODE_NOTIFICATION`. |
| `addToConfig(obj)` | Persist: enabled, server, port, tls, token, venueId, syncSec, telemetry |
| `readFromConfig(obj)` | Load from cfg.json. Called BEFORE `setup()`. Return `false` if required keys missing. |
| `addToJsonInfo(obj)` | Show claim code, connection status, server hostname in WLED Info |

## WebSocket Protocol

All messages: `{ "type": "<event_type>", ...payload_fields }` (raw WS, not Socket.IO).

- **Device → Server:** `state_update`, `telemetry`, `pong`, `claim_ack`
- **Server → Device:** `request_state`, `command`, `ping`, `claim`, `issue_token`

**Update `WLED Cloud/docs/09-websocket-spec.md` before changing the protocol.**

## Memory Budget

Total additional heap must stay under 20KB. Current estimate: ~1,500 bytes RAM, ~30KB flash.

## Build

```bash
pio run -e esp32dev_wledcloud
pio run -e esp32dev_wledcloud --target upload
pio device monitor -b 115200
```

## Key Specs (in WLED Cloud repo)

- `docs/12-usermod-spec.md` — full usermod contract
- `docs/09-websocket-spec.md` — WebSocket protocol
- `docs/flows/device-claim-flow.md` — claim flow sequence
