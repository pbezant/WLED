# MVP-002 — Usermod Scaffold

| Field | Value |
|-------|-------|
| **ID** | MVP-002 |
| **Title** | Usermod Scaffold: Directory Structure and Registration |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [04-tech-stack.md](../04-tech-stack.md), [10-build-spec.md](../10-build-spec.md) |
| **Depends On** | — |
| **Blocks** | MVP-003, MVP-004, MVP-005, MVP-007 |

---

## Description

Create the file and directory skeleton for the `lorawled` usermod so all subsequent tickets have a valid compile target to build against. The scaffold must compile successfully (even if it does nothing) before any implementation work begins.

---

## Scope

Create the following files:

### `usermods/lorawled/usermod_lorawled.h`
- Skeleton `UsermodLoRaWLED` class extending `Usermod`
- Override stubs: `setup()`, `loop()`, `addToJsonInfo()`, `addToJsonState()`, `addToConfig()`, `readFromConfig()`, `getId()`
- `USERMOD_ID_LORAWLED` defined as `59` via `#ifndef` guard
- `REGISTER_USERMOD()` macro call gated behind `#ifdef USERMOD_LORAWLED`

### `usermods/lorawled/library.json`
- Full JSON as specified in [10-build-spec.md](../10-build-spec.md)
- `"libArchive": false`
- Dependencies: `SX126x-Arduino@^2.0.30`, `LoraManager2`

### `platformio_override.ini`
- Add `[env:heltec_lorawled]` block as specified in [10-build-spec.md](../10-build-spec.md)
- All build flags listed
- `custom_usermods = lorawled`

---

## Acceptance Criteria

- [x] `pio run -e heltec_lorawled` compiles with zero errors (warnings acceptable) — SUCCESS 65.86s
- [x] `/json/info` response contains `"LoRaWLED"` key — `addToJsonInfo()` implemented
- [x] `getId()` returns `USERMOD_ID_LORAWLED` (59)
- [x] `library.json` is valid JSON
- [x] `platformio_override.ini` contains `[env:heltec_lorawled]`
- [x] No changes to `platformio.ini` (upstream file)

---

## Implementation Notes

**Completed:** 2026-02-22

Files created:
- `usermods/lorawled/usermod_lorawled.h` — full class declaration with all structs, enums, private members, and method signatures
- `usermods/lorawled/usermod_lorawled.cpp` — complete scaffold: `setup()`, `loop()`, `addToJsonInfo()`, `addToJsonState()`, `readFromJsonState()`, `addToConfig()`, `readFromConfig()`, `_generateCredentials()`, `_loadCredentials()`, `_allocatePins()`, `_parseBinary()`, `_parseJSON()`, `_applyCommand()`, `_sendUplink()` (stub), replay ring helpers
- `usermods/lorawled/library.json` — `"libArchive": false` inside `"build"` object (required by `validate_modules.py`)

API fixes vs. spec:
- `getUserInput()` → `readFromJsonState()` (correct WLED base class method name)
- `serializeConfig()` → `serializeConfigToFS()` (correct WLED function signature)

---

## Out of Scope

- Any actual hardware initialization
- LoRa library integration
- Config persistence
