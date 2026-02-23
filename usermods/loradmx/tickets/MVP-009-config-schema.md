# MVP-009 — Config Schema

| Field | Value |
|-------|-------|
| **ID** | MVP-009 |
| **Title** | Config Schema: Persist Usermod Settings to cfg.json |
| **Role** | Coder |
| **Status** | not-started |
| **Priority** | P1 |
| **Spec Refs** | [07-api-spec.md](../07-api-spec.md) |
| **Depends On** | MVP-002 |
| **Blocks** | MVP-004, MVP-011 |

---

## Description

Implement `addToConfig()` and `readFromConfig()` to persist all usermod settings through WLED's standard `cfg.json` mechanism. Settings must survive OTA firmware updates.

---

## Config Fields

All values stored under the `loradmx` key in `cfg.json`:

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

- Implement `addToConfig(JsonObject& root)` — writes all fields to `root["loradmx"]`
- Implement `readFromConfig(JsonObject& root)` — reads and applies all fields; returns `true` if all required fields present
- `appKey` is included in `addToConfig` (stored to disk) but filtered out in `addToJsonInfo` / `addToJsonState`
- Validate on `readFromConfig`: `devEUI` must be 16 hex chars, `appKey` 32 hex chars; fail gracefully if malformed

---

## Acceptance Criteria

- [ ] After first boot: `cfg.json` contains `loradmx.devEUI`, `loradmx.appKey` (verify via `curl http://<device>/edit?edit=/cfg.json`)
- [ ] After OTA update: settings survive (credentials not regenerated)
- [ ] `joinRetryInterval` and `uplinkInterval` are user-configurable via POST to `/json/cfg`
- [ ] Malformed `cfg.json` (missing fields) causes graceful re-generation of credentials, not a panic
- [ ] `readFromConfig()` returns `false` if required fields are absent

---

## Out of Scope

- Config file encryption
- Chirpstack-side config sync
