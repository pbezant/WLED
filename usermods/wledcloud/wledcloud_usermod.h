#pragma once
#include "wled.h"
#include <WebSocketsClient.h>
#include <HTTPClient.h>

/*
 * WledCloudUsermod — connects WLED to WLED Cloud
 *
 * Features:
 *  - Secure device claiming (no user credentials on device)
 *  - Bidirectional state sync via WLED onStateChange hook
 *  - Remote command execution (color, effects, presets, OTA)
 *  - Telemetry reporting (RSSI, heap, uptime)
 *
 * See: /docs/12-usermod-spec.md for full specification
 */

// ── Claim flow state machine ──────────────────────────────────────────────────
enum class ClaimState : uint8_t {
  IDLE = 0,
  CHECK_TOKEN,
  GENERATE_CODE,
  REGISTER_CODE,   // POST /api/device-auth/register-code
  DISPLAY_CODE,    // waiting, polling every 5s
  POLL,
  SAVE_TOKEN,
  CONNECT_WS
};

class WledCloudUsermod : public Usermod {
 private:
  // ── Config fields (persisted to cfg.json under "WLEDCloud") ─────────────────
  bool     enabled       = false;
  char     serverHost[65] = "cloud.wled.me";
  uint16_t serverPort    = 3000;
  bool     useTLS        = false;  // true requires HAS_SSL / WiFiClientSecure
  char     deviceToken[129] = "";   // set by claim flow
  char     venueId[37]   = "";      // set by claim flow
  uint16_t syncInterval  = 30;      // seconds
  bool     sendTelemetry = true;

  // ── Runtime state (not persisted) ───────────────────────────────────────────
  bool     initDone       = false;
  bool     wsConnected    = false;
  bool     wsConnecting   = false;  // true while handshake in progress
  char     claimCode[9]   = "";     // e.g. "X7K-9P2\0"
  ClaimState claimState   = ClaimState::IDLE;

  unsigned long lastStateSync   = 0;
  unsigned long lastTelemetry   = 0;
  unsigned long lastPingRecv    = 0;
  unsigned long lastClaimPoll   = 0;
  unsigned long lastReconnect   = 0;
  unsigned long claimStarted    = 0; // millis() when claim code was generated
  unsigned long lastRegisterAttempt = 0; // throttle register-code HTTP calls

  uint8_t  reconnectAttempts = 0;
  bool     pendingConfigSave = false;
  bool     stateChanged      = false;
  bool     needsStateSync    = false;  // set in WS callback, handled in loop()

  // ── Conflict policy state ──────────────────────────────────────────────────
  // Tracks cloud-priority lock (suppress local echoes after cloud command)
  // and local-lock-ttl state (reject cloud commands when device is active).
  unsigned long cloudLockSetAt  = 0;          // millis() when cloud hold was applied
  uint32_t      cloudHoldMs     = 0;          // hold duration in ms (0 = inactive)
  unsigned long lastLocalChange = 0;          // millis() of last non-cloud state change
  uint16_t      localLockTtl   = 5000;        // local-lock-ttl window in ms
  char          conflictPolicy[20] = "last-write-wins";

  // ── Pending command (buffered in WS callback, applied safely in loop()) ─────
  // Storing the serialized JSON avoids keeping a dangling JsonDocument reference
  // across the callback boundary. 512 bytes handles all payloads we send.
  bool     hasPendingCommand = false;
  char     pendingCmdId[37]  = "";
  char     pendingStateJson[512] = "";

  // ── Shared send buffer (class member to avoid 1KB stack allocation in callback) ──
  char     sendBuf[1024];

  // ── WebSocket client ─────────────────────────────────────────────────────────
  WebSocketsClient ws;

  // ── PROGMEM strings ─────────────────────────────────────────────────────────
  static const char _name[];
  static const char _enabled[];

  // ── Claim code generation ────────────────────────────────────────────────────
  void generateClaimCode() {
    // charset excludes ambiguous chars: 0 O 1 I L
    static const char charset[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    static const int charsetLen = sizeof(charset) - 1; // 30
    claimCode[0] = charset[random(0, charsetLen)];
    claimCode[1] = charset[random(0, charsetLen)];
    claimCode[2] = charset[random(0, charsetLen)];
    claimCode[3] = '-';
    claimCode[4] = charset[random(0, charsetLen)];
    claimCode[5] = charset[random(0, charsetLen)];
    claimCode[6] = charset[random(0, charsetLen)];
    claimCode[7] = '\0';
    DEBUG_PRINTF("[WledCloud] Claim code: %s\n", claimCode);
  }

  // ── HTTP: register claim code with cloud ─────────────────────────────────────
  bool registerCodeWithCloud() {
    if (!WLED_CONNECTED) return false;

    HTTPClient http;
    char url[200];
    snprintf(url, sizeof(url), "%s://%s:%u/api/device-auth/register-code",
             useTLS ? "https" : "http", serverHost, serverPort);

    http.begin(url);
    http.setConnectTimeout(3000);  // 3s connect timeout — avoid WDT trigger
    http.setTimeout(5000);         // 5s total timeout
    http.addHeader(F("Content-Type"), F("application/json"));

    // Build payload
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(ESP.getEfuseMac() >> 40),
             (uint8_t)(ESP.getEfuseMac() >> 32),
             (uint8_t)(ESP.getEfuseMac() >> 24),
             (uint8_t)(ESP.getEfuseMac() >> 16),
             (uint8_t)(ESP.getEfuseMac() >> 8),
             (uint8_t)(ESP.getEfuseMac()));

    // Detect lorawled usermod and collect LoRa credentials if present
    bool hasLora = false;
    char loraDevEUI[17] = "";
    char loraJoinEUI[17] = "";
    char loraAppKey[33] = "";

#ifdef USERMOD_LORAWLED
    // Forward-declare to avoid requiring the full header include order to be exact.
    // USERMOD_ID_LORAWLED is defined in usermod_lorawled.h which must be compiled
    // in the same build when USERMOD_LORAWLED is defined.
    auto* lorawled = static_cast<UsermodLoRaWLED*>(usermods.lookup(USERMOD_ID_LORAWLED));
    if (lorawled) {
      auto creds = lorawled->getCredentials();
      if (creds.provisioned && strlen(creds.devEUI) == 16 && strlen(creds.appKey) == 32) {
        strlcpy(loraDevEUI, creds.devEUI, sizeof(loraDevEUI));
        strlcpy(loraJoinEUI, creds.joinEUI, sizeof(loraJoinEUI));
        strlcpy(loraAppKey, creds.appKey, sizeof(loraAppKey));
        hasLora = true;
      }
    }
#endif

    // NOTE: AppKey is a sensitive root key. Transmission over plain HTTP is
    // acceptable during the initial WiFi AP setup phase (LAN-only), but
    // production deployments should set useTLS=true.
    char payload[512];
    if (hasLora) {
      snprintf(payload, sizeof(payload),
               "{\"claimCode\":\"%s\",\"macAddress\":\"%s\",\"firmwareVersion\":\"%s\","
               "\"numLeds\":%u,\"connectionType\":\"lorawan\","
               "\"devEUI\":\"%s\",\"joinEUI\":\"%s\",\"appKey\":\"%s\"}",
               claimCode, mac, versionString, strip.getLengthTotal(),
               loraDevEUI, loraJoinEUI, loraAppKey);
    } else {
      snprintf(payload, sizeof(payload),
               "{\"claimCode\":\"%s\",\"macAddress\":\"%s\",\"firmwareVersion\":\"%s\",\"numLeds\":%u}",
               claimCode, mac, versionString, strip.getLengthTotal());
    }

    int code = http.POST((uint8_t *)payload, strlen(payload));
    http.end();

    if (code == 201 || code == 200) {
      DEBUG_PRINTLN(F("[WledCloud] Claim code registered"));
      return true;
    }
    DEBUG_PRINTF("[WledCloud] register-code failed: %d\n", code);
    return false;
  }

  // ── HTTP: poll for claim completion ─────────────────────────────────────────
  // Returns: 0=pending, 1=claimed(token filled), -1=expired/error
  int pollForClaim() {
    if (!WLED_CONNECTED) return 0;

    HTTPClient http;
    char url[200];
    snprintf(url, sizeof(url), "%s://%s:%u/api/device-auth/poll?claimCode=%s",
             useTLS ? "https" : "http", serverHost, serverPort, claimCode);

    http.begin(url);
    http.setConnectTimeout(3000);  // 3s connect timeout — avoid WDT trigger
    http.setTimeout(5000);         // 5s total timeout
    int code = http.GET();

    if (code != 200 && code != 202 && code != 410) {
      http.end();
      return 0;
    }

    String body = http.getString();
    http.end();

    // Simple JSON parse without full ArduinoJson for small responses
    if (body.indexOf("\"claimed\"") >= 0) {
      // Extract deviceToken
      int tStart = body.indexOf("\"deviceToken\":\"");
      if (tStart >= 0) {
        tStart += 15;
        int tEnd = body.indexOf("\"", tStart);
        if (tEnd > tStart && (tEnd - tStart) < (int)sizeof(deviceToken)) {
          body.substring(tStart, tEnd).toCharArray(deviceToken, sizeof(deviceToken));
        }
      }
      // Extract venueId
      int vStart = body.indexOf("\"venueId\":\"");
      if (vStart >= 0) {
        vStart += 11;
        int vEnd = body.indexOf("\"", vStart);
        if (vEnd > vStart && (vEnd - vStart) < (int)sizeof(venueId)) {
          body.substring(vStart, vEnd).toCharArray(venueId, sizeof(venueId));
        }
      }
      if (strlen(deviceToken) > 0) return 1;
    }
    if (code == 410 || body.indexOf("\"expired\"") >= 0) return -1;
    return 0; // pending
  }

  // ── WebSocket: connect ────────────────────────────────────────────────────────
  void connectWebSocket() {
    if (strlen(deviceToken) == 0) return;

    char path[160];
    snprintf(path, sizeof(path), "/ws/device?token=%s", deviceToken);

    ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
      handleWsEvent(type, payload, length);
    });

#ifdef HAS_SSL
    if (useTLS) {
      ws.beginSSL(serverHost, serverPort, path);
    } else {
      ws.begin(serverHost, serverPort, path);
    }
#else
    // Compiled without TLS support (WEBSOCKETS_NETWORK_TYPE != NETWORK_ESP32)
    if (useTLS) {
      DEBUG_PRINTLN(F("[WledCloud] WARN: TLS not available in this build, falling back to plain WS"));
    }
    ws.begin(serverHost, serverPort, path);
#endif

    ws.setReconnectInterval(5000); // library handles reconnection
    wsConnecting = true;
    DEBUG_PRINTF("[WledCloud] Connecting WS → %s:%u%s\n", serverHost, serverPort, path);
  }

  void disconnectWebSocket() {
    ws.disconnect();
    wsConnected = false;
    wsConnecting = false;
  }

  // ── WebSocket: event handler ─────────────────────────────────────────────────
  void handleWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        wsConnected = true;
        wsConnecting = false;
        reconnectAttempts = 0;
        lastPingRecv = millis();
        DEBUG_PRINTLN(F("[WledCloud] WebSocket connected"));
        // Don't call sendStateUpdate() here — the server sends request_state
        // on connect, which sets needsStateSync. Calling it here inside the
        // callback risks stack overflow (1KB buf + DynamicJsonDocument) and
        // produces a duplicate state send.
        break;

      case WStype_DISCONNECTED:
        wsConnected = false;
        wsConnecting = false;
        DEBUG_PRINTLN(F("[WledCloud] WebSocket disconnected"));
        break;

      case WStype_TEXT: {
        DynamicJsonDocument doc(2048);
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err) { DEBUG_PRINTLN(F("[WledCloud] JSON parse error")); return; }

        const char* msgType = doc["type"] | "";
        if (strcmp(msgType, "command") == 0) {
          // Buffer the command for safe processing in loop() — calling
          // deserializeState() here (inside a WS callback) would invoke
          // strip.suspend()/waitForIt() and trigger the watchdog.
          const char* cmdId = doc["id"] | "";
          strlcpy(pendingCmdId, cmdId, sizeof(pendingCmdId));
          size_t written = serializeJson(doc["state"], pendingStateJson, sizeof(pendingStateJson));
          hasPendingCommand = (written > 0 && written < sizeof(pendingStateJson));
        } else if (strcmp(msgType, "ping") == 0) {
          lastPingRecv = millis();
          sendPong(doc["ts"] | 0);
        } else if (strcmp(msgType, "request_state") == 0) {
          needsStateSync = true;  // defer to loop()
        }
        break;
      }

      case WStype_ERROR:
        wsConnected = false;
        wsConnecting = false;
        DEBUG_PRINTLN(F("[WledCloud] WebSocket error"));
        break;

      default:
        break;
    }
  }

  // ── WebSocket: send pong ─────────────────────────────────────────────────────
  void sendPong(unsigned long serverTs) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"type\":\"pong\",\"ts\":%lu}", millis() / 1000UL);
    ws.sendTXT(buf);
  }

  // ── WebSocket: send state update ─────────────────────────────────────────────
  void sendStateUpdate() {
    if (!wsConnected) return;

    DynamicJsonDocument doc(2048);
    doc["type"] = "state_update";

    JsonObject state = doc["state"].to<JsonObject>();
    state["on"]  = (bri > 0);
    state["bri"] = bri;

    // Primary segment colors
    Segment& seg0 = strip.getMainSegment();
    JsonArray col = state["col"].to<JsonArray>();
    for (int j = 0; j < 3; j++) {
      uint32_t c = seg0.colors[j];
      JsonArray rgb = col.createNestedArray();
      rgb.add(R(c)); rgb.add(G(c)); rgb.add(B(c));
    }
    state["fx"]  = seg0.mode;
    state["sx"]  = seg0.speed;
    state["ix"]  = seg0.intensity;
    state["pal"] = seg0.palette;

    // All active segments
    JsonArray segs = state["seg"].to<JsonArray>();
    for (uint8_t i = 0; i < strip.getSegmentsNum(); i++) {
      Segment& seg = strip.getSegment(i);
      if (!seg.isActive()) continue;
      JsonObject s = segs.createNestedObject();
      s["id"]    = i;
      s["start"] = seg.start;
      s["stop"]  = seg.stop;
      s["grp"]   = seg.grouping;
      s["spc"]   = seg.spacing;
      s["fx"]    = seg.mode;
      s["sx"]    = seg.speed;
      s["ix"]    = seg.intensity;
      s["pal"]   = seg.palette;
      JsonArray sc = s["col"].to<JsonArray>();
      for (int j = 0; j < 3; j++) {
        uint32_t cc = seg.colors[j];
        JsonArray srgb = sc.createNestedArray();
        srgb.add(R(cc)); srgb.add(G(cc)); srgb.add(B(cc));
      }
    }

    doc["ts"] = millis() / 1000UL;

    // Use class-member sendBuf[] instead of stack-local buffer.
    // This function MUST only be called from loop() context.
    size_t len = serializeJson(doc, sendBuf, sizeof(sendBuf));
    ws.sendTXT(sendBuf, len);
    lastStateSync = millis();
  }

  // ── WebSocket: send telemetry ─────────────────────────────────────────────────
  void sendTelemetryMessage() {
    if (!wsConnected || !sendTelemetry) return;

    DynamicJsonDocument doc(256);
    doc["type"]   = "telemetry";
    doc["rssi"]   = WiFi.RSSI();
    doc["heap"]   = (unsigned long)ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000UL;
    doc["ts"]     = millis() / 1000UL;

    char buf[192];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    ws.sendTXT(buf, len);
    lastTelemetry = millis();
  }

  // ── Command execution ────────────────────────────────────────────────────────
  // Called from loop() (never from a WS callback) so strip operations are safe.
  void processCommand(const char* stateJson, const char* cmdId) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, stateJson);
    if (err) {
      DEBUG_PRINTF("[WledCloud] processCommand JSON parse error: %s\n", err.c_str());
      return;
    }

    JsonObject state = doc.as<JsonObject>();

    // Check for OTA command
    if (state.containsKey("ota")) {
      const char* url = state["ota"]["url"] | "";
      if (strlen(url) > 0) handleOta(url, cmdId);
      return;
    }

    // ── _config: runtime conflict policy update ───────────────────────────────
    if (state.containsKey("_config")) {
      JsonObject cfg = state["_config"];
      const char* pol = cfg["conflictPolicy"] | (const char*)nullptr;
      if (pol && strlen(pol) > 0) strlcpy(conflictPolicy, pol, sizeof(conflictPolicy));
      if (cfg.containsKey("conflictLockTtl")) {
        int ttl = cfg["conflictLockTtl"].as<int>();
        localLockTtl = (uint16_t)constrain(ttl, 500, 60000);
      }
      pendingConfigSave = true;
      char buf[128];
      snprintf(buf, sizeof(buf),
               "{\"type\":\"command_ack\",\"id\":\"%s\",\"ts\":%lu}", cmdId, millis() / 1000UL);
      ws.sendTXT(buf);
      DEBUG_PRINTLN(F("[WledCloud] Conflict policy updated from _config"));
      return;
    }

    // ── local-lock-ttl: reject if device was recently interacted with ─────────
    if (strcmp(conflictPolicy, "local-lock-ttl") == 0 && lastLocalChange > 0) {
      unsigned long elapsed = millis() - lastLocalChange;
      if (elapsed < (unsigned long)localLockTtl) {
        uint16_t retryAfter = (uint16_t)(localLockTtl - (uint16_t)elapsed);
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"command_nack\",\"id\":\"%s\",\"reason\":\"local_lock\",\"retryAfter\":%u}",
                 cmdId, retryAfter);
        ws.sendTXT(buf);
        DEBUG_PRINTF("[WledCloud] command_nack: local_lock, retry in %ums\n", retryAfter);
        return;
      }
    }

    // ── cloud-priority: arm the local-change suppression window ──────────────
    {
      uint32_t holdMs = state["_cloudHold"] | 0U;
      if (holdMs > 0) {
        cloudLockSetAt = millis();
        cloudHoldMs    = holdMs;
      }
    }

    // Apply state using WLED's JSON handler.
    // CALL_MODE_NOTIFICATION prevents onStateChange from echoing back to cloud.
    deserializeState(state, CALL_MODE_NOTIFICATION);

    // Send acknowledgment
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"command_ack\",\"id\":\"%s\",\"ts\":%lu}", cmdId, millis() / 1000UL);
    ws.sendTXT(buf);
    DEBUG_PRINTF("[WledCloud] command_ack sent for %s\n", cmdId);
  }

  // ── OTA update ──────────────────────────────────────────────────────────────
  void handleOta(const char* url, const char* cmdId) {
    DEBUG_PRINTF("[WledCloud] OTA update from %s\n", url);
    // Send ack before starting update (device will reboot)
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"command_ack\",\"id\":\"%s\",\"status\":\"updating\",\"ts\":%lu}",
             cmdId, millis() / 1000UL);
    ws.sendTXT(buf);
    ws.loop(); // flush

    disconnectWebSocket();

#if defined(ESP32) || defined(ESP8266)
    WiFiClient client;
    HTTPClient http;
    http.begin(client, url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      DEBUG_PRINTF("[WledCloud] OTA HTTP error: %d\n", code);
      http.end();
      return;
    }

    int contentLen = http.getSize();
    if (!Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN)) {
      DEBUG_PRINTLN(F("[WledCloud] OTA Update.begin failed"));
      http.end();
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t otaBuf[256];
    int written = 0;
    while (http.connected() && (contentLen < 0 || written < contentLen)) {
      size_t available = stream->available();
      if (available == 0) { delay(1); continue; }
      if (available > sizeof(otaBuf)) available = sizeof(otaBuf);
      int bytes = stream->readBytes(otaBuf, available);
      if (bytes > 0) {
        if (Update.write(otaBuf, bytes) != (size_t)bytes) {
          DEBUG_PRINTLN(F("[WledCloud] OTA write error"));
          http.end();
          return;
        }
        written += bytes;
      }
    }
    http.end();

    if (Update.end(true)) {
      DEBUG_PRINTLN(F("[WledCloud] OTA success — rebooting"));
      delay(200);
      ESP.restart();
    } else {
      DEBUG_PRINTF("[WledCloud] OTA finalize failed: %d\n", Update.getError());
    }
#endif
  }

  // ── Reconnect backoff ────────────────────────────────────────────────────────
  unsigned long getReconnectDelay() const {
    unsigned long base = 1000UL << min(reconnectAttempts, (uint8_t)5);
    if (base > 30000UL) base = 30000UL;
    long jitter = (long)(base / 4);
    return base + (unsigned long)random(-jitter, jitter + 1);
  }

 public:
  // ── Lifecycle ─────────────────────────────────────────────────────────────────
  void setup() override {
    initDone = true;
  }

  void connected() override {
    if (!enabled || !initDone) return;

    if (strlen(deviceToken) > 0) {
      claimState = ClaimState::CONNECT_WS;
    } else {
      claimState = ClaimState::GENERATE_CODE;
    }
  }

  void loop() override {
    if (!enabled || !initDone) return;
    if (!WLED_CONNECTED) return;
    if (strip.isUpdating()) return;

    // Flush pending config save (from claim flow)
    if (pendingConfigSave) {
      pendingConfigSave = false;
      serializeConfigToFS();
    }

    unsigned long now = millis();

    // ── Claim flow state machine ─────────────────────────────────────────────
    switch (claimState) {
      case ClaimState::IDLE:
        break;

      case ClaimState::CHECK_TOKEN:
        if (strlen(deviceToken) > 0) {
          claimState = ClaimState::CONNECT_WS;
        } else {
          claimState = ClaimState::GENERATE_CODE;
        }
        break;

      case ClaimState::GENERATE_CODE:
        generateClaimCode();
        claimStarted = now;
        claimState = ClaimState::REGISTER_CODE;
        break;

      case ClaimState::REGISTER_CODE:
        // Throttle registration attempts to every 10s — each HTTP call blocks
        // for up to 3s on connect timeout; hammering it every loop() would
        // starve the main loop and trigger the ESP32 task watchdog.
        if (now - lastRegisterAttempt < 10000UL && lastRegisterAttempt > 0) break;
        lastRegisterAttempt = now;
        if (registerCodeWithCloud()) {
          lastClaimPoll = now;
          claimState = ClaimState::DISPLAY_CODE;
        } else {
          // Give up and regenerate after 60s of failures
          if (now - claimStarted > 60000UL) {
            claimState = ClaimState::GENERATE_CODE;
          }
        }
        break;

      case ClaimState::DISPLAY_CODE:
        // Poll every 5 seconds
        if (now - lastClaimPoll >= 5000UL) {
          lastClaimPoll = now;
          claimState = ClaimState::POLL;
        }
        // Expire after 5 minutes
        if (now - claimStarted > 300000UL) {
          claimCode[0] = '\0';
          claimState = ClaimState::GENERATE_CODE;
        }
        break;

      case ClaimState::POLL: {
        int result = pollForClaim();
        if (result == 1) {
          // Claimed — save and connect
          claimState = ClaimState::SAVE_TOKEN;
        } else if (result == -1) {
          // Expired — regenerate
          claimCode[0] = '\0';
          claimState = ClaimState::GENERATE_CODE;
        } else {
          // Still pending — go back to display/wait
          claimState = ClaimState::DISPLAY_CODE;
        }
        break;
      }

      case ClaimState::SAVE_TOKEN:
        pendingConfigSave = true;
        claimCode[0] = '\0';
        claimState = ClaimState::CONNECT_WS;
        break;

      case ClaimState::CONNECT_WS:
        connectWebSocket();
        lastReconnect = millis();   // prevent reconnect logic from immediately retriggering
        reconnectAttempts = 0;
        claimState = ClaimState::IDLE; // WS lib takes over from here
        break;
    }

    // ── WebSocket maintenance (only when past claim phase) ───────────────────
    if (claimState == ClaimState::IDLE && strlen(deviceToken) > 0) {
      ws.loop();

      // Apply any buffered command now that we're in loop() and the strip
      // is not updating — safe to call deserializeState().
      if (hasPendingCommand) {
        hasPendingCommand = false;
        processCommand(pendingStateJson, pendingCmdId);
      }

      // Server requested a state sync (deferred from WS callback to loop)
      if (needsStateSync) {
        needsStateSync = false;
        sendStateUpdate();
      }

      // Pending state sync
      if (stateChanged && wsConnected) {
        stateChanged = false;
        sendStateUpdate();
      }

      // Periodic full sync
      if (wsConnected && now - lastStateSync > (unsigned long)syncInterval * 1000UL) {
        sendStateUpdate();
      }

      // Telemetry (offset by half sync interval)
      if (wsConnected && sendTelemetry &&
          now - lastTelemetry > (unsigned long)syncInterval * 500UL) {
        sendTelemetryMessage();
      }

      // Heartbeat timeout — 45s without a ping = dead connection
      // MUST use millis() here (not cached `now`), because ws.loop() above may
      // have fired a callback that set lastPingRecv = millis() — which is later
      // than the cached `now`, causing unsigned wrap-around to a huge value.
      if (wsConnected && lastPingRecv > 0 && millis() - lastPingRecv > 45000UL) {
        DEBUG_PRINTLN(F("[WledCloud] Ping timeout — forcing reconnect"));
        wsConnected = false;
        // Library's built-in reconnect (setReconnectInterval) handles re-establishing
      }
    }
  }

  // ── WLED state change hook ────────────────────────────────────────────────────
  void onStateChange(uint8_t mode) override {
    if (!enabled || !wsConnected) return;
    // Don't echo back changes we just applied from cloud
    if (mode == CALL_MODE_NOTIFICATION) return;
    // cloud-priority: suppress local echoes during the hold window
    if (cloudHoldMs > 0 && millis() - cloudLockSetAt < cloudHoldMs) return;
    // Record timestamp for local-lock-ttl policy
    lastLocalChange = millis();
    stateChanged = true;
  }

  // ── Info panel ────────────────────────────────────────────────────────────────
  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");

    JsonArray cloud = user.createNestedArray(F("Cloud"));
    if (!enabled) {
      cloud.add(F("Disabled"));
    } else if (strlen(claimCode) > 0) {
      // Brief status — full copyable code is in the Usermods settings page
      cloud.add(F("Claiming — see Usermods page for code"));
    } else if (wsConnected) {
      cloud.add(F("Connected"));
    } else if (strlen(deviceToken) > 0) {
      cloud.add(F("Reconnecting..."));
    } else {
      cloud.add(F("Not claimed"));
    }

    if (enabled) {
      JsonArray srv = user.createNestedArray(F("Cloud Server"));
      srv.add(serverHost);

      // Connection info
      JsonArray dbg = user.createNestedArray(F("Cloud Status"));
      char info[48];
      snprintf(info, sizeof(info), "conn=%d att=%d",
               wsConnected, reconnectAttempts);
      dbg.add(info);
    }
  }

  void addToJsonState(JsonObject& root) override {
    if (!initDone || !enabled) return;
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonObject um = user[FPSTR(_name)];
    if (um.isNull()) um = user.createNestedObject(FPSTR(_name));
    um["connected"] = wsConnected;
    um["claimCode"] = (strlen(claimCode) > 0) ? claimCode : "";
  }

  void readFromJsonState(JsonObject& root) override {
    // No state inputs from external JSON for this usermod
  }

  // ── Config persistence ────────────────────────────────────────────────────────
  void addToConfig(JsonObject& root) override {
    JsonObject top = root.createNestedObject(FPSTR(_name));
    top[FPSTR(_enabled)] = enabled;
    top["server"]    = serverHost;
    top["port"]      = serverPort;
    top["tls"]       = useTLS;
    top["token"]     = deviceToken;
    top["venueId"]   = venueId;
    top["syncSec"]   = syncInterval;
    top["telemetry"] = sendTelemetry;
    top["conflictPolicy"]  = conflictPolicy;
    top["localLockTtlMs"]  = localLockTtl;
    // Read-only: claim code shown here so it is selectable/copyable in the
    // Usermods settings page. Omitted once the device is claimed.
    if (strlen(claimCode) > 0) {
      top["claimCode"] = claimCode;
    }
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)];
    bool complete = !top.isNull();
    if (!complete) return false;

    complete &= getJsonValue(top[FPSTR(_enabled)], enabled, false);
    strlcpy(serverHost, top["server"] | "cloud.wled.me", sizeof(serverHost));
    complete &= getJsonValue(top["port"],      serverPort, (uint16_t)3000);
    complete &= getJsonValue(top["tls"],       useTLS, false);
    strlcpy(deviceToken, top["token"]   | "", sizeof(deviceToken));
    strlcpy(venueId,     top["venueId"] | "", sizeof(venueId));
    complete &= getJsonValue(top["syncSec"],   syncInterval, (uint16_t)30);
    complete &= getJsonValue(top["telemetry"], sendTelemetry, true);
    strlcpy(conflictPolicy, top["conflictPolicy"] | "last-write-wins", sizeof(conflictPolicy));
    getJsonValue(top["localLockTtlMs"], localLockTtl, (uint16_t)5000);

    // Clamp syncInterval to sane range
    if (syncInterval < 10)  syncInterval = 10;
    if (syncInterval > 300) syncInterval = 300;
    // Clamp localLockTtl to 500ms–60s
    if (localLockTtl < 500)   localLockTtl = 500;
    if (localLockTtl > 60000) localLockTtl = 60000;

    return complete;
  }

  void appendConfigData() override {
    oappend(F("addInfo('WLEDCloud:server',1,'<i>hostname (no protocol)</i>');"));
    oappend(F("addInfo('WLEDCloud:port',1,'<i>443 for TLS, 80 for plain</i>');"));
    oappend(F("addInfo('WLEDCloud:syncSec',1,'<i>seconds between syncs (10–300)</i>');"));
    oappend(F("addInfo('WLEDCloud:token',1,'<i>set automatically by claim flow</i>');"));
    oappend(F("addInfo('WLEDCloud:venueId',1,'<i>set automatically by claim flow</i>');"));
  }

  uint16_t getId() override { return USERMOD_ID_WLEDCLOUD; }
};

const char WledCloudUsermod::_name[]    PROGMEM = "WLEDCloud";
const char WledCloudUsermod::_enabled[] PROGMEM = "enabled";

static WledCloudUsermod wledCloudInstance;
REGISTER_USERMOD(wledCloudInstance);
