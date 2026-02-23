# Architecture

## System Overview

```
+---------------------------+         LoRaWAN          +---------------------+
|    Cloud / LNS            |  <----------------------> |  Heltec V3 Device   |
|  (TTN / ChirpStack)       |    Downlinks (commands)   |                     |
|                           |    Uplinks (status/ack)   |  ESP32-S3           |
|  - Payload Decoder        |                           |  + SX1262 LoRa      |
|  - Webhook / Integration  |                           |  + WLED Firmware    |
|  - Device Registry        |                           |  + loradmx usermod  |
+---------------------------+                           |                     |
                                                        |  Outputs:           |
         WiFi (optional)                                |  - GPIO data -> LED |
+---------------------------+  <--------------------->  |  - GPIO 19 -> RS485 |
|  User Web Browser         |    HTTP (WLED JSON API)   |    -> DMX fixtures  |
|  (WLED UI / AP or LAN)    |                           +---------------------+
+---------------------------+
```

---

## Component Breakdown

### 1. WLED Core (untouched)
The standard WLED firmware handles:
- LED strip rendering (RMT/I2S for WS2812B, etc.)
- DMX output via bundled SparkFunDMX library on UART2
- WiFi management (AP mode + station mode)
- Web server and JSON API (`/json/state`, `/json/info`, `/json/cfg`)
- Preset storage and application (`applyPreset()`)
- OTA firmware updates

**The `loradmx` usermod does not modify any of these behaviors.**

### 2. `loradmx` Usermod (this project)
A WLED Usermod v2 that adds LoRaWAN receive capability. Responsibilities:

| Component | Responsibility |
|-----------|--------------|
| **CredentialManager** | Generate `devEUI` from MAC, `appKey` from RNG on first boot; persist via `cfg.json` |
| **RadioManager** | Initialize SX1262 on SPI2 (FSPI) via dedicated `SPIClass`; service radio in `loop()` |
| **CommandParser** | Decode raw LoRa downlink bytes/JSON into normalized `LoRaCommand` structs |
| **WLEDMapper** | Translate `LoRaCommand` structs into WLED API calls (`applyPreset`, `bri`, `on`, segment color) |
| **UplinkManager** | Send ack uplinks and periodic heartbeats; enforce duty cycle limits |
| **DiagnosticsStore** | Track counters (dropped, replayed), join status, loop cadence; expose via `addToJsonInfo` |
| **ConfigStore** | Persist and load all usermod settings via `addToConfig`/`readFromConfig` |

### 3. DMX TX Pin Patch (minimal local change)
A `#ifndef DMX_TX_PIN` guard added to `SparkFunDMX.cpp`, `ESPDMX.cpp`, and `wled.cpp` so the DMX UART TX pin can be overridden from GPIO 2 to GPIO 19 via build flag `DMX_TX_PIN=19`. Default behavior is preserved.

---

## Data Flow

### Downlink (LoRa -> WLED)

```
LNS sends downlink
       |
       v
SX1262 receives on Class C window
       |
       v (interrupt via DIO1)
RadioManager::loop() detects packet
       |
       v
CommandParser::decode(bytes, length)
       |
       v
LoRaCommand { type, preset_id, bri, seg_id, color, cmd_id }
       |
       v
DiagnosticsStore: check cmd_id for duplicate
       |-- duplicate -> increment replay counter, discard
       |
       v (new command)
WLEDMapper::apply(command)
       |-- PRESET  -> applyPreset(N)
       |-- POWER   -> WLED on/off state
       |-- BRIGHT  -> WLED brightness
       |-- COLOR   -> segment color + Solid effect
       |-- PATTERN -> apply mapped effect/preset
       |
       v
DiagnosticsStore: record last_cmd_id, apply result
       |
       v
UplinkManager: queue ack uplink (if duty cycle allows)
```

### Uplink (WLED -> LoRa -> LNS)

```
Trigger: command ack OR Ticker heartbeat (20s interval)
       |
       v
UplinkManager::buildPayload()
       |-- Binary heartbeat (6 bytes): counter(4) + 0xC5 + 0x01
       |-- JSON ack on command: {"cmd_id": N, "result": "ok", "preset": M}
       |   Trimmed to fit 242-byte LoRaWAN payload limit
       |
       v
UplinkManager: check duty cycle (last_uplink_time + min_interval)
       |-- throttled -> discard, log
       |
       v
LoRaManager2::sendUplink(payload)
       |
       v
LNS receives uplink, forwards to webhook
```

---

## Integration Boundary

| Layer | Owner | Interface |
|-------|-------|-----------|
| LED hardware output | WLED core | `strip.setPixelColor()`, `strip.show()` |
| DMX hardware output | WLED core (SparkFunDMX) | `WLED_ENABLE_DMX` feature flag |
| WiFi / HTTP | WLED core | WLED webserver, existing JSON API |
| LoRa radio | `loradmx` usermod | `SX126x-Arduino` + `LoraManager2` on SPI2 |
| Configuration | WLED core (`cfg.json`) | `addToConfig` / `readFromConfig` hooks |
| Diagnostics | `loradmx` usermod | `addToJsonInfo` / `addToJsonState` hooks |
| OTA updates | WLED core | `/update` endpoint |

---

## SPI Bus Allocation

| Bus | Peripheral | GPIOs | Owner |
|-----|-----------|-------|-------|
| SPI2 (FSPI) | SX1262 LoRa radio | SCK=9, MISO=11, MOSI=10 | `loradmx` usermod |
| SPI3 (HSPI) | SPI LEDs / displays (if any) | User-configured | WLED core |

**Note:** These are hardwired PCB traces on the Heltec V3. The usermod must create its own `SPIClass` instance on SPI2 to avoid collision with WLED's default SPI.

---

## Pin Map (Heltec WiFi LoRa 32 V3)

| GPIO | Function | Owner |
|------|----------|-------|
| 9    | SX1262 SCK (SPI2) | `loradmx` usermod |
| 10   | SX1262 MOSI (SPI2) | `loradmx` usermod |
| 11   | SX1262 MISO (SPI2) | `loradmx` usermod |
| 8    | SX1262 NSS (CS) | `loradmx` usermod |
| 12   | SX1262 RST | `loradmx` usermod |
| 13   | SX1262 BUSY | `loradmx` usermod |
| 14   | SX1262 DIO1 (interrupt) | `loradmx` usermod |
| 19   | DMX TX (UART1 TX) -> Grove RS485 DI | WLED DMX output |
| 20   | DMX RX (UART1 RX) -> Grove RS485 RO | WLED DMX input (optional) |
| 2    | **Reserved by WLED DMX** — **NOT exposed on Heltec V3 headers** | Patched out |
| TBD  | WS2812B / LED data | WLED core, user-configured |

---

## Sequence Diagram: First Boot + Join

```
Device            WLED Core         loradmx Usermod     LNS
  |                  |                    |               |
  |-- power on ----> |                    |               |
  |                  |-- readFromConfig ->|               |
  |                  |        (no appKey found)           |
  |                  |<- generate devEUI, appKey          |
  |                  |<- persist to cfg.json              |
  |                  |-- setup() -------> |               |
  |                  |                    |-- init SPI2   |
  |                  |                    |-- init radio  |
  |                  |                    |-- OTAA join ->|
  |                  |                    |    (async)    |
  |-- WiFi AP -----> |                    |               |
  |                  |    (user connects, reads creds,    |
  |                  |     registers devEUI+appKey on LNS)|
  |                  |                    |<-- Join Accept|
  |                  |<- joined flag  ----|               |
  |                  |                    |               |
```
