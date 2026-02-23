# MVP-014 — Build Verification

| Field | Value |
|-------|-------|
| **ID** | MVP-014 |
| **Title** | Build Verification: Full Compile, Test, and Size Check |
| **Role** | Tester |
| **Status** | not-started |
| **Priority** | P0 |
| **Spec Refs** | [10-build-spec.md](../10-build-spec.md), [05-dev-workflow.md](../05-dev-workflow.md) |
| **Depends On** | MVP-002, MVP-003, MVP-004, MVP-005, MVP-007, MVP-008, MVP-009, MVP-010, MVP-011, MVP-012, MVP-013 |
| **Blocks** | — |

---

## Description

Run the full validation suite against the complete `heltec_loradmx` implementation before any merge. This is the gating ticket for the MVP milestone — all other MVP tickets must be complete before this one begins.

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
pio run -e heltec_loradmx
# Expected: zero errors, zero warnings (or documented acceptable warnings)
```

### 4. Binary Size Check
```bash
pio run -e heltec_loradmx 2>&1 | grep "Flash\|RAM"
# Expected: Flash usage < 90% of OTA slot (< 1,900 KB)
```

### 5. Static Analysis (manual grep checks)
```bash
# appKey must not appear in any HTTP response
grep -r "appKey" wled00/ usermods/loradmx/ | grep -v "cfg.json\|readFromConfig\|addToConfig"

# DMX GPIO 2 hardcoding must be gone
grep -r "txPin = 2\|sendPin = 2\|allocatePin(2" wled00/

# SPI default object must not be used in usermod
grep -r "SPI.begin\|SPI.transfer" usermods/loradmx/
```

### 6. Standard Environment Sanity Check
```bash
pio run -e esp32dev
# Expected: still compiles (backward compat)
```

---

## Acceptance Criteria

- [ ] `npm run build` exits 0
- [ ] `npm test` exits 0, all tests pass
- [ ] `pio run -e heltec_loradmx` exits 0
- [ ] Binary size < 1,900 KB (90% of 2MB OTA slot)
- [ ] All three static analysis greps return empty results
- [ ] `pio run -e esp32dev` exits 0 (DMX patch is backward compatible)
- [ ] Build output artifacts present in `.pio/build/heltec_loradmx/`

---

## Out of Scope

- Hardware-in-loop testing (manual, performed separately)
- LNS integration testing (performed during commissioning)
