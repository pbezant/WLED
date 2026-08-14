# LoRa-WLED Usermod

Receives LoRaWAN Class C downlinks on a Heltec WiFi LoRa 32 V3 (ESP32-S3 +
SX1262) and translates them into WLED lighting commands.

## Regional configuration

The radio's regional identity is set from the Usermods settings page (or
`/json/cfg` under `um.LoRaWLED`). Region, default data rate, sub-band
applicability and duty-cycle obligation are stored as one row of a `PROGMEM`
table in `usermod_lorawled.cpp`, so only valid combinations are representable.

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `region` | uint8 index | `0` (US915) | `0` US915, `1` EU868, `2` AU915, `3` AS923 |
| `subBand` | uint8 | `0` | Sub-band 1–8; `0` uses the region default. Ignored where no sub-band mask exists. |
| `adrEnable` | bool | `false` | Adaptive Data Rate |
| `txDataRate` | int8 | `-1` | `-1` uses the region default. A value above the region's maximum is rejected and the default used. |

Per-region behaviour:

| Region | Default DR | Max DR | Sub-band mask | Duty cycle |
|--------|-----------|--------|---------------|------------|
| US915 | DR_4 | DR_4 | yes, default 2 | off |
| EU868 | DR_5 | DR_5 | no | **on** |
| AU915 | DR_2 | DR_6 | yes, default 2 | off |
| AS923 | DR_2 | DR_5 | no | **on** |

**Duty cycle is not configurable.** In EU868 and AS923 the limit is a legal
requirement (ETSI EN 300 220, ARIB STD-T108) enforced inside the LoRaWAN stack;
in US915 and AU915 the FCC governs dwell time instead and the limiter must stay
off. It is derived from the region and there is deliberately no field that can
turn it off where it is mandated.

`DR_4` is not portable across regions — it is SF8BW500 in US915 but SF8BW125 in
EU868 — which is why `txDataRate` is validated against the selected region
rather than carried across.

### Changing region

The LoRaWAN stack cannot be re-initialised in place. Saving a change to
`region`, `subBand`, `txDataRate` or `adrEnable` after the stack has joined sets
an internal flag; `loop()` then reboots the device, and the new configuration is
applied on the next join attempt.

An install with no stored `region` key (flashed before this was configurable)
reports incomplete config so WLED writes the defaults out, and behaves exactly
as before: US915, sub-band 2, DR_4, duty cycle off.

### Diagnostics

`GET /json/info` → `u.LoRaWLED` reports `region`, `subBand` (`0` where no mask
applies), `dutyCycle`, `adr` and the `dataRate` actually passed to `lmh_init()`.

## Class C

Class C is fixed. The cloud control model assumes prompt downlink delivery;
Class A would make every downlink wait for the device's next uplink.

## Build

```bash
pio run -e heltec_lorawled
```
