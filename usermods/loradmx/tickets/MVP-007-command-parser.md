# MVP-007 — Command Parser

| Field | Value |
|-------|-------|
| **ID** | MVP-007 |
| **Title** | Command Parser: Binary and JSON Downlink Decoder |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [08-command-spec.md](../08-command-spec.md) |
| **Depends On** | MVP-001, MVP-004 |
| **Blocks** | MVP-008 |

---

## Description

Implement the command parser that reads raw downlink bytes from the ring buffer (filled by MVP-004) and produces a normalized `LoraDmxCommand` struct. The parser does **not** apply commands to WLED — that is MVP-008. The parser handles format detection, validation, replay protection, and error counting.

---

## Scope

### Format Detection
- If `payload[0] == '{'` (0x7B): parse as JSON using WLED's bundled ArduinoJson
- Otherwise: parse as binary

### Binary Parsing
| Byte | Action |
|------|--------|
| 0x00 | `CMD_OFF` |
| 0x01 | `CMD_COLOR_RED` |
| 0x02 | `CMD_COLOR_GREEN` |
| 0x03 | `CMD_COLOR_BLUE` |
| 0x04 | `CMD_COLOR_WHITE` |
| 0xAA | `CMD_TEST` |
| 0xF0 | `CMD_PATTERN_STOP` |
| 0xF1 + 5 bytes | `CMD_PATTERN_START` (type, speed LE uint16, cycles LE uint16) |
| Unknown | `CMD_DROP` → `dropped++` |

### JSON Parsing
Accepted keys: `preset`, `on`, `bri`, `seg`, `cmd_id`
Unknown top-level keys → `dropped++`

### Replay Protection
- Track last 16 `cmd_id` values
- If `cmd_id` matches a recent value → `replayed++`, drop
- Binary replay: exact-match of last payload within 5s window

### Output Struct
```cpp
struct LoraDmxCommand {
  enum Type { OFF, COLOR, TEST, PATTERN_START, PATTERN_STOP, PRESET, POWER, BRIGHTNESS, SEGMENT, COMBINED, DROP } type;
  uint8_t r, g, b;          // for COLOR/TEST
  uint8_t patternType;      // for PATTERN_START
  uint16_t speed, cycles;   // for PATTERN_START
  uint8_t preset;           // for PRESET
  bool on;                  // for POWER/COMBINED
  uint8_t bri;              // for BRIGHTNESS/COMBINED
  // seg data pointer omitted for MVP — pass raw JSON slice to mapper
  uint32_t cmdId;
};
```

---

## Acceptance Criteria

- [x] All 20 test vectors from `08-command-spec.md` produce correct `LoraDmxCommand` values (verified in `tests/test_parser.md`)
- [x] Unknown binary byte increments `dropped` and returns `Drop` type (default switch case)
- [x] Duplicate `cmd_id` increments `replayed` and returns `Drop` (replay ring check before field parse)
- [x] Malformed JSON increments `dropped` and returns `Drop` (deserializeJson error check)
- [x] Parser processes one command per `loop()` call (`_processRxQueue()` returns after one dequeue)
- [x] Test coverage document created: `usermods/loradmx/tests/test_parser.md` with 30 test vectors

---

## Implementation Notes

- Binary parser: full switch on `data[0]`, bounds check for 0xF1 (requires 6 bytes), unknown pattern type guard
- JSON parser: uses WLED's shared `requestJSONBufferLock / releaseJSONBufferLock` to avoid heap fragmentation
- Lenient key policy: unknown top-level keys silently ignored IF at least one known key is present; pure-unknown payloads are dropped
- Replay ring: 16-slot circular buffer, tracks `cmd_id` uint32 values; check-then-track approach
- `_processRxQueue()` dequeues one slot per call and returns (bounded work guarantee)
- Test file: `usermods/loradmx/tests/test_parser.md` — documents all test vectors, error table, and replay wrap test

---

## Out of Scope

- Applying commands to WLED (MVP-008)
- Saving last command to cfg.json
