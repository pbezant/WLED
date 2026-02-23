# PHASE2-001 — Per-Fixture Raw DMX Channel Control

| Field | Value |
|-------|-------|
| **ID** | PHASE2-001 |
| **Title** | Per-Fixture Raw DMX Channel Control Over LoRa |
| **Role** | Architect / Coder |
| **Status** | placeholder |
| **Priority** | P2 |
| **Spec Refs** | [01-brief.md](../01-brief.md), [08-command-spec.md](../08-command-spec.md) |
| **Depends On** | MVP-007, MVP-008 |

---

## Summary

Extend the downlink command format to support direct DMX channel values for individual fixtures, bypassing WLED's effect/segment abstraction. This enables lighting operators to set exact DMX channel values (e.g., Channel 1=255, Channel 2=128) directly from the LNS.

---

## Proposed Approach

New binary command: `0xD0` + `start_channel` (uint16 LE) + channel count (uint8) + values (N bytes)

Example: Set channels 1-3 to [255, 128, 0]:
```
D0 01 00 03 FF 80 00
```

The mapper would write directly to the DMX universe buffer via SparkFunDMX's `writeByte()` API.

---

## Open Questions

- Does WLED's DMX output path allow direct byte writes alongside effect-generated output, or do effects overwrite the buffer each frame?
- Should the command address fixtures by channel number or by logical fixture ID?
- Is 242-byte payload sufficient for a full DMX universe update (512 channels = 512 bytes)? If not, chunking protocol needed.

---

## Status

**Placeholder — not scheduled.** Do not implement until MVP is deployed and validated in production.
