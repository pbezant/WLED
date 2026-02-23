# Parser Test Vectors — LoRa-DMX Usermod
## Implementation: `_parseBinary()` / `_parseJSON()` in `usermod_loradmx.cpp`
## Spec: `docs/08-command-spec.md`

---

## How to Exercise

The parser can be triggered without a real LoRa gateway by posting to
**POST /json/state**:

```json
{"u": {"LoRaDMX": {"loraCmd": {"preset": 5}}}}
```

The `loraCmd` object is JSON-encoded and injected into the rx ring buffer by
`readFromJsonState()`, which then routes it through `_parseJSON()` /
`_parseBinary()` on the next `loop()` call.

---

## Binary Test Vectors

| # | Input (hex) | Expected `LoraDmxCommand.type` | Fields set | `_dropped` | `_replayed` |
|---|-------------|-------------------------------|------------|-----------|------------|
| B01 | `00` | `Power` | `on=false` | +0 | +0 |
| B02 | `01` | `ColorNamed` | `r=255, g=0, b=0` | +0 | +0 |
| B03 | `02` | `ColorNamed` | `r=0, g=255, b=0` | +0 | +0 |
| B04 | `03` | `ColorNamed` | `r=0, g=0, b=255` | +0 | +0 |
| B05 | `04` | `ColorNamed` | `r=255, g=255, b=255` | +0 | +0 |
| B06 | `AA` | `Test` | `r=0, g=255, b=0` | +0 | +0 |
| B07 | `F1 01 C8 00 03 00` | `PatternStart` | `patternType=1, speed=200, cycles=3` | +0 | +0 |
| B08 | `F1 02 FF 00 00 00` | `PatternStart` | `patternType=2, speed=255, cycles=0` | +0 | +0 |
| B09 | `F1 05 80 01 00 00` | `PatternStart` | `patternType=5, speed=384, cycles=0` | +0 | +0 |
| B10 | `F0` | `PatternStop` | — | +0 | +0 |
| B11 | `AB` | `Drop` | — | +1 | +0 |
| B12 | `FF` | `Drop` | — | +1 | +0 |
| B13 | `F1 09 00 00 00 00` | `Drop` | unknown pattern type 9 | +1 | +0 |
| B14 | `F1 01` | `Drop` | payload too short (< 6 bytes) | +1 | +0 |
| B15 | *(empty payload)* | `Drop` | len=0 | +1 | +0 |

---

## JSON Test Vectors

| # | Input (JSON) | Expected `LoraDmxCommand.type` | Fields set | `_dropped` | `_replayed` |
|---|-------------|-------------------------------|------------|-----------|------------|
| J01 | `{"preset":5}` | `Preset` | `preset=5` | +0 | +0 |
| J02 | `{"preset":5,"cmd_id":42}` | `Preset` | `preset=5, cmdId=42` | +0 | +0 |
| J03 | `{"preset":5,"cmd_id":42}` (duplicate) | `Drop` | replay detected | +0 | +1 |
| J04 | `{"bri":128}` | `Brightness` | `bri=128` | +0 | +0 |
| J05 | `{"on":false}` | `Power` | `on=false` | +0 | +0 |
| J06 | `{"on":true}` | `Power` | `on=true` | +0 | +0 |
| J07 | `{"seg":[{"id":0,"col":[[255,0,128]]}]}` | `Segment` | seg data in JSON | +0 | +0 |
| J08 | `{"on":true,"bri":200,"preset":3}` | `Combined` | `on=true, bri=200, preset=3` | +0 | +0 |
| J09 | `{"on":true,"bri":200}` | `Combined` | `on=true, bri=200` | +0 | +0 |
| J10 | `{invalid json` | `Drop` | parse error | +1 | +0 |
| J11 | `{"unknown_key":42}` | `Drop` | no known keys | +1 | +0 |
| J12 | `{"preset":5,"cmd_id":43}` (new id) | `Preset` | not a replay | +0 | +0 |
| J13 | `{"bri":0}` | `Brightness` | `bri=0` (not power off) | +0 | +0 |
| J14 | `{"preset":0}` | `Preset` | `preset=0` (WLED will handle invalid) | +0 | +0 |
| J15 | `{"on":true,"bri":0}` | `Combined` | `on=true, bri=0` | +0 | +0 |

---

## Replay Ring Wrap Test

Send 17 unique `cmd_id` values (0..16), then re-send id=0. Expect:
- First 17: no replay (`_replayed` unchanged)
- 18th (id=0 again): `_replayed++` if still in ring; because ring holds 16, id=0 was evicted on the 17th insert → NOT a replay. Confirms ring wraps correctly.

---

## Error Table Validation

| Condition | Behavior tested by | Counter |
|-----------|-------------------|---------|
| `len == 0` | B15 | `dropped++` |
| Unknown binary byte | B11, B12 | `dropped++` |
| Unknown pattern type | B13 | `dropped++` |
| Pattern payload too short | B14 | `dropped++` |
| JSON parse failure | J10 | `dropped++` |
| No known JSON keys | J11 | `dropped++` |
| Duplicate `cmd_id` | J03 | `replayed++` |

All 20 test vectors (15 binary + 15 JSON) cover every path in `_parseBinary()`
and `_parseJSON()`. Implementation verified against this table during development.
