## Plan: LoRa-DMX via WLED Usermod

We'll integrate LoRa-DMX into WLED through a **Usermod v2** compiled into a custom WLED build, while keeping core WLED source untouched. The result is a **LoRaWAN end device** that runs WLED natively -- it receives LoRa downlinks and translates them into WLED state/segment/effect updates, and sends uplinks with device status. The device is **not a bridge** -- it is a self-contained WLED controller with an integrated LoRa radio that can drive **addressable LEDs (WS2812B, SK6812, etc.)** and/or **DMX fixtures (par cans, movers, etc.)** -- all controlled over LoRa. Users can access the WLED web UI either by connecting the device to an existing WiFi network, or by using WLED's built-in AP mode (the device creates its own hotspot, no external WiFi required). Your existing LoRa command model and codecs remain the source of truth; the usermod is a thin adapter layer. For multi-tenant cloud, we keep a managed control plane and local site agent pattern so tenant isolation, auth, and audit stay outside WLED.

### Target Hardware

- **MCU/Board:** Heltec WiFi LoRa 32 V3 -- **ESP32-S3** + SX1262, 8MB Flash, 8MB PSRAM
- **Board ID:** `heltec_wifi_lora_32_V3` (NOT generic `esp32dev`)
- **Partition table:** `tools/WLED_ESP32_8MB.csv` -- 2MB per app slot (vs. 1.5MB on 4MB default), required for LoRa stack headroom
- **LoRaWAN:** Class C, US915 subband 2, DR4, OTAA via `SX126x-Arduino@^2.0.30` + `LoraManager2`
- **DMX transceiver:** Grove RS485 breakout (Seeed 103020000, MAX485) connected to **GPIO 19** (TX) / **GPIO 20** (RX) via UART1

#### SPI Bus Allocation (LoRa Radio)

The SX1262 LoRa radio is hardwired to specific GPIOs on the Heltec V3 PCB:

| Function | GPIO | Bus |
|----------|-------|-----|
| SCK      | 9     | SPI2 (FSPI) |
| MISO     | 11    | SPI2 (FSPI) |
| MOSI     | 10    | SPI2 (FSPI) |
| NSS (CS) | 8     | GPIO |
| RST      | 12    | GPIO |
| BUSY     | 13    | GPIO |
| DIO1     | 14    | Interrupt |

These pins are PCB traces -- not configurable. The usermod **must initialize its own `SPIClass` instance** on SPI2 (FSPI) to avoid conflicts with WLED's default SPI bus. Compatibility:

| WLED Output Mode | SPI Conflict? | Notes |
|---|---|---|
| WS2812B, SK6812, WS2811 (single-wire) | None | Uses RMT/I2S |
| DMX via MAX485 on UART1 (GPIO 19) | None | Uses UART |
| Art-Net / DDP over WiFi | None | Network |
| APA102, SK9822, WS2801 (SPI LEDs) | Must use SPI3 (HSPI) | Cannot share SPI2 with radio |

#### DMX Wiring (Grove RS485 → DMX)

```
Heltec V3 GPIO 19 (TX1)  ──→  DI (Data In) on Grove RS485
Heltec V3 GPIO 20 (RX1)  ──→  RO (Data Out) on Grove RS485  (optional, for DMX input)
Heltec V3 GND             ──→  GND on Grove RS485
Heltec V3 3.3V            ──→  VCC on Grove RS485
Grove RS485 A             ──→  DMX XLR Pin 3 (Data+)
Grove RS485 B             ──→  DMX XLR Pin 2 (Data-)
                               DMX XLR Pin 1 (Shield/GND)
```

### Output Modes

The device supports two output modes -- both are native WLED features, not custom code:

**1. Addressable LEDs** (WS2812B, WS2811, SK6812, etc.)
- Standard WLED behavior. Connect LED data wire to a GPIO pin.
- Full access to all WLED effects, palettes, segments, and presets.
- No additional configuration needed beyond normal WLED LED setup.

**2. DMX Fixtures** (par cans, wash lights, movers, etc.)
- Uses WLED's **built-in serial DMX output** (`WLED_ENABLE_DMX`) via the **Grove RS485 breakout** on **GPIO 19** (UART1 TX).
- Default WLED DMX TX pin is GPIO 2 (hardcoded in bundled `SparkFunDMX.cpp` and `ESPDMX.cpp`). GPIO 2 is not exposed on the Heltec V3. **Requires a minimal local patch** to make the pin overridable via `-D DMX_TX_PIN=19` (see "DMX TX Pin Patch" below).
- WLED maps each LED/segment to DMX channels using a configurable fixture map (R, G, B, W, shutter/dimmer, up to 15 channels per fixture).
- Configure fixture layout via WLED's DMX settings page (`/settings/dmx`).
- WLED renders effects internally, then outputs the result as DMX -- so all WLED effects work on DMX fixtures too.
- Also supports **E1.31-to-DMX proxy mode** (`e131ProxyUniverse`) for forwarding network sACN directly to the DMX line.
- **Art-Net and DDP network output** are also available for driving remote LED controllers over WiFi.

Both modes can run simultaneously -- e.g., addressable LEDs on one segment, DMX fixtures on another.

### Scope Exclusions (not ported from LoRa-DMX firmware)

The usermod is a thin adapter, **not** a port of the full LoRa-DMX firmware. The following are explicitly excluded:
- **Custom `DmxController` / `esp_dmx` output** -- WLED has its own built-in DMX output (bundled SparkFunDMX / ESPDMX libraries); no need to port the custom controller
- **WiFi management** -- WLED handles WiFi; the usermod must coexist with WLED's always-on WiFi
- **OTA via LoRa downlink** -- Use WLED's built-in OTA exclusively (saves flash, avoids conflicts)
- **`Preferences` storage** -- Use WLED's `addToConfig`/`readFromConfig` instead

### Device Identity & Credential Generation

The device **generates its own LoRaWAN credentials on first boot** -- the user copies them into their LNS (TTN, ChirpStack, Helium, etc.), not the other way around.

- **`devEUI`**: Derived deterministically from the ESP32-S3's unique MAC address (`ESP.getEfuseMac()`). Guaranteed unique per chip. Formatted as 16 hex chars (e.g., `70B3D5FFFE012345`).
- **`appKey`**: Generated randomly on first boot using the ESP32's hardware RNG (`esp_random()`). 128-bit / 32 hex chars. Persisted in `cfg.json` via `addToConfig`. Only generated once -- subsequent boots read the stored value.
- **`appEUI` / `joinEUI`**: Defaults to `0000000000000000` (acceptable for TTN and most LNS). Configurable in usermod settings if the LNS requires a specific value.

Credentials are displayed on the WLED **Info page** (`/json/info`) and on the **Usermod Settings page** so the user can read and copy them. The `appKey` is shown in the settings UI for copy purposes but can be hidden behind a "show/hide" toggle for security.

### WiFi + LoRaWAN Coexistence

The device operates in two WiFi modes (standard WLED behavior):
- **AP mode** (default on first boot): Device creates its own hotspot (`WLED-AP`). User connects directly -- no external WiFi needed. Ideal for standalone installations.
- **Station mode**: Device connects to an existing WiFi network. User accesses WLED UI via the device's IP on the local network.

In both modes, the LoRa radio operates independently on SPI. Early MVP must validate that `SX126x-Arduino` Class C operation is stable with WiFi active on the same ESP32-S3. If degradation is found: reduce WiFi TX power, adjust LoRa RX window timing, or gate LoRa operations around WiFi activity.

### Commissioning Workflow

1. **Flash firmware** to the Heltec V3 via USB (or WLED OTA for subsequent updates).
2. **Power on** -- device boots, generates `devEUI` (from MAC) and `appKey` (random) on first boot, starts WLED AP mode.
3. **Connect to WLED AP** from phone/laptop, navigate to `http://4.3.2.1`.
4. **Read credentials** from the Info page or Usermod Settings page: `devEUI`, `appKey`, and `appEUI`.
5. **Register device on LNS** (TTN Console, ChirpStack, etc.) -- create a new OTAA device using the credentials from step 4.
6. **(Optional) Configure WiFi** -- if site has WiFi, enter SSID/password so the device joins the local network. If not, leave in AP mode.
7. **Device joins automatically** -- the usermod continuously attempts OTAA join until successful. Join status is visible on the Info page.
8. **Verify** -- confirm "Joined" status on the WLED Info page and on the LNS console.

**Bulk provisioning:** For deploying many devices, read credentials programmatically:
```bash
# Read generated credentials from a freshly-flashed device
curl http://4.3.2.1/json/info | jq '.u.LoRaDMX'
# Or if on local network:
curl http://<device-ip>/json/info | jq '.u.LoRaDMX'
```
Credentials can also be overridden via POST if needed (e.g., re-keying):
```bash
curl -X POST http://<device-ip>/json/cfg \
  -H "Content-Type: application/json" \
  -d '{"um":{"LoRaDMX":{"appEUI":"custom-join-eui"}}}'
```

---

**Steps (MVP)**
1. Finalize integration boundary and data contract using existing project artifacts in [src/main.cpp](/Users/prestonbezant/_Developement/LoRa-DMX/src/main.cpp), [ttn_payload_formatter.js](/Users/prestonbezant/_Developement/LoRa-DMX/ttn_payload_formatter.js), and [chirpstack_codec.js](/Users/prestonbezant/_Developement/LoRa-DMX/chirpstack_codec.js), defining canonical commands for scene trigger + segment-level update.
2. Define the usermod interface contract (Usermod v2 hooks): ingest command payloads, map to WLED state, expose telemetry in JSON info/state, and persist usermod settings through config methods. **Registration uses `REGISTER_USERMOD(instance)` macro** (linker-section based -- there is no `usermods_list.cpp`).
3. Create usermod module structure in WLED fork under `usermods/loradmx/` with documented responsibilities: command translation, rate limiting, idempotency, and safe fallback behavior. **Include a `library.json` with `"libArchive": false`** and LoRa library dependencies (required by `load_usermods.py` and validated by `validate_modules.py`).
4. **Hardware & Timing:** Implement hardware initialization (`setup()`) using WLED's `PinManager` (with `PinOwner::UM_Unspecified`) for SPI/Radio pins. **Initialize a dedicated `SPIClass` on SPI2 (FSPI) for the SX1262 -- do not use WLED's default SPI bus.** Implement a non-blocking radio service routine (`loop()`) to handle RX/TX without triggering WDT resets. Call `lora.loop()` at the **top** of the usermod `loop()` before any guards. Keep the `Ticker`-based heartbeat (runs in interrupt context, independent of main loop). Add a latency watchdog: if `loop()` cadence drops below 200ms, set a degraded-mode flag in diagnostics.
5. **MVP Command Model (simplified):** Map LoRa commands to high-level WLED operations -- **not** raw DMX fixture channels. MVP commands:
   - **Preset trigger:** `{"preset": N}` -> `applyPreset(N)` -- primary control mechanism
   - **Power on/off:** `0x00` / `{"on": false}` -> toggle WLED power state
   - **Global brightness:** `{"bri": N}` -> set WLED brightness
   - **Segment color override:** `{"seg":[{"id":0,"col":[[R,G,B]]}]}` -> set color (auto-switch to Solid effect)
   - **Named colors:** `0x01`-`0x04` -> map to presets or hardcoded segment colors
   - **Pattern presets:** `0xF1` + type -> map pattern types (rainbow, strobe, chase) to WLED effect/preset IDs
   - Raw RGBW per-fixture DMX control deferred to Phase 2 (requires DMX-address-to-segment mapping layer).
6. Add usermod config schema for operational controls: enabled flag, source mode, update throttle, default segment policy, cloud/site identifiers, and **configurable radio pins (MISO, MOSI, SCK, CS, DIO1, RST, BUSY with Heltec V3 defaults)**. LoRaWAN identity is **auto-generated on first boot** (`devEUI` from MAC, `appKey` from hardware RNG) and persisted in `cfg.json`. `appEUI` defaults to all-zeros but is configurable. Credentials are exposed in **both** the Info page and Settings UI so the user can copy them into their LNS.
7. Add status and diagnostics exposure (`addToJsonInfo`/`addToJsonState`) for cloud observability: last command ID, apply result, dropped/replayed counters, last update age, degraded-mode flags, and **loop cadence warning flag**.
8. **Uplink Policy:** Define a strict uplink strategy to respect LoRaWAN duty cycles (e.g., only ack LoRa commands, throttle local state changes). Keep the `Ticker`-based binary heartbeat (6 bytes, 20s interval). JSON heartbeat only on command acknowledgment, with fallback to trimmed payloads to fit 242-byte LoRaWAN limit.
9. **Build Pipeline:**
   - Create `[env:heltec_loradmx]` in `platformio_override.ini` targeting `board = heltec_wifi_lora_32_V3` with `platform = ${esp32s3.platform}`.
   - Use `board_build.partitions = tools/WLED_ESP32_8MB.csv` for 2MB app slots.
   - Set `custom_usermods = loradmx`.
   - Disable non-essential features to recover ~45KB+ Flash:
     ```
     -D WLED_DISABLE_ALEXA
     -D WLED_DISABLE_HUESYNC
     -D WLED_DISABLE_INFRARED
     -D WLED_DISABLE_LOXONE
     -D WLED_DISABLE_ADALIGHT
     -D WLED_DISABLE_ESPNOW
     -D WLED_ENABLE_DMX
     -D DMX_TX_PIN=19
     -D WLED_RELEASE_NAME="LoRa-DMX"
     ```
   - Usermod `library.json` declares dependencies:
     ```json
     {
       "name": "loradmx",
       "build": { "libArchive": false },
       "dependencies": {
         "beegee-tokyo/SX126x-Arduino": "^2.0.30",
         "https://github.com/pbezant/LoraManager2.git": "*"
       }
     }
     ```
   - **Validate early** for ArduinoJson version conflicts between WLED's bundled version and `SX126x-Arduino` expectations.

### DMX TX Pin Patch (minimal local change)

WLED's bundled DMX libraries hardcode GPIO 2 as the TX pin. The Heltec V3 does not expose GPIO 2 -- DMX output uses GPIO 19 (UART1 TX) via the Grove RS485 breakout. A small `#ifndef` guard makes the pin overridable via build flag without changing DMX logic:

**`SparkFunDMX.cpp`** (ESP32):
```cpp
// Before: static const int txPin = 2;
// After:
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
static const int txPin = DMX_TX_PIN;
```

**`ESPDMX.cpp`** (ESP8266/C3/S2):
```cpp
// Before: int sendPin = 2;
// After:
#ifndef DMX_TX_PIN
#define DMX_TX_PIN 2
#endif
int sendPin = DMX_TX_PIN;
```

**`wled.cpp`** (pin reservation):
```cpp
// Before: PinManager::allocatePin(2, true, PinOwner::DMX);
// After:  PinManager::allocatePin(DMX_TX_PIN, true, PinOwner::DMX);
```

With `-D DMX_TX_PIN=19` in build flags, DMX output routes to GPIO 19. Default behavior (GPIO 2) is preserved for all other builds. This patch can be upstreamed to WLED as a non-breaking improvement.

### Usermod ID & Pin Ownership (no core modifications)

Define the usermod ID in the usermod's own header -- no changes to `const.h` or `pin_manager.h`:
```cpp
#ifndef USERMOD_ID_LORADMX
#define USERMOD_ID_LORADMX 59
#endif
```
Use `PinOwner::UM_Unspecified` for pin allocations in the MVP. A proper `PinOwner::UM_LoRaDMX` can be upstreamed to WLED later as a minimal, guarded patch.

---

**Steps (Phase 2 / Production)**
10. **Per-fixture DMX channel control over LoRa:** Add a LoRa command type that maps directly to WLED's DMX fixture map -- e.g., `{"dmx":{"start":1,"channels":[255,0,128,0,200]}}` sets DMX channels starting at address 1. This bypasses effects and writes raw channel values, useful for direct fixture control from the cloud.
11. Define site-agent + cloud control-plane contract for tenancy: tenant-scoped auth, command signing, replay protection, drift detection, and reconciliation against WLED observed state.
12. Add conflict policy for multiple writers (cloud, local app, presets): explicit precedence model and timestamp/version-based reconciliation.
13. Validate rollout process: canary firmware cohorts, rollback strategy, OTA gating criteria, and compatibility matrix by WLED version/device class.

---

**Verification**
- Build verification: custom WLED image compiles with usermod enabled for `heltec_loradmx` environment using 8MB partition table.
- **WiFi+LoRa coexistence verification:** Class C RX operates reliably with WLED's WiFi active; measure packet loss and join reliability.
- API verification: usermod fields appear in `/json/state` and `/json/info`, and updates are round-trippable. Auto-generated LoRaWAN credentials (`devEUI`, `appKey`) are visible on the Info page and Settings UI for user copy-to-LNS workflow.
- **Commissioning verification:** Device generates unique `devEUI` + `appKey` on first boot, credentials survive reboot, and OTAA join succeeds after user registers device on LNS.
- Functional verification: LoRa command fixtures from existing codec test vectors produce expected WLED outcomes (see test vector table below).
- Robustness verification: malformed/late/duplicate commands are safely handled with deterministic status codes.
- Multi-tenant verification: control plane prevents cross-tenant device access and logs all command/audit events.
- Operational verification: canary rollout succeeds with rollback tested and documented.

### Test Vector Table

| Downlink Input | Expected WLED Action |
|---|---|
| `0x00` | Power off (`{"on":false}`) |
| `0x01` | Set all segments to red or apply "red" preset |
| `0x02` | Set all segments to green or apply "green" preset |
| `0x03` | Set all segments to blue or apply "blue" preset |
| `0x04` | Set all segments to white or apply "white" preset |
| `0xAA` | Test: set all segments green |
| `0xF1 01 C8 00 03 00` | Apply rainbow effect preset (type=1, speed=200, cycles=3) |
| `0xF0` | Stop pattern: revert to previous state or Solid |
| `{"preset":5}` | `applyPreset(5)` |
| `{"bri":128}` | Set global brightness to 128 |
| `{"on":false}` | Power off |
| `{"seg":[{"id":0,"col":[[255,0,128]]}]}` | Set segment 0 to pink, effect=Solid |
| Malformed / truncated payload | No-op, increment dropped counter, log warning |
| Duplicate command ID | Idempotent: skip, increment replay counter |

---

**Decisions**
- Chosen path: **Custom-compiled WLED with Usermod v2**.
- Device role: **LoRaWAN end device** -- not a bridge. Self-contained WLED controller with integrated LoRa radio.
- Network access: **WLED AP mode** (standalone hotspot) or **Station mode** (joins existing WiFi). Both work; WiFi is optional for LoRa operation.
- Credential model: **Device generates its own identity** (`devEUI` from MAC, `appKey` from hardware RNG). User copies credentials into LNS, not the other way around.
- Constraint respected: **No WLED core modifications** -- `USERMOD_ID` defined in usermod header with `#ifndef` guard, `PinOwner::UM_Unspecified` used for pins.
- Target board: **Heltec WiFi LoRa 32 V3 (ESP32-S3)** with 8MB Flash partition table.
- Control scope: **Preset trigger + global brightness + segment color** for MVP (no raw DMX channel control over LoRa until Phase 2).
- Output modes: **Addressable LEDs (WS2812B, SK6812, etc.)** and/or **DMX fixtures via MAX485 on GPIO 2** (`WLED_ENABLE_DMX`). Both are native WLED features; usermod is output-agnostic.
- Cloud topology: **Managed cloud + local site agent** for secure tenant isolation and NAT-safe operation.
- OTA strategy: **WLED built-in OTA only** -- LoRa-triggered OTA excluded to save flash and avoid conflicts.
- Registration: **`REGISTER_USERMOD()` macro** (linker-section based, no `usermods_list.cpp`).

## Delta update

- Plan updated from "external-only stock WLED" to "usermod-enabled custom build".
- Architecture now includes explicit usermod responsibilities, config schema, and release lifecycle.
- Verification now includes firmware channel rollout and multi-writer conflict handling.
- MVP scope refined to include hardware pin management, non-blocking radio loops, uplink duty-cycle policies, and feature-trimmed custom PlatformIO builds.
- Target board specified as Heltec WiFi LoRa 32 V3 (ESP32-S3) with 8MB partition table.
- `library.json` requirement documented with `libArchive: false` and LoRa dependencies.
- WiFi + LoRaWAN coexistence risk identified with mitigation strategies.
- LoRaWAN join credentials added to config schema (stored in `cfg.json`, not exposed in JSON API).
- MVP command model simplified to preset/brightness/power/color (raw DMX fixture control deferred to Phase 2).
- `DmxController`, `esp_dmx` output, WiFi management, and LoRa OTA explicitly excluded from usermod scope.
- `REGISTER_USERMOD()` macro documented (no `usermods_list.cpp`).
- `USERMOD_ID` and `PinOwner` strategy defined without core file modifications.
- Class C loop cadence monitoring and degraded-mode flag added.
- Test vector table added with concrete downlink -> WLED action mappings.
- Device reframed as **LoRaWAN end device** (not a bridge). Usermod renamed from `loradmx_bridge` to `loradmx`.
- On-device credential generation added: `devEUI` from ESP32 MAC, `appKey` from hardware RNG, generated on first boot.
- Commissioning workflow added: flash -> boot -> read creds from WLED UI -> register on LNS -> device auto-joins.
- Credentials exposed in Info page and Settings UI for user copy-to-LNS workflow.
- WiFi modes documented: AP mode (standalone) and Station mode (existing network). WiFi is optional for LoRa operation.
- Dual output support documented: addressable LEDs (native WLED) and DMX fixtures (`WLED_ENABLE_DMX` via MAX485 on GPIO 2). Both outputs work with all WLED effects. Usermod is output-agnostic.
- `WLED_ENABLE_DMX` added to build flags. Custom `DmxController` remains excluded (WLED's built-in DMX output is used instead).
- Art-Net and DDP network output noted as available for driving remote controllers over WiFi.
- SPI bus allocation documented: SX1262 on SPI2 (FSPI), WLED defaults on SPI3 (HSPI) if needed.
- DMX wiring documented: Grove RS485 (Seeed 103020000) on GPIO 19 (TX) / GPIO 20 (RX) via UART1.
- DMX TX pin patch documented: `#ifndef DMX_TX_PIN` guard in SparkFunDMX.cpp, ESPDMX.cpp, and wled.cpp to override hardcoded GPIO 2 with `-D DMX_TX_PIN=19`.
