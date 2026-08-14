/*
 * LoRa-WLED Usermod — Implementation
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
#include "usermod_lorawled.h"

// ─── Config key string constants ────────────────────────────────────────────
const char UsermodLoRaWLED::_name[]             PROGMEM = "LoRaWLED";
const char UsermodLoRaWLED::_keyEnabled[]       PROGMEM = "enabled";
const char UsermodLoRaWLED::_keyDevEUI[]        PROGMEM = "devEUI";
const char UsermodLoRaWLED::_keyJoinEUI[]       PROGMEM = "joinEUI";
const char UsermodLoRaWLED::_keyAppKey[]        PROGMEM = "appKey";
const char UsermodLoRaWLED::_keyCredProv[]      PROGMEM = "credentialsProvisioned";
const char UsermodLoRaWLED::_keyUplinkInterval[] PROGMEM = "uplinkInterval";
const char UsermodLoRaWLED::_keyJoinRetry[]     PROGMEM = "joinRetryInterval";
const char UsermodLoRaWLED::_keyCmdThrottle[]   PROGMEM = "cmdThrottleMs";
const char UsermodLoRaWLED::_keyRegion[]        PROGMEM = "region";
const char UsermodLoRaWLED::_keySubBand[]       PROGMEM = "subBand";
const char UsermodLoRaWLED::_keyAdrEnable[]     PROGMEM = "adrEnable";
const char UsermodLoRaWLED::_keyTxDataRate[]    PROGMEM = "txDataRate";

// ─── Region table (M6-014) ──────────────────────────────────────────────────
// One row per supported region. Region, default data rate, sub-band
// applicability and duty-cycle obligation are four columns of the same fact —
// keeping them in one row makes an illegal or unjoinable combination
// unrepresentable.
//
// dutyCycleOn is NOT operator-settable anywhere. In EU868 and AS923 the duty
// cycle limit is enforced by regulation (ETSI EN 300 220 / ARIB STD-T108); in
// US915 and AU915 FCC part 15.247 governs dwell time instead, and the stack's
// duty-cycle limiter must stay off.
//
// maxDataRate is the highest uplink DR valid for the region (FSK/LR-FHSS rates
// excluded). A configured txDataRate above it is rejected in _attemptJoin().
//
// defaultDataRate must name a 125 kHz rate in the 64+8 channel plans (US915,
// AU915) — see M6-016. In those plans the channel a frame goes out on is chosen
// purely from the data rate: Channels[0..63] carry DR_0–DR_3 at 125 kHz and
// Channels[64..71] carry DR_4 (US915) / DR_6 (AU915) at 500 kHz, and nothing
// else. Pinning uplinks to the 500 kHz rate therefore pins them to eight
// frequencies spread across the whole band, of which an 8-channel gateway
// watches at most one — while OTAA still succeeds, because RegionAlternateDr()
// sends eight of every nine join trials at DR_0 on the 125 kHz channels the
// gateway does hear. US915 DR_1 is the lowest 125 kHz rate whose 53-byte
// payload ceiling clears the 12-byte status uplink; DR_0 caps at 11 bytes and
// LoRaMacQueryTxPossible() rejects the frame outright.
struct LoraRegionInfo {
  char           name[7];
  LoRaMacRegion_t macRegion;
  int8_t         defaultDataRate;
  int8_t         maxDataRate;
  uint8_t        defaultSubBand;   // 0 when the region has no sub-band mask
  bool           subBandApplies;
  bool           dutyCycleOn;
};

static const LoraRegionInfo LORA_REGION_TABLE[] PROGMEM = {
  // name      macRegion             defDR  maxDR  defSub  subBand  dutyCycle
  { "US915", LORAMAC_REGION_US915,  DR_1,  DR_4,   2,     true,    false },
  { "EU868", LORAMAC_REGION_EU868,  DR_5,  DR_5,   0,     false,   true  },
  { "AU915", LORAMAC_REGION_AU915,  DR_2,  DR_6,   2,     true,    false },
  { "AS923", LORAMAC_REGION_AS923,  DR_2,  DR_5,   0,     false,   true  },
};
static const uint8_t LORA_REGION_COUNT =
  sizeof(LORA_REGION_TABLE) / sizeof(LORA_REGION_TABLE[0]);

// Copies one region row out of PROGMEM. Out-of-range indices fall back to the
// default region rather than reading past the table.
static void loraRegionInfo(uint8_t idx, LoraRegionInfo& out) {
  if (idx >= LORA_REGION_COUNT) idx = (uint8_t)LORAWLED_REGION_DEFAULT;
  memcpy_P(&out, &LORA_REGION_TABLE[idx], sizeof(LoraRegionInfo));
}

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
static UsermodLoRaWLED* s_loraDmxInstance = nullptr;

// ─── Secret redaction for GET /json/cfg ─────────────────────────────────────
//
// WLED reuses addToConfig() for two very different jobs: writing cfg.json to
// flash, and answering GET /json/cfg — which is unauthenticated and readable by
// anything on the LAN or the WLED-AP. Core WLED keeps its own secrets out of
// the second (the WiFi PSK is reported as a length, the MQTT password as a run
// of asterisks) but usermods get no separate hook, so this has to make the same
// distinction itself. The only signal available is which module holds the
// shared JSON buffer: serializeConfigToFS() takes it as JSON_LOCK_CFG_SER,
// serveJson() as JSON_LOCK_SERVEJSON.
static bool loraCfgIsForFlash() {
  return jsonBufferLock == JSON_LOCK_CFG_SER;
}

// Same-length asterisk run, matching core WLED's MQTT-password convention, so
// the usermod settings page still shows that a key is set. Posting the mask
// back unchanged is recognised by isAsterisksOnly() in readFromConfig() and
// leaves the stored key alone.
static void loraMaskSecret(const char* src, char* out, size_t outLen) {
  size_t n = strlen(src);
  if (n > outLen - 1) n = outLen - 1;
  memset(out, '*', n);
  out[n] = '\0';
}

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
  DEBUG_PRINTF("[LoRaWLED] Class confirmed: %c\n", (char)('A' + Class));
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
void UsermodLoRaWLED::setup() {
  if (!_enabled) return;

  // Register instance pointer for C-style LoRaWAN callbacks
  s_loraDmxInstance = this;

  // Load or generate credentials first (MVP-005)
  if (!_loadCredentials()) {
    _generateCredentials();
  }

  // Allocate SX1262 pins with PinManager
  if (!_allocatePins()) {
    DEBUG_PRINTLN(F("[LoRaWLED] setup: pin allocation failed — another module has a conflict"));
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

    // Spawn dedicated TX task (MVP-016) — all blocking SPI radio work runs here
    // so the WLED main loop never stalls on the SX1262 BUSY pin.
    _loraTxSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(_loraTxTaskFn, "lora_tx", 4096, this,
                            /*priority=*/1, &_loraTxTask, /*core=*/1);
    DEBUG_PRINTLN(F("[LoRaWLED] lora_tx task spawned on core 1"));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
//
// Called every ~1ms by WLED. Must never block or call delay().
// Each call does ONE bounded unit of work then returns.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::loop() {
  if (!_enabled || !_radioReady) return;

  // M6-014: region/data-rate change needs a fresh lmh_init(). Calling it twice
  // in one boot is not supported by the stack, so reboot is the reset path.
  if (_radioConfigDirty) {
    _radioConfigDirty = false;
    doReboot = true;
    return;
  }

  // MVP-015: loop time budget instrumentation
  uint32_t loopStartUs = micros();

  uint32_t now = millis();

  // Single-iteration do-while so any sub-section can `break` to reach timing
  do {
    // ── Join watchdog ───────────────────────────────────────────────────────
    // The retry branch below only re-fires from NotJoined/JoinFailed, and the
    // only things that move us out of Joining are the radio stack's own
    // _onJoinSuccess/_onJoinFailed callbacks. A single lost DIO1 edge or
    // swallowed MAC event therefore parks the device in Joining forever: one
    // JoinRequest goes out, nothing comes back, and the link is dead until
    // someone power-cycles the board. Time the attempt out here so a retry is
    // always scheduled no matter what the stack does.
    if (_joinState == LoraDmxJoinState::Joining &&
        now - _joinStartedMs >= LORAWLED_JOIN_TIMEOUT_MS) {
      DEBUG_PRINTLN(F("[LoRaWLED] join timed out with no callback — retrying"));
      _joinState = LoraDmxJoinState::JoinFailed;
    }

    // ── Join retry (MVP-004) ────────────────────────────────────────────────
    if (_joinState == LoraDmxJoinState::NotJoined ||
        _joinState == LoraDmxJoinState::JoinFailed) {
      if (now - _lastJoinAttemptMs >= _joinRetryInterval) {
        _lastJoinAttemptMs = now;
        _attemptJoin();
        break;
      }
    }

    // ── Radio event polling moved to _loraTxTaskFn (MVP-016) ──────────────────
    // Radio.IrqProcess() is called from the dedicated lora_tx FreeRTOS task
    // every 10 ms to avoid blocking the WLED main loop on SPI/BUSY.

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
    DEBUG_PRINTF("[LoRaWLED] loop() overrun: %lu us\n", elapsed);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// addToJsonInfo()   — contributes to GET /json/info → u.LoRaWLED
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::addToJsonInfo(JsonObject& root) {
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

  // Regional radio config (M6-014) — duty cycle is shown as derived state so a
  // field report can be checked against the region without a serial console.
  {
    LoraRegionInfo region;
    loraRegionInfo(_region, region);
    obj[F("region")]     = region.name;
    obj[F("subBand")]    = region.subBandApplies
                             ? (_subBand ? _subBand : region.defaultSubBand) : 0;
    obj[F("dutyCycle")]  = region.dutyCycleOn;
    obj[F("adr")]        = _adrEnable;
    obj[F("dataRate")]   = _lorawanInitDone ? _effectiveDataRate
                                            : region.defaultDataRate;
  }

  // M6-016 uplink diagnostics. `fCntUp` counts frames this usermod handed to
  // the MAC; `fCntUpMac` is the MAC's own uplink counter, which is what the
  // network server's last_f_cnt_up should track. The two disagreeing points at
  // the usermod; both climbing while the NS stays at 0 points at the air.
  if (_lorawanInitDone) {
    MibRequestConfirm_t mibReq;
    mibReq.Type = MIB_UPLINK_COUNTER;
    if (LoRaMacMibGetRequestConfirm(&mibReq) == LORAMAC_STATUS_OK) {
      obj[F("fCntUpMac")] = mibReq.Param.UpLinkCounter;
    }
  }
  obj[F("classC")]     = _classC;
  obj[F("txErrors")]   = _txErrors;
  obj[F("lastTx")]     = _lastTxStatus;
  obj[F("credentialsProvisioned")] = _credentialsProvisioned;
  obj[F("version")]              = LORAWLED_VERSION;
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
// addToJsonState()  — contributes to GET /json/state → u.LoRaWLED
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::addToJsonState(JsonObject& root) {
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
void UsermodLoRaWLED::readFromJsonState(JsonObject& root) {
  if (!_enabled) return;

  // Support loraCmd for in-field testing without a LoRa gateway
  if (!root[F("loraCmd")].isNull()) {
    String cmdStr;
    serializeJson(root[F("loraCmd")], cmdStr);
    // Re-encode as byte payload and inject into rx queue
    uint16_t len = (uint16_t)min((int)cmdStr.length(), LORAWLED_PAYLOAD_MAX);
    if (_rxCount < LORAWLED_RX_RING_SIZE) {
      LoraDmxRxEntry& entry = _rxBuf[_rxHead];
      memcpy(entry.payload, cmdStr.c_str(), len);
      entry.len   = len;
      entry.fport = 1;
      entry.valid = true;
      _rxHead = (_rxHead + 1) % LORAWLED_RX_RING_SIZE;
      _rxCount++;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// addToConfig()   — writes usermod settings into cfg.json under um.LoRaWLED
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::addToConfig(JsonObject& root) {
  JsonObject um = root["um"];
  if (um.isNull()) um = root.createNestedObject("um");

  JsonObject obj = um.createNestedObject(FPSTR(_name));
  obj[FPSTR(_keyEnabled)]        = _enabled;
  obj[FPSTR(_keyDevEUI)]         = _devEUI;
  obj[FPSTR(_keyJoinEUI)]        = _joinEUI;
  // The AppKey is the device's only OTAA root secret — anyone holding it can
  // impersonate the device on the network. It goes to flash verbatim and to
  // /json/cfg masked.
  char appKeyOut[sizeof(_appKey)];
  if (loraCfgIsForFlash()) {
    strlcpy(appKeyOut, _appKey, sizeof(appKeyOut));
  } else {
    loraMaskSecret(_appKey, appKeyOut, sizeof(appKeyOut));
  }
  obj[FPSTR(_keyAppKey)]         = appKeyOut;
  obj[FPSTR(_keyCredProv)]       = _credentialsProvisioned;
  obj[FPSTR(_keyUplinkInterval)] = _uplinkInterval;
  obj[FPSTR(_keyJoinRetry)]      = _joinRetryInterval;
  obj[FPSTR(_keyCmdThrottle)]    = _cmdThrottleMs;

  // Regional radio config (M6-014). No duty-cycle field is written — duty cycle
  // is a property of the region, not a setting.
  obj[FPSTR(_keyRegion)]         = _region;
  obj[FPSTR(_keySubBand)]        = _subBand;
  obj[FPSTR(_keyAdrEnable)]      = _adrEnable;
  obj[FPSTR(_keyTxDataRate)]     = _txDataRate;

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
bool UsermodLoRaWLED::readFromConfig(JsonObject& root) {
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

  // ── Regional radio config (M6-014) ────────────────────────────────────────
  // An install written before this ticket has no `region` key. Report that as
  // incomplete config so WLED re-saves with the defaults written out, rather
  // than leaving the field silently unset.
  uint8_t prevRegion   = _region;
  uint8_t prevSubBand  = _subBand;
  int8_t  prevDataRate = _txDataRate;
  bool    prevAdr      = _adrEnable;

  if (obj[FPSTR(_keyRegion)].isNull()) {
    allFound = false;
  } else {
    uint8_t r = obj[FPSTR(_keyRegion)] | (uint8_t)LORAWLED_REGION_DEFAULT;
    _region = (r < LORA_REGION_COUNT) ? r : (uint8_t)LORAWLED_REGION_DEFAULT;
  }
  if (!obj[FPSTR(_keySubBand)].isNull())    _subBand    = obj[FPSTR(_keySubBand)];
  if (!obj[FPSTR(_keyAdrEnable)].isNull())  _adrEnable  = obj[FPSTR(_keyAdrEnable)];
  if (!obj[FPSTR(_keyTxDataRate)].isNull()) _txDataRate = obj[FPSTR(_keyTxDataRate)];

  // The LoRaWAN stack cannot be re-initialised in place, so a change after join
  // is flagged here and handled by loop() (see _radioConfigDirty).
  if (_lorawanInitDone &&
      (_region != prevRegion || _subBand != prevSubBand ||
       _txDataRate != prevDataRate || _adrEnable != prevAdr)) {
    DEBUG_PRINTLN(F("[LoRaWLED] radio config changed — reboot to re-init stack"));
    _radioConfigDirty = true;
  }

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
    char prevDevEUI[sizeof(_devEUI)];
    char prevJoinEUI[sizeof(_joinEUI)];
    char prevAppKey[sizeof(_appKey)];
    strlcpy(prevDevEUI,  _devEUI,  sizeof(prevDevEUI));
    strlcpy(prevJoinEUI, _joinEUI, sizeof(prevJoinEUI));
    strlcpy(prevAppKey,  _appKey,  sizeof(prevAppKey));

    strlcpy(_devEUI, obj[FPSTR(_keyDevEUI)] | "", sizeof(_devEUI));
    strlcpy(_joinEUI, obj[FPSTR(_keyJoinEUI)] | "0000000000000000", sizeof(_joinEUI));
    // An all-asterisk value is the mask addToConfig() served to /json/cfg being
    // posted straight back by the settings page — it means "unchanged". Storing
    // it would overwrite the real key with asterisks and permanently break the
    // join, so only a genuinely new value is taken.
    const char* appKeyIn = obj[FPSTR(_keyAppKey)] | "";
    if (!isAsterisksOnly(appKeyIn, sizeof(_appKey))) {
      strlcpy(_appKey, appKeyIn, sizeof(_appKey));
    }
    _credentialsProvisioned = obj[FPSTR(_keyCredProv)] | false;

    // Credentials reach the radio stack exactly once per boot: _attemptJoin()
    // calls lmh_setDevEui/setAppEui/setAppKey inside `if (!_lorawanInitDone)`,
    // and the stack cannot be re-initialised in place. So an operator who
    // pastes a corrected AppKey into the settings page changes cfg.json and
    // what /json/cfg reports, while the radio keeps using the key from boot —
    // the join then fails its MIC against a key that reads back as correct
    // everywhere you would think to look. Treat a credential change exactly
    // like a region change and take the same reboot path.
    if (_lorawanInitDone &&
        (strcmp(_devEUI,  prevDevEUI)  != 0 ||
         strcmp(_joinEUI, prevJoinEUI) != 0 ||
         strcmp(_appKey,  prevAppKey)  != 0)) {
      DEBUG_PRINTLN(F("[LoRaWLED] credentials changed — reboot to re-init stack"));
      _radioConfigDirty = true;
    }
  } else {
    allFound = false;
  }

  // Handle credential reset request
  if (!obj[F("resetCredentials")].isNull() && (bool)obj[F("resetCredentials")]) {
    DEBUG_PRINTLN(F("[LoRaWLED] Credential reset requested — regenerating"));
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
void UsermodLoRaWLED::_generateCredentials() {
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
  Serial.println(F("\n[LoRaWLED] *** First boot — credentials generated ***"));
  Serial.print(F("[LoRaWLED]   devEUI : ")); Serial.println(_devEUI);
  Serial.print(F("[LoRaWLED]   appKey : ")); Serial.println(_appKey);
  Serial.print(F("[LoRaWLED]   joinEUI: ")); Serial.println(_joinEUI);
  Serial.println(F("[LoRaWLED] Register device on your LNS before requesting join.\n"));

  // Persist immediately
  serializeConfigToFS();
}

// ─────────────────────────────────────────────────────────────────────────────
// _loadCredentials()
//
// Returns true if valid credentials are already stored in cfg.json.
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaWLED::_loadCredentials() {
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
bool UsermodLoRaWLED::_allocatePins() {
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

void UsermodLoRaWLED::_deallocatePins() {
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
void UsermodLoRaWLED::_initRadio() {
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
    DEBUG_PRINTF("[LoRaWLED] Radio init FAILED (err=%lu)\n", result);
    _radioReady = false;
    return;
  }
  DEBUG_PRINTLN(F("[LoRaWLED] Radio init OK"));
  _radioReady = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// loraApplySubBandMask()  (M6-016)
//
// Pins the enabled uplink channels to one sub-band of a 64+8 channel plan
// (US915 / AU915), covering the 500 kHz channel as well as the eight 125 kHz
// ones. lmh_setSubBandChannels() cannot be used on its own for this, twice over:
//
//   • It only ever writes mask words 0–3, so ChannelsMask[4] — the eight 500 kHz
//     channels 64–71 — is left cleared. Sub-band 2 comes out as channels 8–15
//     and no 500 kHz channel at all.
//   • RegionAlternateDr(), which runs on every OTAA join trial, then sets
//     ChannelsMask[4] = 0x00FF unconditionally — all eight 500 kHz channels,
//     band-wide, ignoring the sub-band. Nothing puts that back: the join-accept
//     CFList that would normally reprogram the mask is discarded, because
//     RegionUS915ApplyCFList() is an empty function in this library.
//
// So the mask a device actually holds after joining is "my sub-band's 125 kHz
// channels, plus every 500 kHz channel in the band" — and that is the mask the
// first data uplink is scheduled against. Re-applying this after the join is
// what keeps a 500 kHz uplink inside the sub-band the gateway listens to.
//
// Sub-band n (1-based) owns 125 kHz channels (n-1)*8 … (n-1)*8+7 and the single
// 500 kHz channel 64+(n-1). Since 8 divides the 16-bit mask words evenly, the
// 125 kHz half is always one byte of word (n-1)/2.
static bool loraApplySubBandMask(uint8_t subBand) {
  if (subBand < 1 || subBand > 8) return false;

  uint16_t mask[6] = { 0, 0, 0, 0, 0, 0 };
  uint8_t  idx     = (uint8_t)(subBand - 1);
  mask[idx / 2] = (idx % 2 == 0) ? 0x00FF : 0xFF00;  // eight 125 kHz channels
  mask[4]       = (uint16_t)(1u << idx);             // one 500 kHz channel

  MibRequestConfirm_t mibReq;
  mibReq.Type                     = MIB_CHANNELS_DEFAULT_MASK;
  mibReq.Param.ChannelsDefaultMask = mask;
  bool ok = (LoRaMacMibSetRequestConfirm(&mibReq) == LORAMAC_STATUS_OK);

  mibReq.Type              = MIB_CHANNELS_MASK;
  mibReq.Param.ChannelsMask = mask;
  ok = (LoRaMacMibSetRequestConfirm(&mibReq) == LORAMAC_STATUS_OK) && ok;

  if (!ok) {
    DEBUG_PRINTLN(F("[LoRaWLED] channel mask set rejected by MAC"));
  }
  return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// _attemptJoin()  (MVP-004)
//
// On first call: initializes the LoRaWAN stack (lmh_init), sets US915
// subband 2 channel mask, then calls lmh_join().
// On retry calls: just calls lmh_join() again (stack already initialized).
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::_attemptJoin() {
  if (!_lorawanInitDone) {
    // Parse credential strings to byte arrays
    uint8_t devEui[8]  = {};
    uint8_t joinEui[8] = {};
    uint8_t appKey[16] = {};

    if (!hexStrToBytes(_devEUI,  devEui,  8) ||
        !hexStrToBytes(_joinEUI, joinEui, 8) ||
        !hexStrToBytes(_appKey,  appKey,  16)) {
      DEBUG_PRINTLN(F("[LoRaWLED] _attemptJoin: invalid credentials — run provisioning"));
      _joinState = LoraDmxJoinState::JoinFailed;
      return;
    }

    lmh_setDevEui(devEui);
    lmh_setAppEui(joinEui);
    lmh_setAppKey(appKey);

    // ── Regional radio configuration (M6-014) ────────────────────────────────
    LoraRegionInfo region;
    loraRegionInfo(_region, region);

    // Data rate: an operator value outside the region's valid range is rejected
    // rather than handed to lmh_init(), which would silently mis-configure the
    // radio for that region (DR_4 is SF8BW500 in US915 but SF8BW125 in EU868).
    int8_t dataRate = region.defaultDataRate;
    if (_txDataRate >= 0) {
      if (_txDataRate <= region.maxDataRate) {
        dataRate = _txDataRate;
      } else {
        DEBUG_PRINTF("[LoRaWLED] txDataRate DR_%d invalid for %s (max DR_%d) — using DR_%d\n",
                     (int)_txDataRate, region.name, (int)region.maxDataRate,
                     (int)region.defaultDataRate);
      }
    }
    _effectiveDataRate = dataRate;

    static lmh_param_t lmhParam;
    lmhParam.adr_enable            = _adrEnable ? LORAWAN_ADR_ON : LORAWAN_ADR_OFF;
    lmhParam.tx_data_rate          = dataRate;
    lmhParam.enable_public_network = LORAWAN_PUBLIC_NETWORK;
    lmhParam.nb_trials             = 3;
    lmhParam.tx_power              = TX_POWER_0;
    // Duty cycle follows the region table only — see LORA_REGION_TABLE.
    lmhParam.duty_cycle            = region.dutyCycleOn ? LORAWAN_DUTYCYCLE_ON
                                                        : LORAWAN_DUTYCYCLE_OFF;

    // Join as Class A even though this device runs Class C. The MAC drops back
    // to Class A across an OTAA join regardless, and joining *as* Class C
    // changes how the stack ends the join cycle: OnRadioRxTimeout() skips
    // `MacDone = 1` for Class C at the RX2 slot and re-opens continuous RX2
    // instead, so the cycle can only be closed by the AckTimeout timer. That
    // leaves a single fragile path between a missed join accept and the
    // lmh_has_joined_failed callback we depend on. _onJoinSuccess() requests
    // Class C the moment the join lands, which is what the stack's own examples
    // do and what actually opens the continuous RX2 window.
    lmh_error_status err = lmh_init(&s_loraCallbacks, lmhParam,
                                     true /*otaa*/, CLASS_A,
                                     region.macRegion);
    if (err != LMH_SUCCESS) {
      DEBUG_PRINTF("[LoRaWLED] lmh_init failed (%d)\n", (int)err);
      _joinState = LoraDmxJoinState::JoinFailed;
      return;
    }

    // Sub-band channel masks only exist in the 64+8 channel plans (US915,
    // AU915). EU868 and AS923 have no sub-bands — masking there would disable
    // legitimate channels.
    if (region.subBandApplies) {
      uint8_t subBand = _subBand ? _subBand : region.defaultSubBand;
      if (subBand < 1 || subBand > 8) subBand = region.defaultSubBand;
      _effectiveSubBand = subBand;
      lmh_setSubBandChannels(subBand);
      // …then correct the 500 kHz half of the mask it leaves cleared (M6-016).
      loraApplySubBandMask(subBand);
      DEBUG_PRINTF("[LoRaWLED] region %s sub-band %u DR_%d\n",
                   region.name, subBand, (int)dataRate);
    } else {
      DEBUG_PRINTF("[LoRaWLED] region %s (no sub-band mask)\n", region.name);
    }

    _lorawanInitDone = true;
    DEBUG_PRINTLN(F("[LoRaWLED] LoRaWAN stack init OK, sending JoinRequest"));
  }

  _joinState     = LoraDmxJoinState::Joining;
  _joinStartedMs = millis();
  lmh_join();
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback forwarding methods (called from static C-style callbacks above)
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::_pushDownlink(const uint8_t* buf, uint8_t len,
                                    uint8_t fport, int16_t rssi, uint8_t snr) {
  _rssi = (float)rssi;
  _snr  = (float)(int8_t)snr;
  _fCntDown++;

  if (_rxCount >= LORAWLED_RX_RING_SIZE) {
    // Ring full — drop oldest slot
    _rxTail   = (_rxTail + 1) % LORAWLED_RX_RING_SIZE;
    _rxCount--;
    _overflow++;
  }

  LoraDmxRxEntry& entry = _rxBuf[_rxHead];
  uint8_t copyLen = (len > LORAWLED_PAYLOAD_MAX) ? LORAWLED_PAYLOAD_MAX : len;
  memcpy(entry.payload, buf, copyLen);
  entry.len   = copyLen;
  entry.fport = fport;
  entry.valid = true;
  _rxHead   = (_rxHead + 1) % LORAWLED_RX_RING_SIZE;
  _rxCount++;

  DEBUG_PRINTF("[LoRaWLED] Downlink queued fport=%u len=%u rssi=%d snr=%d\n",
               fport, copyLen, rssi, (int8_t)snr);
}

void UsermodLoRaWLED::_onJoinSuccess() {
  DEBUG_PRINTLN(F("[LoRaWLED] Joined network — requesting Class C"));
  _joinState = LoraDmxJoinState::Joined;

  // M6-016: the join we just completed ran RegionAlternateDr() once per trial,
  // and every one of those calls re-enabled all eight 500 kHz channels
  // band-wide. Put the sub-band mask back before the first data uplink is
  // scheduled against it. Safe here: this callback only runs with the MAC in
  // LORAMAC_IDLE, which is what LoRaMacMibSetRequestConfirm() requires.
  if (_effectiveSubBand) loraApplySubBandMask(_effectiveSubBand);

  // After OTAA join the MAC resets to Class A. Request Class C explicitly so
  // the continuous RX2 window is opened and downlinks arrive within seconds
  // rather than waiting for the next uplink RX window.
  lmh_class_request(CLASS_C);

  // lmh_class_request() reports LMH_ERROR for a successful A→C switch (it sets
  // the error before testing the MIB result), so its return value cannot be
  // used. Read the class back instead — a device that silently stayed in
  // Class A looks identical from the outside until a downlink goes missing.
  DeviceClass_t cls = CLASS_A;
  lmh_class_get(&cls);
  _classC = (cls == CLASS_C);
  if (!_classC) {
    DEBUG_PRINTLN(F("[LoRaWLED] WARNING: Class C switch did not take — still Class A"));
  }

  // The network server holds the new session as pending until it sees an uplink
  // on it, and queues Class C downlinks rather than sending them meanwhile. A
  // device that waits a full uplink interval here is unreachable for that whole
  // period after every join. Bring the first uplink forward instead.
  _lastUplinkMs = millis() - _uplinkInterval + LORAWLED_POST_JOIN_UPLINK_MS;
}

void UsermodLoRaWLED::_onJoinFailed() {
  DEBUG_PRINTLN(F("[LoRaWLED] Join failed"));
  _joinState = LoraDmxJoinState::JoinFailed;
  _classC    = false;   // the MAC drops back to Class A across a failed join
}

// ─────────────────────────────────────────────────────────────────────────────
// _processRxQueue()
//
// Dequeues ONE downlink entry per loop() call, parses it, and applies it.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::_processRxQueue() {
  if (_rxCount == 0) return;

  LoraDmxRxEntry& entry = _rxBuf[_rxTail];
  if (!entry.valid) {
    _rxTail = (_rxTail + 1) % LORAWLED_RX_RING_SIZE;
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
    _rxTail  = (_rxTail + 1) % LORAWLED_RX_RING_SIZE;
    _rxCount--;
    _lastDownlinkMs = millis();
    return;
  }

  entry.valid = false;
  _rxTail  = (_rxTail + 1) % LORAWLED_RX_RING_SIZE;
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
LoraDmxCommand UsermodLoRaWLED::_parseBinary(const uint8_t* data, uint16_t len) {
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
LoraDmxCommand UsermodLoRaWLED::_parseJSON(const uint8_t* data, uint16_t len) {
  LoraDmxCommand cmd;

  // Request a shared JSON buffer from WLED
  if (!requestJSONBufferLock(USERMOD_ID_LORAWLED)) {
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
void UsermodLoRaWLED::_applySegmentJson(const uint8_t* data, uint16_t len) {
  if (!requestJSONBufferLock(USERMOD_ID_LORAWLED)) {
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
void UsermodLoRaWLED::_applyCommand(const LoraDmxCommand& cmd) {
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
void UsermodLoRaWLED::_sendUplink() {
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

  // Fill persistent payload buffer — read by _loraTxTaskFn after sem fires
  _txPayloadBuf[0]  = 0x01;                               // version
  _txPayloadBuf[1]  = flags;
  _txPayloadBuf[2]  = bri;
  _txPayloadBuf[3]  = fxId;
  _txPayloadBuf[4]  = (uint8_t)(droppedClamped & 0xFF);
  _txPayloadBuf[5]  = (uint8_t)(droppedClamped >> 8);
  _txPayloadBuf[6]  = (uint8_t)(replayedClamped & 0xFF);
  _txPayloadBuf[7]  = (uint8_t)(replayedClamped >> 8);
  _txPayloadBuf[8]  = (uint8_t)(fCntDownLow & 0xFF);
  _txPayloadBuf[9]  = (uint8_t)(fCntDownLow >> 8);
  _txPayloadBuf[10] = rssiAbs;
  _txPayloadBuf[11] = (uint8_t)snrX4;

  _pendingTx.buffer   = _txPayloadBuf;
  _pendingTx.buffsize = sizeof(_txPayloadBuf);
  _pendingTx.port     = 2;
  _pendingTx.rssi     = 0;
  _pendingTx.snr      = 0;

  // Hand off to the TX task — returns immediately, never blocks main loop
  if (_loraTxSem) {
    xSemaphoreGive(_loraTxSem);
    DEBUG_PRINTLN(F("[LoRaWLED] Uplink TX queued: FPort=2 len=12"));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Replay protection helpers
// ─────────────────────────────────────────────────────────────────────────────
bool UsermodLoRaWLED::_isDuplicate(uint32_t cmdId) {
  for (uint8_t i = 0; i < REPLAY_RING_SIZE; i++) {
    if (_replayRing[i] == cmdId) return true;
  }
  return false;
}

void UsermodLoRaWLED::_trackCmdId(uint32_t cmdId) {
  _replayRing[_replayHead] = cmdId;
  _replayHead = (_replayHead + 1) % REPLAY_RING_SIZE;
}

// ─────────────────────────────────────────────────────────────────────────────
// _loraTxTaskFn()  (MVP-016)
//
// FreeRTOS task pinned to core 1.  All blocking SPI radio work lives here so
// the WLED main loop is never stalled on the SX1262 BUSY pin.
//
// On each iteration:
//   • Waits up to 10 ms for _loraTxSem (set by _sendUplink() when a frame is
//     ready).
//   • Calls Radio.IrqProcess().  NOTE: on ESP32 this is a no-op — SX126x-Arduino
//     compiles the body only for ESP8266.  DIO1 edges are serviced by the
//     library's own "LORA" FreeRTOS task, which its ISR wakes through a
//     semaphore and which calls Radio.BgIrqProcess() itself.  The call is kept
//     for the ESP8266 build; do not read it as this task driving RX/TX events.
//   • If the semaphore fired, calls lmh_send() with the pre-built payload
//     stored in _pendingTx / _txPayloadBuf.
// ─────────────────────────────────────────────────────────────────────────────
void UsermodLoRaWLED::_loraTxTaskFn(void* arg) {
  auto* self = static_cast<UsermodLoRaWLED*>(arg);
  for (;;) {
    bool doTx = (xSemaphoreTake(self->_loraTxSem, pdMS_TO_TICKS(10)) == pdTRUE);
    Radio.IrqProcess();
    if (doTx) {
      // LMH_SUCCESS means the MAC accepted the frame for scheduling — it is not
      // evidence that anything was transmitted, and _fCntUp is not evidence
      // either (M6-016). The authoritative reading is the network server's
      // last_f_cnt_up; addToJsonInfo() reports the MAC's own uplink counter
      // alongside ours so the two can be compared without a serial console.
      lmh_error_status status = lmh_send(&self->_pendingTx, LMH_UNCONFIRMED_MSG);
      if (status == LMH_SUCCESS) {
        DEBUG_PRINTLN(F("[LoRaWLED] Uplink accepted by MAC: FPort=2 len=12"));
        self->_fCntUp++;
        strlcpy(self->_lastTxStatus, "accepted", sizeof(self->_lastTxStatus));
      } else {
        DEBUG_PRINTF("[LoRaWLED] Uplink TX failed (%d)\n", (int)status);
        self->_txErrors++;
        strlcpy(self->_lastTxStatus,
                status == LMH_BUSY ? "busy" : "error",
                sizeof(self->_lastTxStatus));
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Module registration
// ─────────────────────────────────────────────────────────────────────────────
#ifdef USERMOD_LORAWLED
static UsermodLoRaWLED lorawled_instance;
REGISTER_USERMOD(lorawled_instance);
#endif
