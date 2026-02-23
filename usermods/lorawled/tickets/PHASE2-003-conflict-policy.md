# PHASE2-003 — Multi-Writer Conflict Policy

| Field | Value |
|-------|-------|
| **ID** | PHASE2-003 |
| **Title** | Multi-Writer Conflict Policy: LoRa vs Local Control |
| **Role** | Architect / Coder |
| **Status** | placeholder |
| **Priority** | P2 |
| **Spec Refs** | [02-user-stories.md](../02-user-stories.md) (US-OP-02), [08-command-spec.md](../08-command-spec.md) |
| **Depends On** | MVP complete, PHASE2-002 |

---

## Summary

Define and implement a conflict resolution policy when a LoRa downlink command arrives while a local operator is controlling the device via the WLED web interface, or when two LoRa commands arrive in close succession.

---

## Candidate Policies

| Policy | Description | Trade-off |
|--------|-------------|-----------|
| Last-write-wins | Newest command (by timestamp) takes effect | Simple; local changes can be overwritten unexpectedly |
| LoRa-priority | LoRa always wins, local changes are advisory | Predictable for LNS operators; frustrating for on-site tech |
| Local-lock | Local change sets a lock TTL (e.g., 5 min) during which LoRa commands are queued | Balanced but adds complexity |
| Scene-merge | LoRa sets scene; local adjusts brightness only | Good UX but requires richer command model |

---

## Open Questions

- Should local-lock TTL be configurable?
- Should queued LoRa commands be replayed after lock expires, or dropped?
- Does the LNS need to be notified when a lock is active?

---

## Status

**Placeholder — not scheduled.** MVP uses last-write-wins (no policy). Define policy based on real-world deployment feedback.
