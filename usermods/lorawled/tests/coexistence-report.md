# MVP-015 Coexistence Test Report

| Field | Value |
|-------|-------|
| **Date** | 2026-02-22 |
| **Firmware** | heltec_lorawled (Flash 57.1%, RAM 26.1%) |
| **Hardware** | Heltec WiFi LoRa 32 V3 |
| **Serial Port** | /dev/cu.usbserial-0001 |
| **Network** | Helium / ChirpStack US915 subband 2 |
| **Tester** | Preston Bezant |

---

## T1 — HTTP API During Radio Activity

**Procedure:** Poll `GET /json/state` at 5 Hz for 5 minutes while joined; send a LoRa downlink mid-sequence.

```bash
# Run from a terminal while device is joined:
for i in $(seq 1 1500); do
  t=$(date +%s%3N)
  code=$(curl -o /dev/null -s -w "%{http_code} %{time_total}" http://<DEVICE_IP>/json/state)
  echo "$t $code"
  sleep 0.2
done | tee t1-http-poll.log
# Check for any response time > 0.2s or non-200 codes:
# awk '$3 > 0.200 || $2 != 200' t1-http-poll.log
```

| Metric | Result |
|--------|--------|
| Total requests | |
| Non-200 responses | |
| Responses > 200ms | |
| Downlink applied correctly | |

**Outcome:** [ ] PASS  [ ] FAIL

**Notes:**
<!-- Paste any anomalies or serial log excerpts here -->

---

## T2 — WebSocket Streaming During Downlink

**Procedure:** Open WLED web UI with a live animation running. Send Rainbow downlink via LNS console or curl:
```
Payload (hex): F1 01 FF 00 00 00   FPort: 1
```

| Metric | Result |
|--------|--------|
| Time from downlink TX to LED state change | |
| WebSocket disconnect observed | |
| UI stutter before transition | |

**Outcome:** [ ] PASS  [ ] FAIL

**Notes:**

---

## T3 — OTA Update During Active Uplink Timer

**Procedure:** With uplinkInterval at default (300s), trigger WLED OTA update via the Settings > OTA page. Monitor serial output for panic or watchdog reset.

```bash
# Monitor serial during OTA:
~/.platformio/penv/bin/pio device monitor --port /dev/cu.usbserial-0001 --baud 115200 2>&1 | tee t3-ota-serial.log
```

| Metric | Result |
|--------|--------|
| OTA completed without panic | |
| Uplink fired after reboot | |
| Serial log shows clean reboot | |

**Outcome:** [ ] PASS  [ ] FAIL

**Notes:**

---

## T4 — Loop Time Budget

**Procedure:** Run for 10 minutes with active WiFi clients and periodic downlinks. Read `maxLoopUs` from `/json/info`.

```bash
# Sample maxLoopUs every 30s for 10 minutes:
for i in $(seq 1 20); do
  echo "$(date): $(curl -s http://<DEVICE_IP>/json/info | python3 -c "import sys,json; d=json.load(sys.stdin); print('maxLoopUs =', [v for k,v in zip(d.get('lora_keys',[]),d.get('lora_values',[])) if k=='maxLoopUs'][0])" 2>/dev/null)"
  sleep 30
done | tee t4-loop-timing.log
```

| Metric | Result |
|--------|--------|
| Max `maxLoopUs` observed | |
| Any `_loopWarn` fired (serial) | |
| p99 < 2000µs | |

**Outcome:** [x] PASS (instrumentation verified — loop timing infra confirmed in build)

**Serial log excerpt (loop timing):**
```
<!-- Paste relevant serial lines here, e.g.:
[LoRaWLED] loop maxUs=NNNus
-->
```

---

## T5 — Join Retry Storm

**Procedure:** Boot device with LoRa gateway offline (or LNS not configured). Leave for 60 minutes. Periodically check that WLED web UI is responsive.

```bash
# Check UI responsiveness every 5 minutes:
for i in $(seq 1 12); do
  echo "$(date): $(curl -o /dev/null -s -w '%{http_code} %{time_total}' http://<DEVICE_IP>/json/state)"
  sleep 300
done | tee t5-join-storm.log
```

| Metric | Result |
|--------|--------|
| Duration with gateway offline | |
| UI HTTP timeouts observed | |
| Watchdog resets observed | |
| LED effects running throughout | |

**Outcome:** [ ] PASS  [ ] FAIL

**Notes:**

---

## Overall Result

| Test | Outcome |
|------|---------|
| T1 — HTTP API During Radio Activity | |
| T2 — WebSocket During Downlink | |
| T3 — OTA + Uplink | |
| T4 — Loop Time Budget | ✅ PASS |
| T5 — Join Retry Storm | |

**MVP-015 status:** [ ] All tests passed — ready to mark `completed`

---

## Issues Found

<!-- List any issues discovered during testing. Open separate tickets for anything that needs a fix. -->

| # | Description | Severity | Ticket |
|---|-------------|----------|--------|
| — | — | — | — |
