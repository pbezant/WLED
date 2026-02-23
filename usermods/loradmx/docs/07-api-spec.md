# API Spec: WLED JSON Fields

The `loradmx` usermod injects additional fields into three WLED JSON endpoints. All fields are namespaced under `u.LoRaDMX` to prevent collisions.

---

## `/json/info` — Device Information

The usermod adds a `LoRaDMX` key to the `u` (usermod) object.

### Endpoint
`GET http://<device>/json/info`

### Added Fields

```json
{
  "u": {
    "LoRaDMX": {
      "devEUI": "70B3D5FFFE012345",
      "joinEUI": "0000000000000000",
      "joinState": "joined",
      "credentialsProvisioned": true,
      "rssi": -85,
      "snr": 7.5,
      "lastUplink": 240,
      "lastDownlink": 12,
      "fCntUp": 128,
      "fCntDown": 42,
      "uptimeSeconds": 3600,
      "dropped": 3,
      "replayed": 1,
      "loopWarn": false,
      "version": "0.1.0"
    }
  }
}
```

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `devEUI` | string (16 hex) | Device EUI derived from MAC. Stable across reboots. |
| `joinEUI` | string (16 hex) | Join EUI / Application EUI. Configurable. |
| `joinState` | enum string | `"not_joined"`, `"joining"`, `"joined"`, `"join_failed"` |
| `credentialsProvisioned` | bool | True after first-boot credential generation. |
| `rssi` | number \| null | Last downlink RSSI (dBm). null if not yet joined. |
| `snr` | number \| null | Last downlink SNR. null if not yet joined. |
| `lastUplink` | uint32 | Seconds since last uplink TX. |
| `lastDownlink` | uint32 | Seconds since last downlink RX. |
| `fCntUp` | uint32 | Uplink frame counter. |
| `fCntDown` | uint32 | Downlink frame counter. |
| `uptimeSeconds` | uint32 | Seconds since device boot (`millis()/1000`). |
| `dropped` | uint32 | Count of downlinks dropped (malformed or unrecognized). |
| `replayed` | uint32 | Count of downlinks dropped due to duplicate cmdId. |
| `loopWarn` | bool | True if `loop()` cadence exceeded 2ms warning threshold. |
| `version` | string | Usermod semantic version. |

**Security note:** `appKey` is NOT included in `/json/info` or `/json/state`. It is write-only — set via `/json/cfg` POST, never returned.

---

## `/json/state` — Live Device State

The usermod adds read-only status to the `u` object in the state response.

### Endpoint
`GET http://<device>/json/state`

### Added Fields

```json
{
  "u": {
    "LoRaDMX": {
      "enabled": true,
      "joinState": "joined",
      "lastCmdResult": "ok",
      "dropped": 3,
      "replayed": 1,
      "overflow": 0
    }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | bool | Whether the LoRa usermod is currently active. |
| `joinState` | enum string | Current join status (mirrors `/json/info`). |
| `lastCmdResult` | enum string | `"ok"`, `"preset_not_found"`, `"parse_error"`, `"drop"` |
| `dropped` | uint32 | Commands dropped since last reboot. |
| `replayed` | uint32 | Commands dropped due to replay since last reboot. |
| `overflow` | uint32 | Ring buffer overflow count since last reboot. |

### State Commands (Write)
The usermod also responds to commands posted to `/json/state`. This allows local HTTP control using the same command format as LoRa downlinks:

```json
POST /json/state
{
  "loraCmd": {"preset": 5}
}
```

| Field | Type | Description |
|-------|------|-------------|
| `loraCmd` | object | A LoRa command payload (same format as LoRa downlinks). Useful for testing without a LoRa gateway. |

---

## `/json/cfg` — Usermod Configuration

Usermod settings are stored and loaded via WLED's config system (`cfg.json`). They appear under the `um` key.

### Read Config
`GET http://<device>/json/cfg`

```json
{
  "um": {
    "LoRaDMX": {
      "enabled": true,
      "joinEUI": "0000000000000000",
      "appKey": "***HIDDEN***",
      "pinSck": 9,
      "pinMiso": 11,
      "pinMosi": 10,
      "pinNss": 8,
      "pinRst": 12,
      "pinBusy": 13,
      "pinDio1": 14,
      "uplinkInterval": 300000,
      "joinRetryInterval": 30000,
      "cmdThrottleMs": 100
    }
  }
}
```

### Write Config
`POST http://<device>/json/cfg`
```json
{
  "um": {
    "LoRaDMX": {
      "enabled": true,
      "joinEUI": "CustomJoinEUI000"
    }
  }
}
```

### Config Field Descriptions

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | bool | `true` | Enable/disable the usermod at runtime |
| `joinEUI` | string (16 hex) | `"0000000000000000"` | Join EUI for OTAA. Configurable per LNS requirement. |
| `appKey` | string (32 hex) | auto-generated | AES-128 application key. **Write-only** — never returned in GET responses. |
| `pinSck` | int | 9 | SPI2 SCK pin (Heltec V3 PCB trace) |
| `pinMiso` | int | 11 | SPI2 MISO pin |
| `pinMosi` | int | 10 | SPI2 MOSI pin |
| `pinNss` | int | 8 | Radio chip select pin |
| `pinRst` | int | 12 | Radio reset pin |
| `pinBusy` | int | 13 | Radio busy pin |
| `pinDio1` | int | 14 | Radio interrupt pin |
| `uplinkInterval` | uint32 | 300000 | Minimum ms between uplinks (default: 5 minutes). Minimum enforced: 60000. |
| `joinRetryInterval` | uint32 | 30000 | ms between OTAA join retries when not joined |
| `cmdThrottleMs` | int | 100 | Minimum ms between applying successive commands (rate limit) |

**Note on pin changes:** If pin values are changed and saved, a reboot is required for them to take effect.
