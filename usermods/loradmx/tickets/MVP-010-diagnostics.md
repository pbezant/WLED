# MVP-010 — Diagnostics

| Field | Value |
|-------|-------|
| **ID** | MVP-010 |
| **Title** | Diagnostics: Runtime Telemetry in /json/info and /json/state |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P1 |
| **Spec Refs** | [07-api-spec.md](../07-api-spec.md) |
| **Depends On** | MVP-004, MVP-007 |
| **Blocks** | MVP-011 |

---

## Description

Populate all runtime diagnostic fields in `addToJsonInfo()` and `addToJsonState()` so the web interface and any monitoring tool can observe the device's LoRa status at a glance.

---

## Fields to Expose

### `/json/info` → `u.LoRaDMX`
| Field | Type | Source |
|-------|------|--------|
| `devEUI` | string | cfg |
| `joinEUI` | string | cfg |
| `joinState` | string | runtime |
| `credentialsProvisioned` | bool | cfg |
| `rssi` | number | last downlink |
| `snr` | number | last downlink |
| `lastUplink` | uint32 | seconds since last uplink TX |
| `lastDownlink` | uint32 | seconds since last downlink RX |
| `fCntUp` | uint32 | uplink frame counter |
| `fCntDown` | uint32 | downlink frame counter |
| `uptimeSeconds` | uint32 | `millis()/1000` |

### `/json/state` → `loradmx`
| Field | Type | Source |
|-------|------|--------|
| `joinState` | string | runtime |
| `last_cmd_result` | string | last parse result |
| `dropped` | uint32 | parser counter |
| `replayed` | uint32 | replay counter |
| `overflow` | uint32 | ring buffer overflow counter |

---

## Scope

- All fields emitted on every call to `addToJsonInfo()` and `addToJsonState()`
- `rssi` and `snr` are `null` (JSON null) when not yet joined
- `last_cmd_result` holds last string: `"ok"`, `"preset_not_found"`, `"parse_error"`, `"drop"`, etc.
- Counters persist across resets only within a boot session (not written to cfg.json)

---

## Acceptance Criteria

- [x] All fields from spec appear in `/json/info` and `/json/state` responses
- [x] `rssi`/`snr` return `null` before join, numeric values after
- [x] `dropped` increments correctly when malformed payloads are received
- [x] `last_cmd_result` updates after each processed packet
- [x] Response round-trip < 200ms (WLED standard)

### Implementation Notes
- All fields fully implemented in scaffold (MVP-002); no additional changes required
- `rssi`/`snr` initialised to INT8_MIN sentinel and serialised as `null` via `addToJsonInfo()` guard
- `dropped`/`replayed`/`lastCmdResult` updated in `_processRxQueue()`
