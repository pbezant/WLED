# MVP-012 — DMX TX Pin Patch

| Field | Value |
|-------|-------|
| **ID** | MVP-012 |
| **Title** | DMX TX Pin Patch: Replace Hardcoded GPIO 2 with DMX_TX_PIN |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [10-build-spec.md](../10-build-spec.md), [03-architecture.md](../03-architecture.md) |
| **Depends On** | — |
| **Blocks** | MVP-013 (must be applied before build validation) |

---

## Description

WLED hardcodes GPIO 2 as the DMX TX pin in three source files. GPIO 2 is not exposed on the Heltec WiFi LoRa 32 V3 PCB headers. This ticket patches all three files to use `DMX_TX_PIN` macro (defaulting to 2 for backward compatibility) so the build flag `-D DMX_TX_PIN=19` can override the pin at compile time.

The patch is minimal — three `#ifndef DMX_TX_PIN` guards. No other logic is changed.

---

## Files to Patch

### File 1: `wled00/src/dependencies/dmx/SparkFunDMX.cpp` (~line 35)

**Before:**
```cpp
static const int txPin = 2;
```

**After:**
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
static const int txPin = DMX_TX_PIN;
```

---

### File 2: `wled00/src/dependencies/dmx/ESPDMX.cpp` (~line 31)

**Before:**
```cpp
int sendPin = 2;
```

**After:**
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
int sendPin = DMX_TX_PIN;
```

---

### File 3: `wled00/wled.cpp` (~line 448)

**Before:**
```cpp
PinManager::allocatePin(2, true, PinOwner::DMX);
```

**After:**
```cpp
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
PinManager::allocatePin(DMX_TX_PIN, true, PinOwner::DMX);
```

---

## Acceptance Criteria

- [x] All three files patched exactly as specified above
- [x] `grep -r "txPin = 2\|sendPin = 2\|allocatePin(2" wled00/` returns no results after patch
- [x] `pio run -e heltec_lorawled` compiles without errors with `-D DMX_TX_PIN=19`
- [x] `pio run -e esp32dev` (standard build, no `DMX_TX_PIN` flag) still compiles using GPIO 2 (backward compat)
- [x] `npm test` passes after patch (15/16; 1 pre-existing failure confirmed on clean main)
- [x] `npm run build` passes after patch

---

## Implementation Notes

**Completed:** 2026-02-22

Patched all three files with `#ifndef DMX_TX_PIN / #define DMX_TX_PIN 2 / #endif` guards:
- `wled00/src/dependencies/dmx/SparkFunDMX.cpp` line 35
- `wled00/src/dependencies/dmx/ESPDMX.cpp` line 31
- `wled00/wled.cpp` line 447

Patch is backward-compatible: standard builds with no `-D DMX_TX_PIN` flag default to GPIO 2 as before.

---

## Out of Scope

- Any DMX protocol logic changes
- RX pin configuration (GPIO 20 is handled by `ESPDMX.cpp` independently)
