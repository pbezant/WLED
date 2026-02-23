/*
 * LoRa-DMX Usermod — Implementation
 *
 * This file covers:
 *   MVP-002  scaffold & registration
 *   MVP-003  SPI2/SX1262 hardware init (stubs — radio library integration in MVP-004)
 *   MVP-005  on-device credential generation and persistence
 *   MVP-008  WLED state mapper
 *   MVP-009  config schema (addToConfig / readFromConfig)
 *   MVP-010  diagnostics (addToJsonInfo / addToJsonState)
 *
 * Radio loop (MVP-004), command parser (MVP-007), and uplink (MVP-011)
 * are stubbed here and will be completed in their respective tickets.
 */

#include "wled.h"
#include "usermod_loradmx.h"

// ─── Config key string constants ────────────────────────────────────────────
const char UsermodLoRaDMX::_name[]             PROGMEM = "LoRaDMX";
const char UsermodLoRaDMX::_keyEnabled[]       PROGMEM = "enabled";
const char UsermodLoRaDMX::_keyDevEUI[]        PROGMEM = "devEUI";
const char UsermodLoRaDMX::_keyJoinEUI[]       PROGMEM = "joinEUI";
const char UsermodLoRaDMX::_keyAppKey[]        PROGMEM = "appKey";
const char UsermodLoRaDMX::_keyCredProv[]      PROGMEM = "credentialsProvisioned";
const char UsermodLoRaDMX::_keyUplinkInterval[] PROGMEM = "uplinkInterval";
const char UsermodLoRaDMX::_keyJoinRetry[]     PROGMEM = "joinRetryInterval";
const char UsermodLoRaDMX::_keyCmdThrottle[]   PROGMEM = "cmdThrottleMs";

// ─── Pattern → WLED fx ID mapping ───────────────────────────────────────────
static const uint8_t PATTERN_FX_MAP[] = {
  0,   // 0x00 — unused
  9,   // 0x01 — rainbow
  30,  // 0x02 — strobe
  43,  // 0x03 — chase
  7,   // 0x04 — colorFade
  45,  // 0x05 — alternate
};
static const uint8_t PATTERN_FX_MAP_SIZE = sizeof(PATTERN_FX_MAP);

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::setup() {
  if (!_enabled) return;

  // Load or generate credentials first (MVP-005)
  if (!_loadCredentials()) {
    _generateCredentials();
  }

  // Allocate SX1262 pins with PinManager
  if (!_allocatePins()) {
    DEBUG_PRINTLN(F("[LoRaDMX] setup: pin allocation failed — another module has a conflict"));
    return;
  }

  // Initialise SPI2 bus dedicated to SX1262
  _loraSPI.begin(_pinSck, _pinMiso, _pinMosi, _pinNss);
  DEBUG_PRINTLN(F("[LoRaDMX] SPI2 (FSPI) initialised"));

  // Radio hardware init — implementation ticket MVP-003
  _initRadio();
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
//
// Called every ~1ms by WLED. Must never block or call delay().
// Each call does ONE bounded unit of work then returns.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::loop() {
  if (!_enabled || !_radioReady) return;

  uint32_t now = millis();

  // ── Join retry (MVP-004) ──────────────────────────────────────────────────
  if (_joinState == LoraDmxJoinState::NotJoined ||
      _joinState == LoraDmxJoinState::JoinFailed) {
    if (now - _lastJoinAttemptMs >= _joinRetryInterval) {
      _lastJoinAttemptMs = now;
      _attemptJoin();       // non-blocking stub — replaced by radio lib callback in MVP-004
      return;
    }
  }

  // ── Radio event polling (MVP-004) ────────────────────────────────────────
  // TODO(MVP-004): call LoraManager2 non-blocking process() here
  // e.g.:  loraManager.process();

  // ── Process one queued downlink (MVP-007) ────────────────────────────────
  if (_rxCount > 0) {
    _processRxQueue();
    return;
  }

  // ── Uplink timer (MVP-011) ───────────────────────────────────────────────
  if (_joinState == LoraDmxJoinState::Joined) {
    if (now - _lastUplinkMs >= _uplinkInterval) {
      _sendUplink();      // stub — implemented in MVP-011
      _lastUplinkMs = now;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// addToJsonInfo()   — contributes to GET /json/info → u.LoRaDMX
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::addToJsonInfo(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonObject obj = user.createNestedObject(FPSTR(_name));

  obj[F("devEUI")]               = _devEUI;
  obj[F("joinEUI")]              = _joinEUI;
  obj[F("joinState")]            = joinStateStr(_joinState);
  obj[F("credentialsProvisioned")] = _credentialsProvisioned;
  obj[F("version")]              = LORADMX_VERSION;
  obj[F("fCntUp")]               = _fCntUp;
  obj[F("fCntDown")]             = _fCntDown;
  obj[F("uptimeSeconds")]        = millis() / 1000UL;
  obj[F("dropped")]              = _dropped;
  obj[F("replayed")]             = _replayed;
  obj[F("loopWarn")]             = _loopWarn;

  // rssi / snr — null when not joined
  if (_joinState == LoraDmxJoinState::Joined) {
    obj[F("rssi")] = _rssi;
    obj[F("snr")]  = _snr;
    uint32_t now = millis();
    obj[F("lastUplink")]   = (_lastUplinkMs   > 0) ? (now - _lastUplinkMs)   / 1000UL : 0;
    obj[F("lastDownlink")] = (_lastDownlinkMs > 0) ? (now - _lastDownlinkMs) / 1000UL : 0;
  } else {
    obj[F("rssi")]         = nullptr;
    obj[F("snr")]          = nullptr;
    obj[F("lastUplink")]   = nullptr;
    obj[F("lastDownlink")] = nullptr;
  }
  // appKey is intentionally omitted — write-only
}

// ─────────────────────────────────────────────────────────────────────────────
// addToJsonState()  — contributes to GET /json/state → u.LoRaDMX
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::addToJsonState(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonObject obj = user.createNestedObject(FPSTR(_name));
  obj[F("enabled")]        = _enabled;
  obj[F("joinState")]      = joinStateStr(_joinState);
  obj[F("lastCmdResult")]  = _lastCmdResult;
  obj[F("dropped")]        = _dropped;
  obj[F("replayed")]       = _replayed;
  obj[F("overflow")]       = _overflow;
}

// ─────────────────────────────────────────────────────────────────────────────
// getUserInput()   — handles POST /json/state writes (e.g. loraCmd for local test)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::readFromJsonState(JsonObject& root) {
  if (!_enabled) return;

  // Support loraCmd for in-field testing without a LoRa gateway
  if (!root[F("loraCmd")].isNull()) {
    String cmdStr;
    serializeJson(root[F("loraCmd")], cmdStr);
    // Re-encode as byte payload and inject into rx queue
    uint16_t len = (uint16_t)min((int)cmdStr.length(), LORADMX_PAYLOAD_MAX);
    if (_rxCount < LORADMX_RX_RING_SIZE) {
      LoraDmxRxEntry& entry = _rxBuf[_rxHead];
      memcpy(entry.payload, cmdStr.c_str(), len);
      entry.len   = len;
      entry.fport = 1;
      entry.valid = true;
      _rxHead = (_rxHead + 1) % LORADMX_RX_RING_SIZE;
      _rxCount++;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// addToConfig()   — writes usermod settings into cfg.json under um.LoRaDMX
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::addToConfig(JsonObject& root) {
  JsonObject um = root["um"];
  if (um.isNull()) um = root.createNestedObject("um");

  JsonObject obj = um.createNestedObject(FPSTR(_name));
  obj[FPSTR(_keyEnabled)]        = _enabled;
  obj[FPSTR(_keyDevEUI)]         = _devEUI;
  obj[FPSTR(_keyJoinEUI)]        = _joinEUI;
  obj[FPSTR(_keyAppKey)]         = _appKey;   // stored to disk; never returned via addToJsonInfo
  obj[FPSTR(_keyCredProv)]       = _credentialsProvisioned;
  obj[FPSTR(_keyUplinkInterval)] = _uplinkInterval;
  obj[FPSTR(_keyJoinRetry)]      = _joinRetryInterval;
  obj[FPSTR(_keyCmdThrottle)]    = _cmdThrottleMs;

  // SPI pins (allow user override via settings)
  obj[F("pinSck")]  = _pinSck;
  obj[F("pinMiso")] = _pinMiso;
  obj[F("pinMosi")] = _pinMosi;
  obj[F("pinNss")]  = _pinNss;
  obj[F("pinRst")]  = _pinRst;
  obj[F("pinBusy")] = _pinBusy;
  obj[F("pinDio1")] = _pinDio1;
}

// ─────────────────────────────────────────────────────────────────────────────
// readFromConfig()  — loads settings from cfg.json; returns false if required
//                    keys are absent (triggers credential re-gen on next setup)
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaDMX::readFromConfig(JsonObject& root) {
  JsonObject um = root["um"];
  if (um.isNull()) return false;

  JsonObject obj = um[FPSTR(_name)];
  if (obj.isNull()) return false;

  bool allFound = true;

  if (!obj[FPSTR(_keyEnabled)].isNull())        _enabled              = obj[FPSTR(_keyEnabled)];
  if (!obj[FPSTR(_keyUplinkInterval)].isNull()) _uplinkInterval       = obj[FPSTR(_keyUplinkInterval)];
  if (!obj[FPSTR(_keyJoinRetry)].isNull())      _joinRetryInterval    = obj[FPSTR(_keyJoinRetry)];
  if (!obj[FPSTR(_keyCmdThrottle)].isNull())    _cmdThrottleMs        = obj[FPSTR(_keyCmdThrottle)];

  // Enforce minimum uplink interval (duty cycle)
  if (_uplinkInterval < 60000) _uplinkInterval = 60000;

  // SPI pins
  if (!obj[F("pinSck")].isNull())  _pinSck  = obj[F("pinSck")];
  if (!obj[F("pinMiso")].isNull()) _pinMiso = obj[F("pinMiso")];
  if (!obj[F("pinMosi")].isNull()) _pinMosi = obj[F("pinMosi")];
  if (!obj[F("pinNss")].isNull())  _pinNss  = obj[F("pinNss")];
  if (!obj[F("pinRst")].isNull())  _pinRst  = obj[F("pinRst")];
  if (!obj[F("pinBusy")].isNull()) _pinBusy = obj[F("pinBusy")];
  if (!obj[F("pinDio1")].isNull()) _pinDio1 = obj[F("pinDio1")];

  // Credentials (required fields)
  if (!obj[FPSTR(_keyDevEUI)].isNull() && !obj[FPSTR(_keyAppKey)].isNull()) {
    strlcpy(_devEUI, obj[FPSTR(_keyDevEUI)] | "", sizeof(_devEUI));
    strlcpy(_joinEUI, obj[FPSTR(_keyJoinEUI)] | "0000000000000000", sizeof(_joinEUI));
    strlcpy(_appKey, obj[FPSTR(_keyAppKey)] | "", sizeof(_appKey));
    _credentialsProvisioned = obj[FPSTR(_keyCredProv)] | false;
  } else {
    allFound = false;
  }

  // Handle credential reset request
  if (!obj[F("resetCredentials")].isNull() && (bool)obj[F("resetCredentials")]) {
    DEBUG_PRINTLN(F("[LoRaDMX] Credential reset requested — regenerating"));
    _credentialsProvisioned = false;
    _generateCredentials();
    doReboot = true;  // reboot so new credentials take effect
  }

  return allFound;
}

// ─────────────────────────────────────────────────────────────────────────────
// _generateCredentials()  (MVP-005)
//
// Derives devEUI from eFuse MAC and generates a random appKey.
// Printed to serial ONE TIME only. Persisted via addToConfig / saveAllLEDSettings.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_generateCredentials() {
  // devEUI: 64-bit eFuse MAC, big-endian hex (reversed byte order from getEfuseMac)
  uint64_t mac = ESP.getEfuseMac();
  snprintf(_devEUI, sizeof(_devEUI), "%02X%02X%02X%02X%02X%02X%02X%02X",
    (uint8_t)((mac >> 56) & 0xFF),
    (uint8_t)((mac >> 48) & 0xFF),
    (uint8_t)((mac >> 40) & 0xFF),
    (uint8_t)((mac >> 32) & 0xFF),
    (uint8_t)((mac >> 24) & 0xFF),
    (uint8_t)((mac >> 16) & 0xFF),
    (uint8_t)((mac >>  8) & 0xFF),
    (uint8_t)( mac        & 0xFF)
  );

  // joinEUI: all-zeros (accepted by Chirpstack/TTN for OTAA)
  strlcpy(_joinEUI, "0000000000000000", sizeof(_joinEUI));

  // appKey: 128 bits from hardware RNG
  uint32_t k[4];
  k[0] = esp_random();
  k[1] = esp_random();
  k[2] = esp_random();
  k[3] = esp_random();
  snprintf(_appKey, sizeof(_appKey), "%08X%08X%08X%08X", k[0], k[1], k[2], k[3]);

  _credentialsProvisioned = true;

  // Print ONCE to serial — installer must capture these here
  Serial.println(F("\n[LoRaDMX] *** First boot — credentials generated ***"));
  Serial.print(F("[LoRaDMX]   devEUI : ")); Serial.println(_devEUI);
  Serial.print(F("[LoRaDMX]   appKey : ")); Serial.println(_appKey);
  Serial.print(F("[LoRaDMX]   joinEUI: ")); Serial.println(_joinEUI);
  Serial.println(F("[LoRaDMX] Register device on your LNS before requesting join.\n"));

  // Persist immediately
  serializeConfigToFS();
}

// ─────────────────────────────────────────────────────────────────────────────
// _loadCredentials()
//
// Returns true if valid credentials are already stored in cfg.json.
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaDMX::_loadCredentials() {
  // readFromConfig() is called before setup() by the WLED lifecycle, so by the
  // time we arrive here _devEUI/_appKey are already populated if they existed.
  if (!_credentialsProvisioned) return false;
  if (strlen(_devEUI) != 16)    return false;
  if (strlen(_appKey) != 32)    return false;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// _allocatePins()  (MVP-003)
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaDMX::_allocatePins() {
  PinManagerPinType spiPins[] = {
    { _pinSck,  true  },
    { _pinMiso, false },
    { _pinMosi, true  },
    { _pinNss,  true  },
    { _pinRst,  true  },
    { _pinBusy, false },
    { _pinDio1, false },
  };
  return PinManager::allocateMultiplePins(spiPins, sizeof(spiPins) / sizeof(spiPins[0]),
                                          PinOwner::UM_Unspecified);
}

void UsermodLoRaDMX::_deallocatePins() {
  int8_t pins[] = { _pinSck, _pinMiso, _pinMosi, _pinNss, _pinRst, _pinBusy, _pinDio1 };
  for (int8_t p : pins) PinManager::deallocatePin(p, PinOwner::UM_Unspecified);
}

// ─────────────────────────────────────────────────────────────────────────────
// _initRadio()  (MVP-003 stub — radio library wired in MVP-004)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_initRadio() {
  // TODO(MVP-003 / MVP-004): Instantiate SX126xRadio / LoraManager2 here.
  // Use _loraSPI (not default SPI), pins from _pinNss/_pinRst/_pinBusy/_pinDio1.
  // Set _radioReady = true on success, false on failure.
  DEBUG_PRINTLN(F("[LoRaDMX] _initRadio: stub — implement in MVP-003/004"));
  _radioReady = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// _attemptJoin()  (MVP-004 stub)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_attemptJoin() {
  // TODO(MVP-004): call LoraManager2 begin/join.
  DEBUG_PRINTLN(F("[LoRaDMX] _attemptJoin: stub"));
  _joinState = LoraDmxJoinState::Joining;
}

// ─────────────────────────────────────────────────────────────────────────────
// _processRxQueue()
//
// Dequeues ONE downlink entry per loop() call, parses it, and applies it.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_processRxQueue() {
  if (_rxCount == 0) return;

  LoraDmxRxEntry& entry = _rxBuf[_rxTail];
  if (!entry.valid) {
    _rxTail = (_rxTail + 1) % LORADMX_RX_RING_SIZE;
    _rxCount--;
    return;
  }

  LoraDmxCommand cmd;
  if (entry.len > 0 && entry.payload[0] == '{') {
    cmd = _parseJSON(entry.payload, entry.len);
  } else {
    cmd = _parseBinary(entry.payload, entry.len);
  }

  entry.valid = false;
  _rxTail  = (_rxTail + 1) % LORADMX_RX_RING_SIZE;
  _rxCount--;
  _lastDownlinkMs = millis();

  if (cmd.type != LoraDmxCmdType::Drop) {
    uint32_t now = millis();
    if (now - _lastCmdMs >= _cmdThrottleMs) {
      _lastCmdMs = now;
      _applyCommand(cmd);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// _parseBinary()  (MVP-007)
// ─────────────────────────────────────────────────────────────────────────────
LoraDmxCommand UsermodLoRaDMX::_parseBinary(const uint8_t* data, uint16_t len) {
  LoraDmxCommand cmd;
  if (len == 0) {
    _dropped++;
    strlcpy(_lastCmdResult, "parse_error", sizeof(_lastCmdResult));
    return cmd;
  }

  switch (data[0]) {
    case 0x00:
      cmd.type = LoraDmxCmdType::Power;
      cmd.on   = false;
      break;
    case 0x01: cmd.type = LoraDmxCmdType::ColorNamed; cmd.r=255; cmd.g=0;   cmd.b=0;   break;
    case 0x02: cmd.type = LoraDmxCmdType::ColorNamed; cmd.r=0;   cmd.g=255; cmd.b=0;   break;
    case 0x03: cmd.type = LoraDmxCmdType::ColorNamed; cmd.r=0;   cmd.g=0;   cmd.b=255; break;
    case 0x04: cmd.type = LoraDmxCmdType::ColorNamed; cmd.r=255; cmd.g=255; cmd.b=255; break;
    case 0xAA: cmd.type = LoraDmxCmdType::Test;       cmd.r=0;   cmd.g=255; cmd.b=0;   break;
    case 0xF0:
      cmd.type = LoraDmxCmdType::PatternStop;
      break;
    case 0xF1:
      if (len < 6) { _dropped++; strlcpy(_lastCmdResult, "parse_error", sizeof(_lastCmdResult)); return cmd; }
      cmd.type        = LoraDmxCmdType::PatternStart;
      cmd.patternType = data[1];
      cmd.speed       = (uint16_t)(data[2]) | ((uint16_t)(data[3]) << 8);
      cmd.cycles      = (uint16_t)(data[4]) | ((uint16_t)(data[5]) << 8);
      if (cmd.patternType == 0 || cmd.patternType >= PATTERN_FX_MAP_SIZE) {
        _dropped++;
        strlcpy(_lastCmdResult, "drop", sizeof(_lastCmdResult));
        cmd.type = LoraDmxCmdType::Drop;
        return cmd;
      }
      break;
    default:
      _dropped++;
      strlcpy(_lastCmdResult, "drop", sizeof(_lastCmdResult));
      return cmd;   // type remains Drop
  }

  strlcpy(_lastCmdResult, "ok", sizeof(_lastCmdResult));
  return cmd;
}

// ─────────────────────────────────────────────────────────────────────────────
// _parseJSON()   (MVP-007)
// ─────────────────────────────────────────────────────────────────────────────
LoraDmxCommand UsermodLoRaDMX::_parseJSON(const uint8_t* data, uint16_t len) {
  LoraDmxCommand cmd;

  // Request a shared JSON buffer from WLED
  if (!requestJSONBufferLock(USERMOD_ID_LORADMX)) {
    _dropped++;
    strlcpy(_lastCmdResult, "parse_error", sizeof(_lastCmdResult));
    return cmd;
  }

  DeserializationError err = deserializeJson(*pDoc, data, len);
  if (err) {
    releaseJSONBufferLock();
    _dropped++;
    strlcpy(_lastCmdResult, "parse_error", sizeof(_lastCmdResult));
    return cmd;
  }

  JsonObject root = pDoc->as<JsonObject>();

  // Replay protection
  if (!root[F("cmd_id")].isNull()) {
    cmd.cmdId = root[F("cmd_id")].as<uint32_t>();
    if (_isDuplicate(cmd.cmdId)) {
      releaseJSONBufferLock();
      _replayed++;
      strlcpy(_lastCmdResult, "drop", sizeof(_lastCmdResult));
      cmd.type = LoraDmxCmdType::Drop;
      return cmd;
    }
    _trackCmdId(cmd.cmdId);
  }

  // Parse known fields
  bool hasPreset = !root[F("preset")].isNull();
  bool hasOn     = !root[F("on")].isNull();
  bool hasBri    = !root[F("bri")].isNull();
  bool hasSeg    = !root[F("seg")].isNull();

  if (!hasPreset && !hasOn && !hasBri && !hasSeg) {
    releaseJSONBufferLock();
    _dropped++;
    strlcpy(_lastCmdResult, "drop", sizeof(_lastCmdResult));
    return cmd;
  }

  // Combined command if more than one field
  int fieldCount = (int)hasPreset + (int)hasOn + (int)hasBri + (int)hasSeg;
  if (fieldCount > 1) {
    cmd.type = LoraDmxCmdType::Combined;
  } else if (hasPreset) {
    cmd.type   = LoraDmxCmdType::Preset;
    cmd.preset = root[F("preset")].as<uint8_t>();
  } else if (hasOn) {
    cmd.type = LoraDmxCmdType::Power;
    cmd.on   = root[F("on")].as<bool>();
  } else if (hasBri) {
    cmd.type = LoraDmxCmdType::Brightness;
    cmd.bri  = root[F("bri")].as<uint8_t>();
  } else if (hasSeg) {
    cmd.type = LoraDmxCmdType::Segment;
    // Segment data is re-applied directly via WLED state API in _applyCommand
  }

  // For Combined, copy all fields
  if (cmd.type == LoraDmxCmdType::Combined) {
    if (hasPreset) cmd.preset = root[F("preset")].as<uint8_t>();
    if (hasOn)     cmd.on     = root[F("on")].as<bool>();
    if (hasBri)    cmd.bri    = root[F("bri")].as<uint8_t>();
  }

  releaseJSONBufferLock();
  strlcpy(_lastCmdResult, "ok", sizeof(_lastCmdResult));
  return cmd;
}

// ─────────────────────────────────────────────────────────────────────────────
// _applyCommand()   (MVP-008)
//
// Translates a parsed command into WLED state mutations.
// Uses internal WLED APIs only — no HTTP self-calls.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_applyCommand(const LoraDmxCommand& cmd) {
  switch (cmd.type) {

    case LoraDmxCmdType::Power:
      if (cmd.on) {
        if (briLast == 0) briLast = 128;
        bri = briLast;
      } else {
        bri = 0;
      }
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;

    case LoraDmxCmdType::ColorNamed:
    case LoraDmxCmdType::Test:
      bri = (bri == 0) ? 255 : bri;  // ensure not off
      for (uint8_t s = 0; s < strip.getSegmentsNum(); s++) {
        Segment& seg = strip.getSegment(s);
        seg.setColor(0, RGBW32(cmd.r, cmd.g, cmd.b, 0));
        seg.setMode(0, true);  // Solid
      }
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;

    case LoraDmxCmdType::PatternStart: {
      uint8_t fxId = PATTERN_FX_MAP[cmd.patternType];
      // Scale speed: 0-65535 → 0-255
      uint8_t sx = (uint8_t)((cmd.speed * 255UL) / 65535UL);
      for (uint8_t s = 0; s < strip.getSegmentsNum(); s++) {
        Segment& seg = strip.getSegment(s);
        seg.setMode(fxId, true);
        seg.speed = sx;
      }
      bri = (bri == 0) ? 255 : bri;
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;
    }

    case LoraDmxCmdType::PatternStop:
      for (uint8_t s = 0; s < strip.getSegmentsNum(); s++) {
        strip.getSegment(s).setMode(0, true);  // Solid
      }
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;

    case LoraDmxCmdType::Preset:
      if (!applyPreset(cmd.preset)) {
        strlcpy(_lastCmdResult, "preset_not_found", sizeof(_lastCmdResult));
        _dropped++;
      }
      break;

    case LoraDmxCmdType::Brightness:
      bri = cmd.bri;
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;

    case LoraDmxCmdType::Combined:
      if (cmd.on) {
        bri = (cmd.bri > 0) ? cmd.bri : ((briLast > 0) ? briLast : 255);
      } else {
        bri = 0;
      }
      if (cmd.preset > 0) applyPreset(cmd.preset);
      stateUpdated(CALL_MODE_DIRECT_CHANGE);
      break;

    case LoraDmxCmdType::Segment:
      // Segment commands: handled by re-forwarding the raw JSON through WLED's
      // deserializeState() — implemented in MVP-008 extension after JSON buffer
      // refactor is confirmed safe.
      DEBUG_PRINTLN(F("[LoRaDMX] Segment command — TODO(MVP-008 extension)"));
      break;

    default:
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// _sendUplink()  (MVP-011 stub)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_sendUplink() {
  // TODO(MVP-011): build 12-byte telemetry payload and call LoraManager2 send.
  DEBUG_PRINTLN(F("[LoRaDMX] _sendUplink: stub"));
  _fCntUp++;
}

// ─────────────────────────────────────────────────────────────────────────────
// Replay protection helpers
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaDMX::_isDuplicate(uint32_t cmdId) {
  for (uint8_t i = 0; i < REPLAY_RING_SIZE; i++) {
    if (_replayRing[i] == cmdId) return true;
  }
  return false;
}

void UsermodLoRaDMX::_trackCmdId(uint32_t cmdId) {
  _replayRing[_replayHead] = cmdId;
  _replayHead = (_replayHead + 1) % REPLAY_RING_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Module registration
// ─────────────────────────────────────────────────────────────────────────────
#ifdef USERMOD_LORADMX
static UsermodLoRaDMX loradmx_instance;
REGISTER_USERMOD(loradmx_instance);
#endif
