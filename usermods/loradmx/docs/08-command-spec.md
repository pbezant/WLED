# Command Spec: LoRa Downlink Payload Format

All commands are delivered as LoRaWAN **downlinks** (Class C, FPort 1 unless noted). The device supports both binary and JSON payload formats. Binary is preferred for brevity.

---

## Payload Format Selection

The parser auto-detects format:
- If byte[0] is `{` (0x7B): treat as JSON
- Otherwise: treat as binary

---

## Binary Commands

### CMD-B01 — Power Off
**Byte:** `0x00`
**Action:** Set WLED power state to off (`{"on": false}`)

```
Byte 0: 0x00
```

---

### CMD-B02 — Named Color
**Bytes:** `0x01`–`0x04`
**Action:** Set all segments to a named color (Solid effect)

| Byte | Color | Hex Value |
|------|-------|-----------|
| 0x01 | Red   | #FF0000   |
| 0x02 | Green | #00FF00   |
| 0x03 | Blue  | #0000FF   |
| 0x04 | White | #FFFFFF   |

---

### CMD-B03 — Test Trigger
**Byte:** `0xAA`
**Action:** Set all segments to green (test/health check)

```
Byte 0: 0xAA
```

---

### CMD-B04 — Pattern Start
**Length:** 6 bytes
**Action:** Start a named LED pattern via WLED effect

```
Byte 0:    0xF1        (command type byte)
Byte 1:    type        (0x01=rainbow, 0x02=strobe, 0x03=chase, 0x04=colorFade, 0x05=alternate)
Bytes 2-3: speed       (uint16 little-endian, 0-65535)
Bytes 4-5: cycles      (uint16 little-endian, 0=infinite)
```

**Pattern -> WLED Effect Mapping:**

| type | Pattern | WLED Effect (fx ID) |
|------|---------|---------------------|
| 0x01 | rainbow | Rainbow (9) |
| 0x02 | strobe | Strobe (30) |
| 0x03 | chase | Chase (43) |
| 0x04 | colorFade | Fade (7) |
| 0x05 | alternate | Alternating (45) |

Effects are applied to all active segments. `speed` maps to the WLED `sx` (speed) parameter (0-255 scaled from 0-65535). `cycles` is not directly supported by WLED -- infinite play assumed; future enhancement.

---

### CMD-B05 — Pattern Stop
**Byte:** `0xF0`
**Action:** Stop running pattern; revert to Solid effect with last color

```
Byte 0: 0xF0
```

---

## JSON Commands

JSON payloads must be valid UTF-8 and fit within the LoRaWAN 242-byte payload limit. Use compact JSON (no whitespace).

---

### CMD-J01 — Preset Trigger
**Action:** Apply a WLED preset by ID

```json
{"preset": 5}
```

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `preset` | int | 1-250 | WLED preset ID |

If the preset does not exist, the command is dropped and `last_cmd_result` = `"preset_not_found"`.

---

### CMD-J02 — Power Control
**Action:** Set WLED power on or off

```json
{"on": true}
{"on": false}
```

---

### CMD-J03 — Brightness
**Action:** Set WLED global brightness

```json
{"bri": 128}
```

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `bri` | int | 0-255 | Global brightness. 0 = off (power state unchanged). |

---

### CMD-J04 — Segment Color Override
**Action:** Set a segment's primary color and switch to Solid effect

```json
{"seg": [{"id": 0, "col": [[255, 0, 128]]}]}
```

| Field | Type | Description |
|-------|------|-------------|
| `seg[].id` | int | Segment index (0-based). Omit for all segments. |
| `seg[].col` | array | Array of [R, G, B] color arrays (primary, secondary, tertiary) |

---

### CMD-J05 — Combined State
**Action:** Set multiple WLED state fields in one command

```json
{"on": true, "bri": 200, "preset": 3}
```

Fields from CMD-J01 through CMD-J04 can be combined. Applied atomically.

---

## Command ID (Replay Protection)

All commands may include an optional `cmd_id` field (JSON) or sequence byte (binary, not yet specified). The parser tracks the last `N=16` command IDs. If a received command ID matches a recent one, it is dropped and `replayed` counter increments.

**JSON:**
```json
{"preset": 5, "cmd_id": 42}
```

Binary commands do not yet carry a cmd_id. Replay protection for binary is by exact-match of the last received binary payload within a 5-second window.

---

## Error Handling

| Condition | Behavior | Counter |
|-----------|----------|---------|
| Malformed binary (unknown byte) | Drop, log | `dropped++` |
| Malformed JSON (parse error) | Drop, log | `dropped++` |
| Unknown JSON key | Drop (strict parse), log | `dropped++` |
| Preset not found | Drop, `last_cmd_result = "preset_not_found"` | `dropped++` |
| Duplicate cmd_id | Drop silently | `replayed++` |
| CMD-B04 with unknown type byte | Drop | `dropped++` |

---

## Test Vectors

| Input (hex / JSON) | Expected WLED Action | Notes |
|---|---|---|
| `00` | WLED power off | |
| `01` | All segments red, Solid | |
| `02` | All segments green, Solid | |
| `03` | All segments blue, Solid | |
| `04` | All segments white, Solid | |
| `AA` | All segments green, Solid | Health check |
| `F1 01 C8 00 03 00` | Rainbow effect, speed=200, 3 cycles | |
| `F1 02 FF 00 00 00` | Strobe effect, speed=255, infinite | |
| `F0` | Solid effect, last color | |
| `{"preset":5}` | applyPreset(5) | |
| `{"preset":5,"cmd_id":42}` | applyPreset(5), track cmd_id=42 | |
| `{"preset":5,"cmd_id":42}` (duplicate) | Drop, replayed++ | |
| `{"bri":128}` | Brightness = 128 | |
| `{"on":false}` | WLED power off | |
| `{"on":true}` | WLED power on | |
| `{"seg":[{"id":0,"col":[[255,0,128]]}]}` | Seg 0 pink, Solid | |
| `{"on":true,"bri":200,"preset":3}` | Power on, bri=200, preset 3 | |
| `AB` (unknown byte) | Drop, dropped++ | |
| `{invalid json` | Drop, dropped++ | |
| `F1 09 00 00 00 00` | Drop (unknown pattern type 9), dropped++ | |
