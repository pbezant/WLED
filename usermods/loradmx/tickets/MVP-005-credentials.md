# MVP-005 — Credential Generation

| Field | Value |
|-------|-------|
| **ID** | MVP-005 |
| **Title** | On-Device LoRaWAN Credential Generation and Persistence |
| **Role** | Coder |
| **Status** | not-started |
| **Priority** | P0 |
| **Spec Refs** | [09-commissioning.md](../09-commissioning.md), [07-api-spec.md](../07-api-spec.md) |
| **Depends On** | MVP-002 |
| **Blocks** | MVP-004, MVP-006 |

---

## Description

Generate LoRaWAN OTAA credentials on first boot and persist them to `cfg.json`. The device must derive a stable `devEUI` from hardware and generate a cryptographically random `appKey`. Credentials must survive firmware OTA updates (stored in LittleFS `cfg.json`, not in firmware flash).

---

## Credential Derivation

| Credential | Method | Notes |
|------------|--------|-------|
| `devEUI` | `ESP.getEfuseMac()` → 64-bit big-endian hex | Stable across reboots, unique per chip |
| `appKey` | `esp_random()` × 4 calls → 128-bit | Generated once, never regenerated unless reset |
| `joinEUI` | `0000000000000000` (hardcoded for MVP) | Chirpstack/TTN accept any value for OTAA |

---

## Scope

- Implement `credentialsProvisioned` check on `setup()` (read from `cfg.json`)
- If not provisioned: generate `devEUI` and `appKey`, write to `cfg.json`, log to serial (one-time)
- If provisioned: load existing credentials silently
- `appKey` must appear in `cfg.json` but **never** in any HTTP API response
- Implement `resetCredentials` flag: POST to `/json/cfg` with `{"loradmx":{"resetCredentials":true}}` regenerates credentials and reboots
- Support credential override: POST with explicit `devEUI` + `appKey` + `joinEUI` values writes them directly without auto-generation

---

## Security Constraints

- `appKey` must not appear in `/json/info` or `/json/state` responses (confirmed in [07-api-spec.md](../07-api-spec.md))
- `appKey` printed to serial **only** on first boot (never on subsequent boots or after override)
- `cfg.json` is stored in LittleFS — note: no encryption at rest in MVP (Phase 2 concern)

---

## Acceptance Criteria

- [ ] First boot: `devEUI` and `appKey` printed to serial, saved to `cfg.json`
- [ ] Second boot: nothing printed, credentials loaded from file
- [ ] `credentialsProvisioned: true` in `/json/info` after first boot
- [ ] `devEUI` matches `ESP.getEfuseMac()` formatted as 16-char uppercase hex
- [ ] `appKey` does not appear in any HTTP response (automated grep test)
- [ ] `resetCredentials: true` POST causes new credentials to be generated and device reboots
- [ ] `pio run -e heltec_loradmx` compiles without errors

---

## Out of Scope

- LNS registration (manual — see [09-commissioning.md](../09-commissioning.md))
- Credential encryption at rest (Phase 2)
