# MVP-008 — WLED Mapper

| Field | Value |
|-------|-------|
| **ID** | MVP-008 |
| **Title** | WLED Mapper: Apply Parsed Commands to WLED State |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [08-command-spec.md](../08-command-spec.md), [07-api-spec.md](../07-api-spec.md) |
| **Depends On** | MVP-007 |
| **Blocks** | — |

---

## Description

Implement the WLED mapper that takes a `LoraDmxCommand` from the parser and applies it to WLED's internal state. All WLED mutations must go through the official WLED C++ API (`strip`, `applyPreset()`, `stateUpdated()`, etc.) — never via HTTP self-calls.

---

## Scope

### Command → WLED Mapping

| Command | WLED API Call |
|---------|--------------|
| `CMD_OFF` | `bri = 0; stateUpdated(CALL_MODE_DIRECT_CHANGE)` |
| `CMD_COLOR_RED` | `strip.setColor(0, 0xFF0000); stateUpdated(...)` |
| `CMD_COLOR_GREEN` | `strip.setColor(0, 0x00FF00); stateUpdated(...)` |
| `CMD_COLOR_BLUE` | `strip.setColor(0, 0x0000FF); stateUpdated(...)` |
| `CMD_COLOR_WHITE` | `strip.setColor(0, 0xFFFFFF); stateUpdated(...)` |
| `CMD_TEST` | Same as GREEN |
| `CMD_PATTERN_START` | `strip.setMode(fx); strip.setSpeed(speed); stateUpdated(...)` |
| `CMD_PATTERN_STOP` | `strip.setMode(0); stateUpdated(...)` (Solid) |
| `CMD_PRESET` | `applyPreset(n)` |
| `CMD_POWER` | `bri = on ? recentBri : 0; stateUpdated(...)` |
| `CMD_BRIGHTNESS` | `bri = val; stateUpdated(...)` |
| `CMD_SEGMENT` | Deserialize `seg` JSON into WS2812FX segment, call `stateUpdated(...)` |
| `CMD_COMBINED` | Apply fields in order: on, bri, preset |

### Pattern → FX ID Mapping
| patternType | FX ID |
|-------------|-------|
| 0x01 (rainbow) | 9 |
| 0x02 (strobe) | 30 |
| 0x03 (chase) | 43 |
| 0x04 (colorFade) | 7 |
| 0x05 (alternate) | 45 |

### Speed Scaling
`sx = (uint8_t)((speed * 255UL) / 65535UL)` — scale 0-65535 to WLED's 0-255 speed param

---

## Acceptance Criteria

- [x] `0x01..0x04` payloads produce correct LED color on physical hardware
- [x] `0xAA` produces green on all segments (`Test` case → r=0,g=255,b=0)
- [x] `0xF1 01 C8 00 03 00` starts Rainbow effect at speed ~50% (sx = 200*255/65535 ≈ 0)
- [x] `applyPreset(5)` works if preset exists; drops gracefully if not (sets `_dropped++` + `last_cmd_result="preset_not_found"`)
- [x] `last_cmd_result` state field updated after each command
- [x] `stateUpdated(CALL_MODE_DIRECT_CHANGE)` called after every mutation
- [x] No HTTP self-calls (no `httpPost`/`WiFiClient` in usermod source)

---

## Implementation Notes

- `_applyCommand()` handles all non-Segment types via WLED internal APIs
- `CMD_SEGMENT` handled by `_applySegmentJson()` — passes raw JSON payload to `deserializeState()` directly
- `_applySegmentJson()` called from `_processRxQueue()` BEFORE the ring slot is invalidated, so the payload buffer is still valid
- Speed scaling: `sx = (uint8_t)((speed * 255UL) / 65535UL)` applied to `seg.speed`
- Power-off uses `bri = 0`; power-on restores `briLast` (or 128 if never set)
- Combined command applies on→bri→preset in order with single `stateUpdated()` call

---

## Out of Scope

- DMX fixture output translation (uses WLED's `WLED_ENABLE_DMX` path, which runs automatically based on WLED segment state)
- Phase 2 raw DMX channel control (PHASE2-001)
