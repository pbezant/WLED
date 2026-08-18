#pragma once
/*
 * Usermod:  LoRa-WLED
 * Board:    Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262)
 * Purpose:  Receive LoRaWAN Class C downlinks and translate them to WLED
 *           lighting commands. Optionally drive a DMX universe via Grove
 *           RS485 (GPIO 19 TX / GPIO 20 RX) using WLED's built-in
 *           SparkFunDMX output path.
 *
 * Author:   Preston Bezant
 * License:  MIT
 *
 * Ticket refs: MVP-002 (scaffold), MVP-003 (HW init), MVP-004 (radio loop),
 *              MVP-005 (credentials), MVP-006 (commissioning UI),
 *              MVP-007 (parser), MVP-008 (mapper), MVP-009 (config),
 *              MVP-010 (diagnostics), MVP-011 (uplink policy)
 */

#include "wled.h"
#include <SPI.h>
#include "LoRaWan-Arduino.h"  // lora_hardware_init, lmh_*, hw_config, Radio

// ─── Usermod ID ──────────────────────────────────────────────────────────────
#ifndef USERMOD_ID_LORAWLED
  #define USERMOD_ID_LORAWLED  59
#endif

// ─── Version ─────────────────────────────────────────────────────────────────
#define LORAWLED_VERSION "0.1.0"

// ─── SPI2 (FSPI) pin assignments — hardwired on Heltec V3 PCB ─────────────
#define LORAWLED_PIN_SCK   9
#define LORAWLED_PIN_MISO  11
#define LORAWLED_PIN_MOSI  10
#define LORAWLED_PIN_NSS   8
#define LORAWLED_PIN_RST   12
#define LORAWLED_PIN_BUSY  13
#define LORAWLED_PIN_DIO1  14

// ─── Downlink ring-buffer ────────────────────────────────────────────────────
#define LORAWLED_RX_RING_SIZE  4
#define LORAWLED_PAYLOAD_MAX   242   // LoRaWAN max payload bytes

// ─── Join watchdog ───────────────────────────────────────────────────────────
// Ceiling on how long a join attempt may sit in `Joining` before we give up on
// the radio stack ever calling back. A full OTAA cycle is three trials of
// RX1 (5 s) + RX2 (6 s) plus inter-trial backoff, so a healthy failure reports
// in well under 60 s — this only fires when an event was genuinely lost.
#define LORAWLED_JOIN_TIMEOUT_MS  60000UL

// ─── Post-join uplink (M6-016) ───────────────────────────────────────────────
// The network server keeps a freshly joined session pending until it receives
// an uplink on it, and holds Class C downlinks until then. Waiting a full
// uplink interval after every join leaves the device unreachable for that
// whole period, so the first uplink is brought forward to this delay instead.
#define LORAWLED_POST_JOIN_UPLINK_MS  5000UL

// ─── LoRaWAN region (M6-014) ─────────────────────────────────────────────────
// Index into LORA_REGION_TABLE in usermod_lorawled.cpp. The stored config value
// is this index, so the order must never be reshuffled — append only.
enum class LoraRegionId : uint8_t {
  US915 = 0,
  EU868,
  AU915,
  AS923,
  _Count
};

#define LORAWLED_REGION_DEFAULT  LoraRegionId::US915

// Largest application payload we will ever build. US915 DR1 — the region
// table's default — caps an uplink at 53 bytes, and nothing here may exceed it.
#define LORAWLED_MAX_UPLINK_BYTES 53
// Segments included in a full state report. A device with more still reports
// its true segment count in the root fragment, so a truncated report is
// visibly truncated rather than quietly wrong.
#define LORAWLED_MAX_STATE_SEGMENTS 8
// Spacing between fragments of one report. The MAC rejects a second frame while
// the first is still in flight, and a tight loop would just burn LMH_BUSY.
#define LORAWLED_STATE_FRAG_GAP_MS 3000

// ─── Join state ──────────────────────────────────────────────────────────────
enum class LoraDmxJoinState : uint8_t {
  NotJoined = 0,
  Joining,
  Joined,
  JoinFailed
};

static const char* joinStateStr(LoraDmxJoinState s) {
  switch (s) {
    case LoraDmxJoinState::Joining:    return "joining";
    case LoraDmxJoinState::Joined:     return "joined";
    case LoraDmxJoinState::JoinFailed: return "join_failed";
    default:                           return "not_joined";
  }
}

// ─── Downlink ring-buffer entry ──────────────────────────────────────────────
struct LoraDmxRxEntry {
  uint8_t  payload[LORAWLED_PAYLOAD_MAX];
  uint16_t len;
  uint8_t  fport;
  bool     valid;
};

// ─── Parsed command ──────────────────────────────────────────────────────────
enum class LoraDmxCmdType : uint8_t {
  Off = 0, ColorNamed, Test,
  PatternStart, PatternStop,
  Preset, Power, Brightness, Segment, Combined,
  // Opcodes from the WLED Cloud downlink protocol (docs/11-lns-integration.md).
  Color, Effect, Speed, Intensity, Palette, FullState, RequestState,
  RequestFullState,
  Drop
};

struct LoraDmxCommand {
  LoraDmxCmdType type   = LoraDmxCmdType::Drop;
  uint8_t  r = 0, g = 0, b = 0;      // COLOR / TEST
  uint8_t  patternType  = 0;          // PATTERN_START
  uint16_t speed        = 0;          // PATTERN_START
  uint16_t cycles       = 0;          // PATTERN_START
  uint8_t  preset       = 0;          // PRESET / COMBINED
  bool     on           = true;       // POWER / COMBINED
  uint8_t  bri          = 255;        // BRIGHTNESS / COMBINED
  uint8_t  fx           = 0;          // EFFECT / FULL_STATE
  uint8_t  sx           = 128;        // SPEED / FULL_STATE
  uint8_t  ix           = 128;        // INTENSITY / FULL_STATE
  uint8_t  pal          = 0;          // PALETTE / FULL_STATE
  uint32_t cmdId        = 0xFFFFFFFF; // replay protection
};

// ─── Main usermod class ──────────────────────────────────────────────────────
class UsermodLoRaWLED : public Usermod {
 public:
  // ── Usermod v2 lifecycle ──────────────────────────────────────────────────
  void setup()          override;
  void loop()           override;
  void addToJsonInfo(JsonObject& root)  override;
  void addToJsonState(JsonObject& root) override;
    void readFromJsonState(JsonObject& root) override;  // handles /json/state writes
  void addToConfig(JsonObject& root)    override;
  bool readFromConfig(JsonObject& root) override;
  uint16_t getId()      override { return USERMOD_ID_LORAWLED; }

  // ── C-callback forwarding (called by static LoRaWAN callbacks) ───────────
  const char* _devEUI_cb() const { return _devEUI; }
  void _pushDownlink(const uint8_t* buf, uint8_t len, uint8_t fport,
                     int16_t rssi, uint8_t snr);
  void _onJoinSuccess();
  void _onJoinFailed();

  // ── Credential accessor — used by WledCloudUsermod during claiming ────────
  struct Credentials {
    const char* devEUI;   // 16 hex chars (read-only pointer into _devEUI)
    const char* joinEUI;  // 16 hex chars (read-only pointer into _joinEUI)
    const char* appKey;   // 32 hex chars (read-only pointer into _appKey)
    bool provisioned;
  };
  Credentials getCredentials() const {
    return { _devEUI, _joinEUI, _appKey, _credentialsProvisioned };
  }

 private:
  // ── Config (persisted) ────────────────────────────────────────────────────
  bool     _enabled               = true;
  char     _devEUI[17]            = "";   // 16 hex chars + NUL
  char     _joinEUI[17]           = "";
  char     _appKey[33]            = "";   // 32 hex chars + NUL — write-only via API
  bool     _credentialsProvisioned = false;
  // 15 min. At US915 DR1 a 16-byte status frame costs ~120 ms of airtime, so
  // the old 5-minute default spent ~35 s/day against TTN's ~30 s/day fair-use
  // budget — over the line before the on-demand state report existed.
  uint32_t _uplinkInterval        = 900000; // ms (15 min)
  uint32_t _joinRetryInterval     = 30000;  // ms (30 s)
  uint32_t _cmdThrottleMs         = 100;

  // Regional radio configuration (M6-014). Duty cycle is deliberately absent —
  // it is derived from the region table and is never operator-settable.
  uint8_t  _region                = (uint8_t)LORAWLED_REGION_DEFAULT;
  uint8_t  _subBand               = 0;    // 0 = region default
  bool     _adrEnable             = false;
  int8_t   _txDataRate            = -1;   // -1 = region default

  // SPI pin config — user can override via /json/cfg
  int8_t _pinSck  = LORAWLED_PIN_SCK;
  int8_t _pinMiso = LORAWLED_PIN_MISO;
  int8_t _pinMosi = LORAWLED_PIN_MOSI;
  int8_t _pinNss  = LORAWLED_PIN_NSS;
  int8_t _pinRst  = LORAWLED_PIN_RST;
  int8_t _pinBusy = LORAWLED_PIN_BUSY;
  int8_t _pinDio1 = LORAWLED_PIN_DIO1;

  // ── Runtime state ─────────────────────────────────────────────────────────
  bool               _radioReady        = false;
  bool               _lorawanInitDone   = false;
  LoraDmxJoinState   _joinState         = LoraDmxJoinState::NotJoined;
  float              _rssi              = 0.0f;
  float              _snr               = 0.0f;
  uint32_t           _fCntUp            = 0;
  uint32_t           _fCntDown          = 0;
  uint32_t           _lastUplinkMs      = 0;
  uint32_t           _lastDownlinkMs    = 0;
  uint32_t           _lastJoinAttemptMs = 0;
  uint32_t           _joinStartedMs     = 0;   // millis() of the in-flight join
  uint32_t           _lastCmdMs         = 0;
  uint32_t           _dropped           = 0;
  uint32_t           _replayed          = 0;
  uint32_t           _overflow          = 0;
  bool               _loopWarn          = false;
  uint32_t           _maxLoopUs         = 0;    // worst-case loop() µs (MVP-015)
  char               _lastCmdResult[32] = "none";

  // M6-014: set when readFromConfig() sees a region/sub-band/DR change after the
  // stack is already initialised. lmh_init() cannot safely run twice, so loop()
  // takes the only supported reset path — a reboot — and the new region is
  // picked up on the next _attemptJoin().
  bool               _radioConfigDirty  = false;
  int8_t             _effectiveDataRate = -1;   // DR actually passed to lmh_init()
  uint8_t            _effectiveSubBand  = 0;    // 0 = region has no sub-band mask

  // M6-016 diagnostics. _classC is read back from the MAC after the post-join
  // class request rather than assumed — see _onJoinSuccess(). _txErrors counts
  // frames the MAC refused; neither it nor _fCntUp says anything about airtime.
  bool               _classC            = false;
  uint32_t           _txErrors          = 0;
  char               _lastTxStatus[16]  = "none";

  // ── FreeRTOS TX task (MVP-016) ─────────────────────────────────────────────
  // All blocking SPI radio work (lmh_send + Radio.IrqProcess) runs here so the
  // WLED main loop is never stalled waiting on the SX1262 BUSY pin.
  uint8_t           _txPayloadBuf[LORAWLED_MAX_UPLINK_BYTES] = {}; // built uplink payload
  lmh_app_data_t    _pendingTx         = {};   // points into _txPayloadBuf
  SemaphoreHandle_t _loraTxSem         = nullptr;
  TaskHandle_t      _loraTxTask        = nullptr;

  // ── Full state report progress ─────────────────────────────────────────────
  // Fragments are built one at a time, on demand, rather than staged as a set:
  // a staged set would cost ~900 bytes of RAM to buy atomicity the report does
  // not actually need. The report ID lets the cloud detect a set that got mixed
  // with a later one instead of silently reassembling a state that never was.
  uint8_t  _stateReportId    = 0;
  uint8_t  _stateFragNext    = 0;
  uint8_t  _stateFragCount   = 0;   // 0 = no report in progress
  uint8_t  _stateSegReported = 0;
  uint8_t  _stateNameIds[LORAWLED_MAX_STATE_SEGMENTS] = {};
  uint8_t  _stateNameCount   = 0;
  uint32_t _lastStateFragMs  = 0;

  // Replay protection — ring of last 16 cmd IDs
  static const uint8_t REPLAY_RING_SIZE = 16;
  uint32_t _replayRing[REPLAY_RING_SIZE] = {};
  uint8_t  _replayHead = 0;

  // Downlink ring buffer
  LoraDmxRxEntry _rxBuf[LORAWLED_RX_RING_SIZE] = {};
  uint8_t        _rxHead  = 0;
  uint8_t        _rxTail  = 0;
  uint8_t        _rxCount = 0;

  // SPIClass on SPI2 (FSPI) — dedicated to SX1262
  SPIClass _loraSPI;

  // ── Private methods ────────────────────────────────────────────────────────
  void     _generateCredentials();
  bool     _loadCredentials();
  void     _initRadio();
  void     _attemptJoin();
  void     _processRxQueue();
  LoraDmxCommand _parseBinary(const uint8_t* data, uint16_t len);
  static uint16_t _binaryArgBytes(uint8_t opcode);
  LoraDmxCommand _parseJSON(const uint8_t* data, uint16_t len);
  void     _applyCommand(const LoraDmxCommand& cmd);
  void     _applySegmentJson(const uint8_t* data, uint16_t len);
  void     _sendUplink();

  // ── Full state report (FPort 3, frame v2) ──────────────────────────────────
  // Sent only in reply to downlink 0xF3 — never on a timer. See the airtime
  // note in docs/11-lns-integration.md.
  void     _beginFullStateReport();
  void     _sendNextStateFragment();
  uint8_t  _buildStateFragment(uint8_t index);
  bool     _isDuplicate(uint32_t cmdId);
  void     _trackCmdId(uint32_t cmdId);
  bool     _allocatePins();
  void     _deallocatePins();
  static void _loraTxTaskFn(void* arg);

  // config key constants
  static const char _name[];
  static const char _keyEnabled[];
  static const char _keyDevEUI[];
  static const char _keyJoinEUI[];
  static const char _keyAppKey[];
  static const char _keyCredProv[];
  static const char _keyUplinkInterval[];
  static const char _keyJoinRetry[];
  static const char _keyCmdThrottle[];
  static const char _keyRegion[];
  static const char _keySubBand[];
  static const char _keyAdrEnable[];
  static const char _keyTxDataRate[];
};
