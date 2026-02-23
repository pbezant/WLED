# MVP-009 — Config Schema

| Field | Value |
|-------|-------|
| **ID** | MVP-009 |
| **Title** | Config Schema: Persist Usermod Settings to cfg.json |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P1 |
| **Spec Refs** | [07-api-spec.md](../07-api-spec.md) |
| **Depends On** | MVP-002 |
| **Blocks** | MVP-004, MVP-011 |

---

## Description

Implement `addToConfig()` and `readFromConfig()` to persist all usermod settings through WLED's standard `cfg.json` mechanism. Settings must survive OTA firmware updates.

---

## Config Fields

All values stored under the `lorawled` key in `cfg.json`:

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `devEUI` | string | (generated) | Device EUI, 16 hex chars |
| `joinEUI` | string | `"0000000000000000"` | Join/App EUI |
| `appKey` | string | (generated) | 32 hex chars, NEVER returned via API |
| `credentialsProvisioned` | bool | false | Set true after first key gen |
| `joinRetryInterval` | uint32 | 30000 | ms between join retries |
| `uplinkInterval` | uint32 | 300000 | ms between telemetry uplinks |

---

## Scope

- Implement `addToConfig(JsonObject& root)` — writes all fields to `root["lorawled"]`
- Implement `readFromConfig(JsonObject& root)` — reads and applies all fields; returns `true` if all required fields present
- `appKey` is included in `addToConfig` (stored to disk) but filtered out in `addToJsonInfo` / `addToJsonState`
- Validate on `readFromConfig`: `devEUI` must be 16 hex chars, `appKey` 32 hex chars; fail gracefully if malformed

---

## Acceptance Criteria

- [x] After first boot: `cfg.json` contains `lorawled.devEUI`, `lorawled.appKey`
- [x] After OTA update: settings survive — `readFromConfig()` populates all fields from file before `setup()` runs
- [x] `joinRetryInterval` and `uplinkInterval` are user-configurable via POST to `/json/cfg`
- [x] Malformed `cfg.json` (missing fields): `readFromConfig()` returns `false`, `_loadCredentials()` returns `false`, `_generateCredentials()` re-runs — no panic
- [x] `readFromConfig()` returns `false` if required credential fields are absent

---

## Implementation Notes

**Completed:** 2026-02-22 (implemented as part of MVP-002 scaffold)

- `addToConfig()` writes under `root["um"]["LoRaWLED"]` — all credential, interval, and pin fields including `appKey`
- `readFromConfig()` reads with null-guards; enforces `uplinkInterval >= 60000` (duty cycle); returns `false` if credential keys absent
- All 7 SPI pin fields are also persisted/restored to allow user overrides via the WLED settings UI
- Config key strings stored in `PROGMEM` to save heap

---

## Out of Scope

- Config file encryption
- Chirpstack-side config sync
