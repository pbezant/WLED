# MVP-002 — Usermod Scaffold

| Field | Value |
|-------|-------|
| **ID** | MVP-002 |
| **Title** | Usermod Scaffold: Directory Structure and Registration |
| **Role** | Coder |
| **Status** | not-started |
| **Priority** | P0 |
| **Spec Refs** | [04-tech-stack.md](../04-tech-stack.md), [10-build-spec.md](../10-build-spec.md) |
| **Depends On** | — |
| **Blocks** | MVP-003, MVP-004, MVP-005, MVP-007 |

---

## Description

Create the file and directory skeleton for the `loradmx` usermod so all subsequent tickets have a valid compile target to build against. The scaffold must compile successfully (even if it does nothing) before any implementation work begins.

---

## Scope

Create the following files:

### `usermods/loradmx/usermod_loradmx.h`
- Skeleton `UsermodLoRaDMX` class extending `Usermod`
- Override stubs: `setup()`, `loop()`, `addToJsonInfo()`, `addToJsonState()`, `addToConfig()`, `readFromConfig()`, `getId()`
- `USERMOD_ID_LORADMX` defined as `59` via `#ifndef` guard
- `REGISTER_USERMOD()` macro call gated behind `#ifdef USERMOD_LORADMX`

### `usermods/loradmx/library.json`
- Full JSON as specified in [10-build-spec.md](../10-build-spec.md)
- `"libArchive": false`
- Dependencies: `SX126x-Arduino@^2.0.30`, `LoraManager2`

### `platformio_override.ini`
- Add `[env:heltec_loradmx]` block as specified in [10-build-spec.md](../10-build-spec.md)
- All build flags listed
- `custom_usermods = loradmx`

---

## Acceptance Criteria

- [ ] `pio run -e heltec_loradmx` compiles with zero errors (warnings acceptable)
- [ ] `/json/info` response contains `"LoRaDMX"` key (even if empty object)
- [ ] `getId()` returns `USERMOD_ID_LORADMX` (59)
- [ ] `library.json` is valid JSON (validate with `python3 -m json.tool library.json`)
- [ ] `platformio_override.ini` contains `[env:heltec_loradmx]`
- [ ] No changes to `platformio.ini` (upstream file)

---

## Out of Scope

- Any actual hardware initialization
- LoRa library integration
- Config persistence
