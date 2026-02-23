/*
 * LoRa-DMX Usermod — Implementation
 *
 * This file covers:
 *   MVP-002  scaffold & registration
 *   MVP-003  SX1262 hardware init via lora_hardware_init() (SX126x-Arduino)
 *   MVP-004  LoRaWAN Class C join loop + Radio.IrqProcess() non-blocking poll
 *   MVP-005  on-device credential generation and persistence
 *   MVP-006  commissioning UI (devEUI formatted XX:XX:XX:XX:XX:XX:XX:XX in /json/info)
 *   MVP-008  WLED state mapper
 *   MVP-009  config schema (addToConfig / readFromConfig)
 *   MVP-010  diagnostics (addToJsonInfo / addToJsonState)
 *   MVP-011  uplink policy: 12-byte FPort 2 telemetry, min 60 s interval
 *   MVP-015  loop timing instrumentation (maxLoopUs / loopWarn)
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

// ─── Single-instance pointer for LoRaWAN C-style callbacks ──────────────────
static UsermodLoRaDMX* s_loraDmxInstance = nullptr;

// ─── Hex string → byte array helper ─────────────────────────────────────────
// hex: must be exactly outLen*2 chars; returns false on length mismatch.
static bool hexStrToBytes(const char* hex, uint8_t* out, size_t outLen) {
  if (!hex || strlen(hex) != outLen * 2) return false;
  for (size_t i = 0; i < outLen; i++) {
    auto fromHex = [](char c) -> uint8_t {
      if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
      if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
      if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
      return 0;
    };
    out[i] = (uint8_t)((fromHex(hex[i * 2]) << 4) | fromHex(hex[i * 2 + 1]));
  }
  return true;
}

// ─── LoRaWAN MAC callbacks (C-style, forwarded through s_loraDmxInstance) ───

static uint8_t s_getBatLevel() { return 254; }

static void s_getUniqueId(uint8_t* id) {
  if (!s_loraDmxInstance) return;
  hexStrToBytes(s_loraDmxInstance->_devEUI_cb(), id, 8);
}

static uint32_t s_getRandomSeed() { return (uint32_t)esp_random(); }

static void s_onRxData(lmh_app_data_t* appdata) {
  if (!s_loraDmxInstance || !appdata || appdata->buffsize == 0) return;
  s_loraDmxInstance->_pushDownlink(appdata->buffer, appdata->buffsize, appdata->port,
                                   (int16_t)appdata->rssi, (uint8_t)appdata->snr);
}

static void s_onJoined() {
  if (!s_loraDmxInstance) return;
  s_loraDmxInstance->_onJoinSuccess();
}

static void s_onJoinFailed() {
  if (!s_loraDmxInstance) return;
  s_loraDmxInstance->_onJoinFailed();
}

static void s_onConfirmClass(DeviceClass_t Class) {
  DEBUG_PRINTF("[LoRaDMX] Class confirmed: %c\n", (char)('A' + Class));
}

static void s_onUnconfFinished() { /* no-op */ }
static void s_onConfResult(bool /*result*/) { /* no-op */ }

static lmh_callback_t s_loraCallbacks = {
  .BoardGetBatteryLevel = s_getBatLevel,
  .BoardGetUniqueId     = s_getUniqueId,
  .BoardGetRandomSeed   = s_getRandomSeed,
  .lmh_RxData           = s_onRxData,
  .lmh_has_joined       = s_onJoined,
  .lmh_ConfirmClass     = s_onConfirmClass,
  .lmh_has_joined_failed= s_onJoinFailed,
  .lmh_unconf_finished  = s_onUnconfFinished,
  .lmh_conf_result      = s_onConfResult,
};

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::setup() {
  if (!_enabled) return;

  // Register instance pointer for C-style LoRaWAN callbacks
  s_loraDmxInstance = this;

  // Load or generate credentials first (MVP-005)
  if (!_loadCredentials()) {
    _generateCredentials();
  }

  // Allocate SX1262 pins with PinManager
  if (!_allocatePins()) {
    DEBUG_PRINTLN(F("[LoRaDMX] setup: pin allocation failed — another module has a conflict"));
    return;
  }

  // Radio hardware init (MVP-003). lora_hardware_init() internally calls
  // SPI_LORA.begin(sck, miso, mosi, nss) using the pins in hw_config, so we
  // do NOT call _loraSPI.begin() here — the library owns the SPI bus init.
  _initRadio();

  if (_radioReady) {
    // Trigger first join attempt on the very next loop() call (unsigned underflow
    // ensures now - _lastJoinAttemptMs >= _joinRetryInterval immediately).
    _lastJoinAttemptMs = millis() - _joinRetryInterval;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
//
// Called every ~1ms by WLED. Must never block or call delay().
// Each call does ONE bounded unit of work then returns.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::loop() {
  if (!_enabled || !_radioReady) return;

  // MVP-015: loop time budget instrumentation
  uint32_t loopStartUs = micros();

  uint32_t now = millis();

  // Single-iteration do-while so any sub-section can `break` to reach timing
  do {
    // ── Join retry (MVP-004) ────────────────────────────────────────────────
    if (_joinState == LoraDmxJoinState::NotJoined ||
        _joinState == LoraDmxJoinState::JoinFailed) {
      if (now - _lastJoinAttemptMs >= _joinRetryInterval) {
        _lastJoinAttemptMs = now;
        _attemptJoin();
        break;
      }
    }

    // ── Radio event polling (MVP-004) ──────────────────────────────────────
    // Non-blocking: process one pending radio IRQ (DIO1) per loop() call.
    Radio.IrqProcess();

    // ── Process one queued downlink (MVP-007) ──────────────────────────────
    if (_rxCount > 0) {
      _processRxQueue();
      break;
    }

    // ── Uplink timer (MVP-011) ─────────────────────────────────────────────
    if (_joinState == LoraDmxJoinState::Joined) {
      if (now - _lastUplinkMs >= _uplinkInterval) {
        _sendUplink();
        _lastUplinkMs = now;
      }
    }
  } while (false);

  // Update worst-case timing; set _loopWarn if > 2ms threshold
  uint32_t elapsed = micros() - loopStartUs;
  if (elapsed > _maxLoopUs) _maxLoopUs = elapsed;
  if (elapsed > 2000 && !_loopWarn) {
    _loopWarn = true;
    DEBUG_PRINTF("[LoRaDMX] loop() overrun: %lu us\n", elapsed);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// addToJsonInfo()   — contributes to GET /json/info → u.LoRaDMX
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::addToJsonInfo(JsonObject& root) {
  JsonObject user = root["u"];
  if (user.isNull()) user = root.createNestedObject("u");

  JsonObject obj = user.createNestedObject(FPSTR(_name));

  // Format devEUI / joinEUI as XX:XX:XX:XX:XX:XX:XX:XX (MVP-006)
  char devEuiFormatted[24] = {};
  char joinEuiFormatted[24] = {};
  auto fmtEUI = [](const char* src, char* out, size_t outLen) {
    if (strlen(src) == 16) {
      for (int i = 0; i < 8; i++) {
        out[i*3]   = src[i*2];
        out[i*3+1] = src[i*2+1];
        if (i < 7) out[i*3+2] = ':';
      }
    } else {
      strlcpy(out, src, outLen);
    }
  };
  fmtEUI(_devEUI,  devEuiFormatted,  sizeof(devEuiFormatted));
  fmtEUI(_joinEUI, joinEuiFormatted, sizeof(joinEuiFormatted));

  obj[F("devEUI")]               = devEuiFormatted;
  obj[F("joinEUI")]              = joinEuiFormatted;
  obj[F("joinState")]            = joinStateStr(_joinState);
  obj[F("credentialsProvisioned")] = _credentialsProvisioned;
  obj[F("version")]              = LORADMX_VERSION;
  obj[F("fCntUp")]               = _fCntUp;
  obj[F("fCntDown")]             = _fCntDown;
  obj[F("uptimeSeconds")]        = millis() / 1000UL;
  obj[F("dropped")]              = _dropped;
  obj[F("replayed")]             = _replayed;
  obj[F("loopWarn")]             = _loopWarn;
  obj[F("maxLoopUs")]            = _maxLoopUs;  // MVP-015 timing diagnostic

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
// _initRadio()  (MVP-003)
//
// Initializes the SX1262 hardware via SX126x-Arduino's lora_hardware_init().
// The library internally calls SPI_LORA.begin(sck, miso, mosi, nss) using
// the pin numbers in hw_config — we do NOT manage SPI directly.
//
// Heltec WiFi LoRa 32 V3 specifics:
//   - 32 MHz TCXO on DIO3, 1.8 V supply rail  -> USE_DIO3_TCXO = true, 1_8V
//   - DC-DC power regulator (not LDO)          -> USE_LDO = false
//   - No antenna switch via DIO2               -> USE_DIO2_ANT_SWITCH = false
//   - No external TXEN/RXEN lines              -> RADIO_TXEN/RXEN = -1
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_initRadio() {
  hw_config hwConfig;
  hwConfig.CHIP_TYPE           = SX1262_CHIP;
  hwConfig.PIN_LORA_RESET      = _pinRst;
  hwConfig.PIN_LORA_NSS        = _pinNss;
  hwConfig.PIN_LORA_SCLK       = _pinSck;
  hwConfig.PIN_LORA_MISO       = _pinMiso;
  hwConfig.PIN_LORA_DIO_1      = _pinDio1;
  hwConfig.PIN_LORA_BUSY       = _pinBusy;
  hwConfig.PIN_LORA_MOSI       = _pinMosi;
  hwConfig.RADIO_TXEN          = -1;
  hwConfig.RADIO_RXEN          = -1;
  hwConfig.USE_DIO2_ANT_SWITCH = false;
  hwConfig.USE_DIO3_TCXO       = true;
  hwConfig.USE_DIO3_ANT_SWITCH = false;
  hwConfig.USE_LDO             = false;
  hwConfig.USE_RXEN_ANT_PWR    = false;
  hwConfig.TCXO_CTRL_VOLTAGE   = TCXO_CTRL_1_8V;

  uint32_t result = lora_hardware_init(hwConfig);
  if (result != 0) {
    DEBUG_PRINTF("[LoRaDMX] Radio init FAILED (err=%lu)\n", result);
    _radioReady = false;
    return;
  }
  DEBUG_PRINTLN(F("[LoRaDMX] Radio init OK"));
  _radioReady = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// _attemptJoin()  (MVP-004)
//
// On first call: initializes the LoRaWAN stack (lmh_init), sets US915
// subband 2 channel mask, then calls lmh_join().
// On retry calls: just calls lmh_join() again (stack already initialized).
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_attemptJoin() {
  if (!_lorawanInitDone) {
    // Parse credential strings to byte arrays
    uint8_t devEui[8]  = {};
    uint8_t joinEui[8] = {};
    uint8_t appKey[16] = {};

    if (!hexStrToBytes(_devEUI,  devEui,  8) ||
        !hexStrToBytes(_joinEUI, joinEui, 8) ||
        !hexStrToBytes(_appKey,  appKey,  16)) {
      DEBUG_PRINTLN(F("[LoRaDMX] _attemptJoin: invalid credentials — run provisioning"));
      _joinState = LoraDmxJoinState::JoinFailed;
      return;
    }

    lmh_setDevEui(devEui);
    lmh_setAppEui(joinEui);
    lmh_setAppKey(appKey);

    static lmh_param_t lmhParam = {
      .adr_enable          = LORAWAN_ADR_OFF,
      .tx_data_rate        = DR_4,       // SF8BW500 — Helium-recommended for US915
      .enable_public_network = LORAWAN_PUBLIC_NETWORK,
      .nb_trials           = 3,
      .tx_power            = TX_POWER_0,
      .duty_cycle          = LORAWAN_DUTYCYCLE_OFF,
    };

    lmh_error_status err = lmh_init(&s_loraCallbacks, lmhParam,
                                     true /*otaa*/, CLASS_C,
                                     LORAMAC_REGION_US915);
    if (err != LMH_SUCCESS) {
      DEBUG_PRINTF("[LoRaDMX] lmh_init failed (%d)\n", (int)err);
      _joinState = LoraDmxJoinState::JoinFailed;
      return;
    }

    // US915 subband 2 — channels 8-15 (uplink) + 65 (500 kHz uplink)
    lmh_setSubBandChannels(2);

    _lorawanInitDone = true;
    DEBUG_PRINTLN(F("[LoRaDMX] LoRaWAN stack init OK, sending JoinRequest"));
  }

  _joinState = LoraDmxJoinState::Joining;
  lmh_join();
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback forwarding methods (called from static C-style callbacks above)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_pushDownlink(const uint8_t* buf, uint8_t len,
                                    uint8_t fport, int16_t rssi, uint8_t snr) {
  _rssi = (float)rssi;
  _snr  = (float)(int8_t)snr;
  _fCntDown++;

  if (_rxCount >= LORADMX_RX_RING_SIZE) {
    // Ring full — drop oldest slot
    _rxTail   = (_rxTail + 1) % LORADMX_RX_RING_SIZE;
    _rxCount--;
    _overflow++;
  }

  LoraDmxRxEntry& entry = _rxBuf[_rxHead];
  uint8_t copyLen = (len > LORADMX_PAYLOAD_MAX) ? LORADMX_PAYLOAD_MAX : len;
  memcpy(entry.payload, buf, copyLen);
  entry.len   = copyLen;
  entry.fport = fport;
  entry.valid = true;
  _rxHead   = (_rxHead + 1) % LORADMX_RX_RING_SIZE;
  _rxCount++;

  DEBUG_PRINTF("[LoRaDMX] Downlink queued fport=%u len=%u rssi=%d snr=%d\n",
               fport, copyLen, rssi, (int8_t)snr);
}

void UsermodLoRaDMX::_onJoinSuccess() {
  DEBUG_PRINTLN(F("[LoRaDMX] Joined network — Class C active"));
  _joinState = LoraDmxJoinState::Joined;
}

void UsermodLoRaDMX::_onJoinFailed() {
  DEBUG_PRINTLN(F("[LoRaDMX] Join failed"));
  _joinState = LoraDmxJoinState::JoinFailed;
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

  // For Segment commands the raw JSON payload must drive WLED's deserializeState()
  // directly.  Do this before invalidating the ring slot so the payload buffer
  // is still valid, then skip the generic _applyCommand() call.
  if (cmd.type == LoraDmxCmdType::Segment) {
    _applySegmentJson(entry.payload, entry.len);
    strlcpy(_lastCmdResult, "ok", sizeof(_lastCmdResult));
    // fall through to ring advance + return
    entry.valid = false;
    _rxTail  = (_rxTail + 1) % LORADMX_RX_RING_SIZE;
    _rxCount--;
    _lastDownlinkMs = millis();
    return;
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
// _applySegmentJson()  (MVP-008)
//
// Routes a raw JSON segment payload (e.g. {"seg":[{"id":0,"col":...}]}) through
// WLED's deserializeState() directly, bypassing the LoraDmxCommand struct.
// Called from _processRxQueue() BEFORE the ring slot is invalidated so that
// entry.payload is still valid.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_applySegmentJson(const uint8_t* data, uint16_t len) {
  if (!requestJSONBufferLock(USERMOD_ID_LORADMX)) {
    _dropped++;
    return;
  }
  DeserializationError err = deserializeJson(*pDoc, data, len);
  if (err) {
    releaseJSONBufferLock();
    _dropped++;
    strlcpy(_lastCmdResult, "parse_error", sizeof(_lastCmdResult));
    return;
  }
  JsonObject root = pDoc->as<JsonObject>();
  deserializeState(root, CALL_MODE_DIRECT_CHANGE, 0);
  releaseJSONBufferLock();
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
      // Segment commands are handled in _processRxQueue() via _applySegmentJson()
      // before _applyCommand() is called.  This case is unreachable for Segment
      // payloads but kept as a safety no-op.
      break;

    default:
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// _sendUplink()  (MVP-011)
//
// Builds a 12-byte status payload on FPort 2 and transmits it via lmh_send().
// The minimum configured interval is clamped to 60 s in readFromConfig().
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaDMX::_sendUplink() {
  uint8_t payload[12];

  uint8_t flags = 0;
  if (bri > 0)                                   flags |= 0x01;  // bit0: on
  if (_joinState == LoraDmxJoinState::Joined)    flags |= 0x02;  // bit1: joined

  uint8_t fxId = 0;
  if (strip.getSegmentsNum() > 0) fxId = (uint8_t)strip.getSegment(0).mode;

  uint16_t droppedClamped  = (uint16_t)min((uint32_t)0xFFFF, _dropped);
  uint16_t replayedClamped = (uint16_t)min((uint32_t)0xFFFF, _replayed);
  uint16_t fCntDownLow     = (uint16_t)(_fCntDown & 0xFFFF);
  uint8_t  rssiAbs         = (uint8_t)min((uint32_t)255, (uint32_t)abs((int)_rssi));
  int8_t   snrX4           = (int8_t)((int)(_snr * 4.0f));

  payload[0]  = 0x01;                               // version
  payload[1]  = flags;
  payload[2]  = bri;
  payload[3]  = fxId;
  payload[4]  = (uint8_t)(droppedClamped & 0xFF);
  payload[5]  = (uint8_t)(droppedClamped >> 8);
  payload[6]  = (uint8_t)(replayedClamped & 0xFF);
  payload[7]  = (uint8_t)(replayedClamped >> 8);
  payload[8]  = (uint8_t)(fCntDownLow & 0xFF);
  payload[9]  = (uint8_t)(fCntDownLow >> 8);
  payload[10] = rssiAbs;
  payload[11] = (uint8_t)snrX4;

  lmh_app_data_t txData;
  txData.buffer   = payload;
  txData.buffsize = sizeof(payload);
  txData.port     = 2;
  txData.rssi     = 0;
  txData.snr      = 0;

  lmh_error_status status = lmh_send(&txData, LMH_UNCONFIRMED_MSG);
  if (status == LMH_SUCCESS) {
    DEBUG_PRINTLN(F("[LoRaDMX] Uplink TX: FPort=2 len=12"));
    _fCntUp++;
  } else {
    DEBUG_PRINTF("[LoRaDMX] Uplink TX failed (%d)\n", (int)status);
  }
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
