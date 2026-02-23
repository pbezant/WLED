# Commissioning Guide

This document describes the end-to-end process for provisioning a LoRa-WLED device onto a LoRaWAN Network Server (LNS) and verifying a live downlink.

---

## Overview

The LoRa-WLED device is an **OTAA end device**. It generates its own credentials on first boot and requires manual registration on the LNS (Chirpstack, TTN, Helium, or compatible) before it can join.

```
Flash → First Boot → Read Credentials → Register on LNS → Join → Verify
```

---

## Step 1 — Flash Firmware

Build and flash using PlatformIO:

```bash
cd /path/to/WLED
pio run -e heltec_lorawled --target upload
pio device monitor -e heltec_lorawled -b 115200
```

On first boot, the device will:
1. Detect that `cfg.json` has no credentials stored
2. Derive `devEUI` from `ESP.getEfuseMac()` (64-bit, big-endian hex, formatted as `AA:BB:CC:DD:EE:FF:00:01`)
3. Generate `appKey` using `esp_random()` (128-bit)
4. Persist both to `cfg.json` under `lorawled.devEUI` and `lorawled.appKey`
5. Emit credentials to serial console (one-time only) in the format below

**Serial output on first boot:**
```
[LoRaWLED] First boot - credentials generated
  devEUI : 70B3D57ED005A123
  appKey : A1B2C3D4E5F60718293A4B5C6D7E8F90
  joinEUI: 0000000000000000
[LoRaWLED] Register device on LNS before requesting join
```

> **Security note**: `appKey` is not logged on subsequent boots. If you missed it, reset credentials (see Step 6).

---

## Step 2 — Read Credentials

Credentials are available via three methods after boot:

### Serial Console (first boot only)
See output from Step 1.

### HTTP API
```bash
curl http://<device-ip>/json/info | python3 -m json.tool | grep -A5 LoRaWLED
```

Returns:
```json
{
  "LoRaWLED": {
    "devEUI": "70B3D57ED005A123",
    "joinEUI": "0000000000000000",
    "joinState": "not_joined",
    "credentialsProvisioned": true
  }
}
```

> Note: `appKey` is **never** returned in API responses. Retrieve it from serial console on first boot only or by direct `cfg.json` read.

### Direct cfg.json Read (advanced)
```bash
curl http://<device-ip>/edit?edit=/cfg.json -o cfg.json
grep -o '"appKey":"[^"]*"' cfg.json
```

---

## Step 3 — Register on LNS

### Chirpstack (recommended)
1. Navigate to **Device Profiles** → Create profile: `LoRaWLED`
   - LoRaWAN MAC version: 1.0.3
   - Regional parameters: US915 (subband 2, channels 8-15 + 65)
   - Class: C
   - ADR: disabled
   - Codec: paste the Chirpstack codec from `LoRa-WLED/chirpstack_codec.js`
2. Navigate to **Devices** → Add Device
   - Device name: any (e.g., `lorawled-venue-east`)
   - Device Profile: `LoRaWLED`
   - DevEUI: paste from Step 2
3. Under the device, go to **OTAA Keys**
   - Application Key (AppKey): paste `appKey` from Step 2
4. Confirm device is created

### The Things Network (TTN/TTS)
1. Create Application in TTN console
2. Add end device (manual)
   - LoRaWAN version: 1.0.3
   - Regional parameters: US915 - Subband 2
   - Frequency plan: United States 902-928 MHz, FSB 2
   - JoinEUI: `0000000000000000`
   - DevEUI: paste from Step 2
   - AppKey: paste `appKey` from Step 2
3. Set device class to Class C after join

---

## Step 4 — Trigger Join

Once registered on LNS, restart the device (or wait for the auto-retry timer, default 30s):

```bash
curl -X POST http://<device-ip>/json/state -d '{"command":"reboot"}'
```

Or press the physical reset button.

The device will send a **JoinRequest**. The LNS will respond with a **JoinAccept**. The device transitions `joinState → joined`.

**Monitor join progress:**
```bash
# Poll device state
watch -n 2 'curl -s http://<device-ip>/json/info | python3 -c "import sys,json; d=json.load(sys.stdin); print(d[\"u\"][\"LoRaWLED\"][\"joinState\"])"'

# Or via serial console
pio device monitor -e heltec_lorawled -b 115200
```

Expected serial output:
```
[LoRaWLED] Sending JoinRequest...
[LoRaWLED] JoinAccept received - OTAA joined
[LoRaWLED] devAddr: 01234567
[LoRaWLED] rxWindows: open (Class C)
```

---

## Step 5 — Send Test Downlink

From LNS console or CLI, send `AA` on FPort 1 (test color = green):

### Chirpstack CLI
```bash
chirpstack-cli --server <lns-host>:8080 device enqueue \
  --dev-eui 70B3D57ED005A123 \
  --fport 1 \
  --data AA \
  --confirmed false
```

### TTN CLI
```bash
ttn-lw-cli devices downlink push \
  --app-id <your-app> \
  --dev-id <your-dev> \
  --frm-payload AA \
  --f-port 1
```

### Expected behavior
LEDs turn **green** (all segments, Solid effect). Device emits to serial:

```
[LoRaWLED] Downlink received: FPort=1 len=1 payload=[AA]
[LoRaWLED] CMD: Test → green
[LoRaWLED] Applied to WLED: {on:true, bri:255, seg:[{fx:0, col:[[0,255,0]]}]}
```

---

## Step 6 — Verify Diagnostics

Check uplink telemetry is flowing by inspecting the LNS device feed for FPort 2 packets. Uplink interval default: every 5 minutes.

Or query directly:
```bash
curl http://<device-ip>/json/info | python3 -m json.tool | grep -A20 LoRaWLED
```

Expected response:
```json
{
  "LoRaWLED": {
    "devEUI": "70B3D57ED005A123",
    "joinEUI": "0000000000000000",
    "joinState": "joined",
    "rssi": -85,
    "snr": 7.5,
    "lastUplink": 240,
    "lastDownlink": 12,
    "fCntDown": 1,
    "fCntUp": 3,
    "uptimeSeconds": 3600,
    "brightness": 255,
    "on": true,
    "dropped": 0,
    "replayed": 0
  }
}
```

---

## Bulk Provisioning

For multi-device deployments, extract credentials from all devices before registering:

```bash
#!/bin/bash
# collect-creds.sh — run after flashing each device
DEVICE_IP=$1
OUT="device-$(date +%s).json"
curl -s http://$DEVICE_IP/json/info | python3 -c "
import sys, json
d = json.load(sys.stdin)
u = d['u']['LoRaWLED']
print(json.dumps({'devEUI': u['devEUI'], 'joinEUI': u['joinEUI']}, indent=2))
" > $OUT
echo "Saved to $OUT - manually add appKey from serial console"
```

Register devices in Chirpstack via REST API (batch import):
```bash
# Template: POST /api/devices (see Chirpstack REST API docs)
# Automate with: tools/chirpstack-import.sh (Phase 2)
```

---

## Resetting Credentials

To regenerate credentials (e.g., device was stolen or AppKey must change):

**Option A — HTTP API:**
```bash
curl -X POST http://<device-ip>/json/cfg \
  -H "Content-Type: application/json" \
  -d '{"lorawled": {"resetCredentials": true}}'
```

Device will:
1. Generate new `devEUI` + `appKey`
2. Save to `cfg.json`
3. Print new credentials to serial console (one-time)
4. De-register previous join (send `LeaveRequest` if supported by LNS; otherwise manual cleanup required)
5. Reboot and attempt new join

**Option B — Factory reset:**
Hold the BOOT button for 10 seconds → clears all WLED settings including `cfg.json` → next boot regenerates credentials.

---

## Override Credentials (Pre-provisioned Fleet)

For deployments where the LNS admin pre-creates devices with fixed credentials, push them to the device before first boot:

```bash
curl -X POST http://<device-ip>/json/cfg \
  -H "Content-Type: application/json" \
  -d '{
    "lorawled": {
      "devEUI": "70B3D57ED005A123",
      "appKey": "A1B2C3D4E5F60718293A4B5C6D7E8F90",
      "joinEUI": "0000000000000000"
    }
  }'
```

Device saves these values, skipping auto-generation. Takes effect on next reboot.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No JoinRequest on LNS | Device not rebooted after registration | Reboot device |
| `joinState: "join_failed"` | AppKey mismatch | Re-check AppKey; use `resetCredentials` |
| `joinState: "join_failed"` | Wrong frequency plan | Verify US915 subband 2 on LNS device profile |
| `joinState: "joining"` (stuck) | LNS not reachable / no gateway coverage | Check gateway is online, check LNS logs |
| LEDs don't respond to downlink | Downlink enqueued but not sent (Class C window missed) | Verify device has Class C enabled on LNS |
| `dropped` counter > 0 | Malformed downlink payload | Re-check payload format (see command spec) |
| `credentialsProvisioned: false` | cfg.json write failed (Flash full?) | Check available flash; factory reset if needed |
