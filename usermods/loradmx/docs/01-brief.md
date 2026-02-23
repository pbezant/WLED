# Project Brief: LoRa-DMX WLED Usermod

## Summary

The `loradmx` usermod integrates LoRaWAN wireless control into WLED, turning a Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) into a **self-contained, wirelessly-commanded lighting controller**. The device runs WLED natively and can drive both addressable LEDs (WS2812B, SK6812, etc.) and DMX fixtures (par cans, wash lights, movers) over a standard RS485 DMX bus. All lighting control commands are delivered over LoRaWAN from a cloud network server or local gateway.

The device is a **LoRaWAN end device** -- not a bridge. It has its own unique identity, joins a LoRaWAN network autonomously via OTAA, and responds to downlink commands. Users can also access the full WLED web interface directly (over the device's AP hotspot or local WiFi).

---

## Problem Statement

Professional and event lighting installations often require wireless, long-range command delivery to fixtures -- particularly in outdoor or large-venue environments where WiFi coverage is unreliable and cable runs are impractical. Existing WLED deployments have no native mechanism for LoRaWAN control. The LoRa-DMX project has proven the hardware and command model on a standalone ESP32, but requires a full custom firmware stack separate from WLED.

This project bridges that gap: embed LoRaWAN control directly into WLED via a Usermod v2, so users get the full WLED feature set (effects, presets, UI, OTA) plus LoRa control without a separate controller or firmware.

---

## Goals

| # | Goal | MVP | Phase 2 |
|---|------|-----|---------|
| 1 | Receive LoRa downlinks and translate to WLED preset/brightness/color commands | YES | |
| 2 | Drive addressable LEDs (WS2812B/SK6812) via WLED natively | YES | |
| 3 | Drive DMX fixtures via Grove RS485 transceiver on GPIO 19 | YES | |
| 4 | Generate and display LoRaWAN credentials (devEUI, appKey) for user-driven LNS registration | YES | |
| 5 | Expose diagnostics (join status, last command, counters) in WLED JSON API | YES | |
| 6 | Maintain WLED web UI access via AP mode or local WiFi | YES | |
| 7 | Per-fixture raw DMX channel control over LoRa | | YES |
| 8 | Multi-tenant cloud control plane with tenant isolation and command signing | | YES |
| 9 | Canary OTA rollout with rollback gating | | YES |

---

## Non-Goals (Explicit Exclusions)

- **No LoRa realtime pixel streaming** -- LoRaWAN packet rate and payload size are not suitable for per-frame pixel data.
- **No custom DMX output code** -- WLED's built-in `WLED_ENABLE_DMX` handles DMX output natively via SparkFunDMX.
- **No LoRa-triggered OTA** -- WLED's built-in OTA is used exclusively.
- **No WiFi management code** -- WLED handles all WiFi including AP mode and station mode.
- **No porting of prior `DmxController` or `esp_dmx` output code** -- these are superseded by WLED's native DMX.

---

## Constraints

| Constraint | Detail |
|---|---|
| No WLED core modifications | All changes stay in `usermods/loradmx/` except the DMX TX pin patch (minimal, guarded with `#ifndef`) |
| Board-specific | Heltec WiFi LoRa 32 V3 (ESP32-S3). SX1262 SPI pins are PCB traces, not reconfigurable |
| LoRaWAN duty cycles | US915 / Class C. Max uplink rate limited; no spamming on local app changes |
| Flash budget | 8MB flash, 2MB per OTA slot with 8MB partition table. Feature trimming required |
| WLED upstream compatibility | Fork must remain mergeable; no gratuitous core changes |

---

## Success Criteria (MVP)

1. Device joins a TTN or ChirpStack network (OTAA) after user copies auto-generated credentials into LNS console.
2. A LoRa downlink preset trigger payload (`{"preset":5}`) causes WLED to switch to preset 5 within 500ms of receive.
3. A LoRa downlink power-off payload (`0x00`) turns off the LED strip.
4. DMX fixtures connected via Grove RS485 on GPIO 19 respond to the same WLED effects as addressable LEDs.
5. Malformed or duplicate LoRa downlinks are silently dropped; counters increment in `/json/info`.
6. WLED web UI remains accessible and fully functional during LoRa operation.
7. Firmware compiles and flashes to Heltec V3 without error using the `heltec_loradmx` PlatformIO environment.

---

## Stakeholders

| Role | Concern |
|---|---|
| Event lighting operator | Can control fixtures remotely via LoRa without WiFi infrastructure |
| Installation technician | Can commission a new device in under 5 minutes using only the WLED web UI |
| Cloud platform developer | Can integrate device into managed control plane via standard LNS downlink API |
| WLED community | Integration is non-invasive and can be upstreamed as an optional usermod |
