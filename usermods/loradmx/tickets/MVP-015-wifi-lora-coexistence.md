# MVP-015 — WiFi + LoRa Coexistence

| Field | Value |
|-------|-------|
| **ID** | MVP-015 |
| **Title** | WiFi + LoRa Coexistence: Validate Concurrent Operation |
| **Role** | Tester |
| **Status** | not-started |
| **Priority** | P1 |
| **Spec Refs** | [03-architecture.md](../03-architecture.md), [04-tech-stack.md](../04-tech-stack.md) |
| **Depends On** | MVP-004, MVP-008 |
| **Blocks** | — |

---

## Description

Validate that WLED WiFi functionality (HTTP API, WebSocket, mDNS, OTA) operates correctly while the LoRa radio is actively running (Class C receive windows, periodic uplinks). Neither subsystem should starve or corrupt the other.

The primary risk: LoRa SPI transactions on SPI2 (FSPI) conflicting with WiFi interrupt timing, or radio `loop()` work exceeding its time budget and causing WLED watchdog resets.

---

## Test Scenarios

### T1 — HTTP API During Radio Activity
1. Device joined and Class C windows open
2. Client continuously polls `GET /json/state` at 5 Hz for 5 minutes
3. Send LoRa downlink mid-poll sequence
**Expected:** All HTTP responses complete < 200ms, no timeouts, downlink applied

### T2 — WebSocket Streaming During Downlink
1. Open WLED web interface in browser with live animation running
2. Send LoRa downlink `0xF1 01 FF 00 00 00` (Rainbow)
**Expected:** Animation changes within 2 seconds, no WebSocket disconnect, no visible stutter before the transition

### T3 — OTA Update + Periodic Uplink
1. Trigger WLED OTA update while uplink timer is active
2. Monitor serial for panic/watchdog
**Expected:** OTA completes successfully; uplink timer fires after OTA and reboot without crash

### T4 — Loop Time Budget
1. Instrument `loop()` with `micros()` before/after usermod section
2. Run for 10 minutes with active WiFi clients and periodic downlinks
**Expected:** Usermod `loop()` section never exceeds 2ms per call (p99 measured over 10 min)

### T5 — Join Retry Storm
1. Power on with LNS unresponsive (gateway offline)
2. Let device attempt join retries for 60 minutes
**Expected:** WLED web interface remains responsive throughout (LED effects running), no watchdog resets

---

## Acceptance Criteria

- [ ] T1: Zero HTTP timeouts over 5-minute test
- [ ] T2: LED state change visible within 2 seconds of downlink, no WebSocket disconnect
- [ ] T3: OTA completes without panic; uplink resumes after reboot
- [ ] T4: p99 loop time < 2ms (measured and logged)
- [ ] T5: Web interface responsive throughout join retry storm

---

## Deliverables

- Short test report (markdown) in `usermods/loradmx/tests/coexistence-report.md`
- Serial log captures from T4 loop timing measurement
- Any issues found → open new tickets before merge

---

## Out of Scope

- WiFi throughput benchmarking
- LoRa RF performance characterization
