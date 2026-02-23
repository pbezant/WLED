# PHASE2-002 — Cloud Tenancy / Site-Agent Contract

| Field | Value |
|-------|-------|
| **ID** | PHASE2-002 |
| **Title** | Cloud Tenancy: Multi-Venue Site-Agent Contract |
| **Role** | Architect |
| **Status** | placeholder |
| **Priority** | P2 |
| **Spec Refs** | [01-brief.md](../01-brief.md), [02-user-stories.md](../02-user-stories.md) |
| **Depends On** | MVP complete |

---

## Summary

Define the contract between the cloud platform (LNS + application server) and LoRa-WLED devices deployed across multiple venues. Covers device grouping, site-level broadcast commands, per-device addressing, and tenant isolation.

---

## Proposed Scope

- FPort 10: Broadcast command targeting all devices registered to a site tag
- FPort 11: Multicast group addressing (LoRaWAN Class C multicast)
- Application-layer device groups in Chirpstack mapped to venue zones
- Webhook payload schema for triggering scenes from external event systems (scheduling, DMX show controllers)

---

## Open Questions

- Should site grouping be managed in the LNS (Chirpstack device groups) or in an application layer?
- What is the conflict resolution policy when a broadcast command arrives while a per-device command is being processed?
- How are multicast keys provisioned? (requires LoRaWAN multicast group setup — significant complexity)

---

## Status

**Placeholder — not scheduled.** Requires definition of cloud application architecture first (outside this repo's scope).
