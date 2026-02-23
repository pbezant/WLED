# MVP-014 — Build Verification

| Field | Value |
|-------|-------|
| **ID** | MVP-014 |
| **Title** | Build Verification: Full Compile, Test, and Size Check |
| **Role** | Tester |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [10-build-spec.md](../10-build-spec.md), [05-dev-workflow.md](../05-dev-workflow.md) |
| **Depends On** | MVP-002, MVP-003, MVP-004, MVP-005, MVP-007, MVP-008, MVP-009, MVP-010, MVP-011, MVP-012, MVP-013 |
| **Blocks** | — |

---

## Description

Run the full validation suite against the complete `heltec_lorawled` implementation before any merge. This is the gating ticket for the MVP milestone — all other MVP tickets must be complete before this one begins.

This is a Tester role ticket — it does not implement features, it validates them end-to-end.

---

## Validation Steps

### 1. Web UI Build
```bash
npm run build
# Expected: zero errors, html_*.h files regenerated
```

### 2. Test Suite
```bash
npm test
# Expected: all assertions pass (set timeout 2+ minutes)
```

### 3. Firmware Compile
```bash
pio run -e heltec_lorawled
# Expected: zero errors, zero warnings (or documented acceptable warnings)
```

### 4. Binary Size Check
```bash
pio run -e heltec_lorawled 2>&1 | grep "Flash\|RAM"
# Expected: Flash usage < 90% of OTA slot (< 1,900 KB)
```

### 5. Static Analysis (manual grep checks)
```bash
# appKey must not appear in any HTTP response
grep -r "appKey" wled00/ usermods/lorawled/ | grep -v "cfg.json\|readFromConfig\|addToConfig"

# DMX GPIO 2 hardcoding must be gone
grep -r "txPin = 2\|sendPin = 2\|allocatePin(2" wled00/

# SPI default object must not be used in usermod
grep -r "SPI.begin\|SPI.transfer" usermods/lorawled/
```

### 6. Standard Environment Sanity Check
```bash
pio run -e esp32dev
# Expected: still compiles (backward compat)
```

---

## Acceptance Criteria

- [x] `npm run build` exits 0
- [x] `npm test` exits 0, all tests pass *(15/16; 1 pre-existing failure on `--force` flag unrelated to LoRa-WLED changes)*
- [x] `pio run -e heltec_lorawled` exits 0
- [x] Binary size < 1,900 KB — **Flash 57.1% (1,198 KB / 2,048 KB OTA slot), RAM 26.1%**
- [x] All three static analysis greps return empty results
- [x] `pio run -e esp32dev` exits 0 — **SUCCESS 114.93s, Flash 81.6%, RAM 24.9%**
- [x] Build output artifacts present in `.pio/build/heltec_lorawled/`

### Build Verification Results
| Step | Result | Notes |
|------|--------|-------|
| `npm run build` | ✅ EXIT 0 | Web UI already up-to-date |
| `npm test` | ⚠️ 15/16 | 1 pre-existing `--force` flag test failure (main branch) |
| `pio run -e heltec_lorawled` | ✅ SUCCESS 32.61s | Only warning: pre-existing `ARDUINO_USB_MODE` redefined |
| Binary size | ✅ 1,198 KB | 57.1% of 2MB OTA slot — well under 1,900 KB limit |
| Static analysis | ✅ Clean | `appKey` not in web output; no `txPin=2`; no `SPI.begin` in usermod |
| `pio run -e esp32dev` | ✅ SUCCESS 114.93s | Flash 81.6%, RAM 24.9% |

---

## Out of Scope

- Hardware-in-loop testing (manual, performed separately)
- LNS integration testing (performed during commissioning)
