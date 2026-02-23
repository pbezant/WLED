# MVP-006 — Commissioning UI

| Field | Value |
|-------|-------|
| **ID** | MVP-006 |
| **Title** | Commissioning UI: Credential Display in WLED Web Interface |
| **Role** | Coder |
| **Status** | not-started |
| **Priority** | P1 |
| **Spec Refs** | [07-api-spec.md](../07-api-spec.md), [09-commissioning.md](../09-commissioning.md) |
| **Depends On** | MVP-005 |
| **Blocks** | — |

---

## Description

Expose LoRaWAN credentials and join state in the WLED web interface so an installer can read the `devEUI` without serial console access. The primary surface is the existing WLED "Info" panel (accessible at the bottom of the main page or via the `i` icon).

This does **not** require a new HTML page — the `addToJsonInfo()` hook is sufficient to inject data into the existing info panel.

---

## Scope

- `addToJsonInfo()` must emit:
  - `devEUI` (string, formatted as `XX:XX:XX:XX:XX:XX:XX:XX`)
  - `joinEUI` (string)
  - `joinState` (string: `not_joined`, `joining`, `joined`, `join_failed`)
  - `credentialsProvisioned` (bool)
  - `rssi`, `snr` (numbers, or `null` if not joined)
  - See [07-api-spec.md](../07-api-spec.md) for full field list
- `appKey` must **not** appear in any web-visible output
- Info panel must show LoRaDMX section without requiring page reload (already works via polling if the API emits correct data)
- Optional: add a minimal "LoRa Status" section to the existing Settings → Info page

---

## Acceptance Criteria

- [ ] `curl http://<device>/json/info | grep devEUI` returns the correct EUI
- [ ] `joinState` updates live as join progresses (no page reload needed)
- [ ] `appKey` does not appear anywhere in the web interface (automated browser console check)
- [ ] Info panel loads without JavaScript errors
- [ ] Field values match what was printed to serial on first boot

---

## Out of Scope

- Bulk provisioning tooling (covered in commissioning guide)
- Full settings page UI modifications
