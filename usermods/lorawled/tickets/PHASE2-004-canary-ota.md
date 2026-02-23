# PHASE2-004 — Canary OTA Rollout

| Field | Value |
|-------|-------|
| **ID** | PHASE2-004 |
| **Title** | Canary OTA Rollout: Staged Firmware Updates Over LoRa |
| **Role** | Architect / Coder |
| **Status** | placeholder |
| **Priority** | P2 |
| **Spec Refs** | [04-tech-stack.md](../04-tech-stack.md), [10-build-spec.md](../10-build-spec.md) |
| **Depends On** | MVP complete |

---

## Summary

Implement a canary-style firmware OTA rollout where a new firmware version is pushed to a small percentage of devices first through the LoRaWAN network. Devices that report healthy uplinks after the update (within a configurable window) allow the rollout to proceed. Devices that stop responding trigger automatic rollback to the previous OTA slot.

---

## Proposed Mechanism

1. LNS application server tags devices into rollout cohorts (e.g., 10% → 50% → 100%)
2. OTA trigger sent via LoRa downlink (FPort 15): URL of firmware binary + expected SHA256
3. Device downloads binary over WiFi (existing WLED OTA path), writes to inactive OTA slot
4. Device reboots, sends FPort 2 uplink with new firmware version in flags byte
5. If no uplink within 5 minutes → LNS marks device as degraded → device auto-rolls back on next join failure

---

## Open Questions

- Firmware URL in LoRa downlink — how to fit a full HTTPS URL in 242 bytes? (URL shortener? Indirect pointer to LNS-hosted URL?)
- Should the rollback be automatic (device-side) or manual (LNS-initiated)?
- How is SHA256 verification handled without a crypto library on the device?

---

## Status

**Placeholder — not scheduled.** Standard WLED WiFi OTA is sufficient for MVP. Canary rollout is only needed at fleet scale (50+ devices).
