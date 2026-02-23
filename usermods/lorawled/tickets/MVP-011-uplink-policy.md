# MVP-011 — Uplink Policy

| Field | Value |
|-------|-------|
| **ID** | MVP-011 |
| **Title** | Uplink Policy: Periodic Telemetry on FPort 2 |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P1 |
| **Spec Refs** | [07-api-spec.md](../07-api-spec.md), [09-commissioning.md](../09-commissioning.md) |
| **Depends On** | MVP-004, MVP-010, MVP-009 |
| **Blocks** | — |

---

## Description

Implement a periodic uplink that transmits a compact status payload on FPort 2 at a configurable interval. This allows the LNS to confirm the device is alive, observe RSSI/SNR from both directions, and monitor LED state without polling the HTTP API.

---

## Uplink Payload Format (FPort 2)

Compact binary, 12 bytes:

```
Byte 0:   version = 0x01
Byte 1:   flags   = [bit0: on, bit1: joined, bit2-7: reserved]
Byte 2:   bri     = global brightness (0-255)
Byte 3:   fx      = active effect ID (0-255)
Bytes 4-5: dropped  (uint16 LE)
Bytes 6-7: replayed (uint16 LE)
Bytes 8-9: fCntDown (uint16 LE, lower 16 bits only)
Byte 10:  rssi_abs = abs(rssi) clamped to 0-255 (last downlink RSSI)
Byte 11:  snr_x4  = snr * 4, signed byte (-128 to 127)
```

---

## Scope

- Send uplink on FPort 2 every `uplinkInterval` ms (default: 300,000ms = 5 minutes)
- `uplinkInterval` is user-configurable via cfg (MVP-009)
- Do **not** send uplink if not joined (`joinState != joined`)
- Do **not** exceed LoRaWAN duty cycle: minimum enforced interval = 60,000ms regardless of config
- Log uplink TX to serial: `[LoRaWLED] Uplink TX: FPort=2 len=12`
- Update `lastUplink` timestamp after confirmed TX

---

## Acceptance Criteria

- [x] LNS shows FPort 2 uplink packets at configured interval
- [x] Uplink is not sent when `joinState != joined`
- [x] Minimum interval protection: setting `uplinkInterval < 60000` silently clamped to 60000
- [x] Uplink payload decodes correctly against the byte format above (verified with Chirpstack codec or manual parse)
- [x] `lastUplink` field in `/json/info` reflects seconds since last TX

### Implementation Notes
- `_sendUplink()` builds 12-byte FPort 2 payload and calls `lmh_send(&txData, LMH_UNCONFIRMED_MSG)`
- Guard: `if (!_joined) return;` prevents uplink when not joined
- 60s floor clamped in `readFromConfig()` (line ~316)
- Chirpstack codec FPort 2 decoder added to `chirpstack_codec.js`
- `_fCntUp` incremented on successful send; `_lastUplinkMs` timestamped

---

## Out of Scope

- Confirmed (ACKed) uplinks — unconfirmed only for MVP
- Adaptive uplink rate based on duty cycle budget
