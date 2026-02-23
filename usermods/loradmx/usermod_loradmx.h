#pragma once
/*
 * Usermod:  LoRa-DMX
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

// ─── Usermod ID ──────────────────────────────────────────────────────────────
#ifndef USERMOD_ID_LORADMX
  #define USERMOD_ID_LORADMX  59
#endif

// ─── Version ─────────────────────────────────────────────────────────────────
#define LORADMX_VERSION "0.1.0"

// ─── SPI2 (FSPI) pin assignments — hardwired on Heltec V3 PCB ─────────────
#define LORADMX_PIN_SCK   9
#define LORADMX_PIN_MISO  11
#define LORADMX_PIN_MOSI  10
#define LORADMX_PIN_NSS   8
#define LORADMX_PIN_RST   12
#define LORADMX_PIN_BUSY  13
#define LORADMX_PIN_DIO1  14

// ─── Downlink ring-buffer ────────────────────────────────────────────────────
#define LORADMX_RX_RING_SIZE  4
#define LORADMX_PAYLOAD_MAX   242   // LoRaWAN max payload bytes

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
  uint8_t  payload[LORADMX_PAYLOAD_MAX];
  uint16_t len;
  uint8_t  fport;
  bool     valid;
};

// ─── Parsed command ──────────────────────────────────────────────────────────
enum class LoraDmxCmdType : uint8_t {
  Off = 0, ColorNamed, Test,
  PatternStart, PatternStop,
  Preset, Power, Brightness, Segment, Combined,
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
  uint32_t cmdId        = 0xFFFFFFFF; // replay protection
};

// ─── Main usermod class ──────────────────────────────────────────────────────
class UsermodLoRaDMX : public Usermod {
 public:
  // ── Usermod v2 lifecycle ──────────────────────────────────────────────────
  void setup()          override;
  void loop()           override;
  void addToJsonInfo(JsonObject& root)  override;
  void addToJsonState(JsonObject& root) override;
    void readFromJsonState(JsonObject& root) override;  // handles /json/state writes
  void addToConfig(JsonObject& root)    override;
  bool readFromConfig(JsonObject& root) override;
  uint16_t getId()      override { return USERMOD_ID_LORADMX; }

 private:
  // ── Config (persisted) ────────────────────────────────────────────────────
  bool     _enabled               = true;
  char     _devEUI[17]            = "";   // 16 hex chars + NUL
  char     _joinEUI[17]           = "";
  char     _appKey[33]            = "";   // 32 hex chars + NUL — write-only via API
  bool     _credentialsProvisioned = false;
  uint32_t _uplinkInterval        = 300000; // ms (5 min)
  uint32_t _joinRetryInterval     = 30000;  // ms (30 s)
  uint32_t _cmdThrottleMs         = 100;

  // SPI pin config — user can override via /json/cfg
  int8_t _pinSck  = LORADMX_PIN_SCK;
  int8_t _pinMiso = LORADMX_PIN_MISO;
  int8_t _pinMosi = LORADMX_PIN_MOSI;
  int8_t _pinNss  = LORADMX_PIN_NSS;
  int8_t _pinRst  = LORADMX_PIN_RST;
  int8_t _pinBusy = LORADMX_PIN_BUSY;
  int8_t _pinDio1 = LORADMX_PIN_DIO1;

  // ── Runtime state ─────────────────────────────────────────────────────────
  bool               _radioReady        = false;
  LoraDmxJoinState   _joinState         = LoraDmxJoinState::NotJoined;
  float              _rssi              = 0.0f;
  float              _snr               = 0.0f;
  uint32_t           _fCntUp            = 0;
  uint32_t           _fCntDown          = 0;
  uint32_t           _lastUplinkMs      = 0;
  uint32_t           _lastDownlinkMs    = 0;
  uint32_t           _lastJoinAttemptMs = 0;
  uint32_t           _lastCmdMs         = 0;
  uint32_t           _dropped           = 0;
  uint32_t           _replayed          = 0;
  uint32_t           _overflow          = 0;
  bool               _loopWarn          = false;
  char               _lastCmdResult[32] = "none";

  // Replay protection — ring of last 16 cmd IDs
  static const uint8_t REPLAY_RING_SIZE = 16;
  uint32_t _replayRing[REPLAY_RING_SIZE] = {};
  uint8_t  _replayHead = 0;

  // Downlink ring buffer
  LoraDmxRxEntry _rxBuf[LORADMX_RX_RING_SIZE] = {};
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
  LoraDmxCommand _parseJSON(const uint8_t* data, uint16_t len);
  void     _applyCommand(const LoraDmxCommand& cmd);
  void     _sendUplink();
  bool     _isDuplicate(uint32_t cmdId);
  void     _trackCmdId(uint32_t cmdId);
  bool     _allocatePins();
  void     _deallocatePins();

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
};
