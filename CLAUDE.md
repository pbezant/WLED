# WLED Firmware (WLED Cloud Fork)

Fork of [WLED](https://github.com/Aircoookie/WLED) with the `wledcloud` usermod for connecting ESP32 devices to WLED Cloud. Branch: `wled-cloud`.

**Cloud repo:** `../WLED Cloud/` — linked via `additionalDirectories` in `.claude/settings.local.json`

## What This Repo Is

PlatformIO C++ project targeting ESP32 (primary) and ESP8266 (best-effort). The wledcloud usermod lives at `usermods/wledcloud/`. Do **not** modify WLED core files (`wled00/*.cpp`, `wled00/*.h`) unless absolutely necessary.

## Build Environments

Three envs in `platformio_override.ini`. They are deliberately separate builds —
a device is either a WiFi cloud device or a LoRaWAN device, not both.

| Env | Usermods | Target | Reaches the cloud via |
|-----|----------|--------|-----------------------|
| `esp32dev_wledcloud` | `wledcloud` | Generic ESP32 | WiFi → Socket.IO `/ws/device` |
| `heltec_lorawled` | `lorawled` | Heltec WiFi LoRa 32 V3 | LoRaWAN → LNS (TTN) → cloud |
| `heltec_wledcloud` | `lorawled wledcloud` | Heltec WiFi LoRa 32 V3 | Both — see caveat below |

```bash
# WiFi cloud device (the common case)
pio run -e esp32dev_wledcloud
pio run -e esp32dev_wledcloud --target upload

# LoRaWAN device
pio run -e heltec_lorawled
pio run -e heltec_lorawled --target upload

# Serial monitor
pio device monitor -b 115200
```

**Pick the env before debugging a missing feature.** Code in the wledcloud
usermod that touches LoRa is guarded by `#ifdef USERMOD_LORAWLED`, which is only
defined by `heltec_wledcloud`. Building `esp32dev_wledcloud` compiles that code
out silently — the device claims itself as `connectionType: "wifi"` and never
sends its LoRa credentials, with no error anywhere to explain why.

`heltec_wledcloud` is the combined build used for WiFi-assisted LoRa
commissioning: the device joins WiFi once to claim itself and hand its
devEUI/AppKey to the cloud, then runs over LoRaWAN. It is not the normal
deployment target.

## Usermod Files

```
usermods/wledcloud/
  wledcloud_usermod.h      # Main usermod class declaration
  wledcloud_usermod.cpp    # Implementation
  library.json             # PlatformIO dependencies
  README.md                # Setup and configuration guide

usermods/lorawled/
  usermod_lorawled.h       # LoRaWAN radio, OTAA join, Class C, downlink decode
  usermod_lorawled.cpp     # Implementation
```

The two usermods are independent. `lorawled` does not depend on `wledcloud`;
`wledcloud` reaches into `lorawled` only through `usermods.lookup()` behind
`#ifdef USERMOD_LORAWLED`.

## Cloud Repo Specs (Read These First)

| Topic | File |
|-------|------|
| Usermod contract | `../WLED Cloud/docs/12-usermod-spec.md` |
| WebSocket protocol | `../WLED Cloud/docs/09-websocket-spec.md` |
| Device claim flow | `../WLED Cloud/docs/flows/device-claim-flow.md` |
| Binary LoRa encoding | `../WLED Cloud/docs/11-lns-integration.md` |
| Conflict policy | `../WLED Cloud/docs/15-conflict-policy.md` |
| Firmware tickets | `../WLED Cloud/tickets/` (Role: Firmware Coder) |

## Code Conventions

- **Language:** C++ (Arduino/ESP-IDF compatible), 2-space indent
- **Naming:** `camelCase` for variables/methods, `PascalCase` for classes
- **Strings:** `PROGMEM` for all constant strings (saves RAM)
- **JSON:** ArduinoJson 7 (`JsonDocument`)
- **WebSocket:** Links2004 `WebSocketsClient` library
- **Registration:** `static MyInstance instance; REGISTER_USERMOD(instance);`

## Critical Rules — Never Break These

1. **Never block.** No `delay()` > 10ms. Use `millis()` comparisons.
2. **Never call `serializeConfig()` from callbacks.** Set `pendingConfigSave = true`, call from `loop()`.
3. **Check `strip.isUpdating()`** before heavy work in `loop()`.
4. **WiFi work goes in `connected()`**, not `setup()` (WiFi isn't ready at setup time).
5. **Use `CALL_MODE_NOTIFICATION`** when applying cloud commands to prevent echo back to cloud.

## WLED Usermod Hooks

| Hook | Purpose |
|------|---------|
| `setup()` | Set `initDone = true`. Defer network work to `connected()`. |
| `loop()` | WS client loop, reconnect logic, periodic sync, claim polling |
| `connected()` | WiFi ready — start WS connection or begin claim flow |
| `onStateChange(mode)` | Flag `stateChanged = true`. Skip `CALL_MODE_NOTIFICATION` to avoid echo. |
| `addToConfig(obj)` | Persist: enabled, server, port, tls, token, venueId, syncSec, telemetry |
| `readFromConfig(obj)` | Load config from cfg.json. Called BEFORE setup(). Return false if keys missing. |
| `addToJsonInfo(obj)` | Show claim code, connection status, server name in WLED Info page |

## WLED State Variables (via `#include "wled.h"`)

- `bri` — brightness
- `strip` — LED strip object; `strip.getMainSegment()` for mode, speed, intensity, palette, colors
- `WLED_CONNECTED` — macro for WiFi status
- `deserializeState(JsonObject&, CALL_MODE_NOTIFICATION)` — apply JSON state to WLED
- `serializeConfig()` — save cfg.json to flash (call sparingly, from loop only)

## Memory Budget

| Component | Estimate |
|-----------|----------|
| WebSocketsClient | ~500 bytes |
| JSON buffers | ~512 bytes |
| Config strings | ~250 bytes |
| Runtime state | ~50 bytes |
| Stack overhead | ~200 bytes |
| **Total RAM** | **~1,500 bytes** |
| **Flash** | **~30 KB** |

Total additional RAM must stay under 20KB. ESP32 typically has 40–80KB free heap.

## WebSocket Protocol

All messages use JSON envelope: `{ "type": "<event_type>", ...fields }`

Device connects to `/ws/device` (raw WebSocket, not Socket.IO) with `{ auth: { token: "<deviceJWT>" } }`.

Key message types:
- **Device → Server:** `state_update`, `telemetry`, `pong`, `claim_ack`
- **Server → Device:** `request_state`, `command`, `ping`, `claim`, `issue_token`

See `docs/09-websocket-spec.md` for full protocol. **If you change the protocol, update the spec first.**
