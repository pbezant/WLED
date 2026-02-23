# MVP-007 — Command Parser

| Field | Value |
|-------|-------|
| **ID** | MVP-007 |
| **Title** | Command Parser: Binary and JSON Downlink Decoder |
| **Role** | Coder |
| **Status** | not-started |
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

- [ ] All 15+ test vectors from [08-command-spec.md](../08-command-spec.md) produce correct `LoraDmxCommand` values
- [ ] Unknown binary byte increments `dropped` and returns `CMD_DROP`
- [ ] Duplicate `cmd_id` increments `replayed` and returns `CMD_DROP`
- [ ] Malformed JSON increments `dropped` and returns `CMD_DROP`
- [ ] Parser processes one command per `loop()` call (bounded work)
- [ ] Unit test file created: `usermods/loradmx/tests/test_parser.cpp` (or equivalent)

---

## Out of Scope

- Applying commands to WLED (MVP-008)
- Saving last command to cfg.json
