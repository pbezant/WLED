# MVP-001 — Data Contract

| Field | Value |
|-------|-------|
| **ID** | MVP-001 |
| **Title** | Data Contract: LoRa Downlink Payload Format |
| **Role** | Architect |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [08-command-spec.md](../08-command-spec.md) |
| **Blocks** | MVP-007, MVP-008 |

---

## Description

Define and lock the binary and JSON payload formats for LoRaWAN downlink commands (FPort 1). This ticket is purely documentation/spec work — no code is written. The output is an agreed-upon, reviewed command table that all subsequent tickets implement against.

The data contract must be complete before any command parser (MVP-007) or WLED mapper (MVP-008) is written.

---

## Scope

- Review and sign off on all command types in `08-command-spec.md`
- Confirm the test vector table is correct and complete (at minimum: 0x00, 0x01-0x04, 0xAA, 0xF1+pattern, 0xF0, JSON variants)
- Confirm the 242-byte LoRaWAN payload limit constraint
- Confirm FPort assignment: FPort 1 = commands (downlink), FPort 2 = telemetry (uplink)
- Confirm `cmd_id` replay-protection approach
- Flag any gaps or edge cases for inclusion in the spec before coder work begins

---

## Acceptance Criteria

- [x] All binary command types documented (CMD-B01–CMD-B05: Power, Named Color, Test Trigger, Pattern Start, Pattern Stop)
- [x] All JSON command types documented (CMD-J01–CMD-J05: Preset, Power, Brightness, Segment Color, Combined)
- [x] Test vector table contains 20 entries covering happy-path and error cases (>= required 15)
- [x] Error handling table is complete (6 drop conditions with counters)
- [x] FPort assignments confirmed: FPort 1 = commands (downlink), FPort 2 = telemetry (uplink)
- [x] Spec reviewed and signed off — `08-command-spec.md` fully documents all fields, ranges, behaviors

---

## Implementation Notes

- Spec is in `usermods/lorawled/docs/08-command-spec.md`
- 242-byte LoRaWAN payload constraint confirmed; largest binary command is 6 bytes (CMD-B04)
- `cmd_id` replay protection: JSON field tracked in 16-entry ring buffer; binary commands use exact-match within 5-second window
- All command types implemented in `_parseBinary()` and `_parseJSON()` in `usermod_lorawled.cpp` (MVP-007 scaffold)

---

## Out of Scope

- Uplink (telemetry) payload format — covered in MVP-011
- Per-fixture raw DMX channel control (PHASE2-001)
- Any code implementation
