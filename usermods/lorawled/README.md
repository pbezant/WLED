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
| US915 | DR_1 (SF9BW125) | DR_4 | yes, default 2 | off |
| EU868 | DR_5 | DR_5 | no | **on** |
| AU915 | DR_2 (SF10BW125) | DR_6 | yes, default 2 | off |
| AS923 | DR_2 | DR_5 | no | **on** |

**Duty cycle is not configurable.** In EU868 and AS923 the limit is a legal
requirement (ETSI EN 300 220, ARIB STD-T108) enforced inside the LoRaWAN stack;
in US915 and AU915 the FCC governs dwell time instead and the limiter must stay
off. It is derived from the region and there is deliberately no field that can
turn it off where it is mandated.

`DR_4` is not portable across regions — it is SF8BW500 in US915 but SF8BW125 in
EU868 — which is why `txDataRate` is validated against the selected region
rather than carried across.

### Why US915 defaults to DR_1 and not DR_4

In the 64+8 channel plans (US915, AU915) the data rate alone decides which
channels a frame may use. `Channels[0..63]` are the 125 kHz channels and carry
DR_0–DR_3; `Channels[64..71]` are the 500 kHz channels and carry **only** DR_4
(US915) or DR_6 (AU915). Selecting the 500 kHz rate therefore confines every
uplink to eight frequencies spread across the entire band, of which a typical
8-channel gateway watches at most one — and only if its `LoRa std` channel is
configured at all, which is frequently not the case.

This is silent, because OTAA is unaffected: `RegionAlternateDr()` sends eight of
every nine join trials at DR_0 on the 125 kHz channels, so joining keeps working
while every data uplink disappears. See [M6-016](../../../WLED%20Cloud/tickets/M6-016.md).

`DR_1` is the lowest 125 kHz rate that can carry the 12-byte status uplink —
US915 `DR_0` caps the application payload at 11 bytes and `LoRaMacQueryTxPossible()`
rejects the frame outright. At SF9BW125 a 12-byte uplink is roughly 185 ms of
airtime, comfortably inside the FCC 400 ms dwell limit for 125 kHz channels.

`DR_4` remains selectable via `txDataRate` for a gateway known to watch the
sub-band's 500 kHz channel; the channel-mask correction below is what makes it
land on that channel rather than a random one.

### Sub-band channel mask

`lmh_setSubBandChannels()` in SX126x-Arduino only writes mask words 0–3, so it
enables the sub-band's eight 125 kHz channels and clears **all** the 500 kHz
ones. `RegionAlternateDr()` then sets `ChannelsMask[4] = 0x00FF` on every join
trial — all eight 500 kHz channels, band-wide, with no regard for the sub-band —
and nothing puts it back, because `RegionUS915ApplyCFList()` is an empty
function and the join-accept CFList that would reprogram the mask is discarded.

The usermod therefore writes the correct mask itself, through
`MIB_CHANNELS_MASK` / `MIB_CHANNELS_DEFAULT_MASK`, both before the join and
again from the join-success callback. Sub-band *n* gets 125 kHz channels
`(n-1)*8 … (n-1)*8+7` plus the single 500 kHz channel `64+(n-1)`.

### Changing region

The LoRaWAN stack cannot be re-initialised in place. Saving a change to
`region`, `subBand`, `txDataRate` or `adrEnable` after the stack has joined sets
an internal flag; `loop()` then reboots the device, and the new configuration is
applied on the next join attempt.

An install with no stored `region` key (flashed before this was configurable)
reports incomplete config so WLED writes the defaults out, and comes up as
US915, sub-band 2, DR_1, duty cycle off.

### Diagnostics

`GET /json/info` → `u.LoRaWLED` reports `region`, `subBand` (`0` where no mask
applies), `dutyCycle`, `adr` and the `dataRate` actually passed to `lmh_init()`,
plus:

| Field | Meaning |
|-------|---------|
| `fCntUp` | Frames this usermod handed to the MAC |
| `fCntUpMac` | The MAC's own uplink counter — what the network server's `last_f_cnt_up` should track |
| `classC` | Read back from the MAC after the post-join class request, not assumed |
| `txErrors` | Frames the MAC refused (`LMH_BUSY` / `LMH_ERROR`) |
| `lastTx` | `accepted`, `busy`, `error`, or `none` |

**None of these prove a frame reached the air.** `lmh_send()` returning
`LMH_SUCCESS` means the MAC accepted the frame for scheduling, and `fCntUp`
increments on that return. The authoritative reading is the network server's
`last_f_cnt_up`, or gateway traffic. `fCntUp` and `fCntUpMac` disagreeing points
at the usermod; both climbing while the server stays at `0` points at the air.

## Class C

Class C is fixed. The cloud control model assumes prompt downlink delivery;
Class A would make every downlink wait for the device's next uplink.

The stack joins as Class A and switches on the join-success callback — the MAC
resets to Class A across an OTAA join regardless, and joining *as* Class C
leaves the join cycle depending on the AckTimeout timer to close. The switch is
verified by reading the class back rather than by `lmh_class_request()`'s return
value, which reports `LMH_ERROR` even for a successful A→C switch.

Note that a Class C device is only reachable once the network server has an
**active** session. The server holds a freshly joined session as pending, and
queues downlinks rather than sending them, until it receives an uplink on that
session. The usermod therefore brings the first post-join uplink forward to
`LORAWLED_POST_JOIN_UPLINK_MS` (5 s) instead of waiting a full uplink interval.

## Build

```bash
pio run -e heltec_lorawled
```
