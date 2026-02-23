# User Stories

## Personas

### 1. Installer / Technician
A person setting up the device on-site. They are comfortable with basic networking and web UIs but are not a firmware developer. They need to get the device online and registered on the LNS in minutes.

### 2. Lighting Operator / Venue Tech
A person running a show or event, sending commands via a cloud dashboard or LoRa remote. They need reliable, low-latency command delivery and visible status feedback.

### 3. Cloud/Integration Developer
A developer building the cloud control plane, LNS webhook integration, or custom scene management UI. They need a predictable, documented API contract.

### 4. WLED Power User
A person who already uses WLED and wants to add LoRa control to their existing setup. They need the usermod to be non-disruptive and the WLED UI to remain fully functional.

---

## User Stories

### Installer / Technician

---

**US-01 — Device Self-Provision**
> As an installer, I want the device to generate its own LoRaWAN credentials on first boot so that I don't need external tooling to provision it.

**Acceptance Criteria:**
- On first boot, `devEUI` is derived from the ESP32-S3 MAC address.
- On first boot, `appKey` is generated using the hardware RNG and persisted.
- Credentials survive a power cycle.
- Credentials are NOT re-generated on subsequent boots.

---

**US-02 — Read Credentials from Web UI**
> As an installer, I want to read the device's LoRaWAN credentials from the WLED web interface so that I can register the device on my LNS without connecting a serial monitor.

**Acceptance Criteria:**
- `devEUI`, `appEUI`, and `appKey` are visible on the WLED Info page (`/json/info`).
- Credentials are also shown on the Usermod Settings page in copyable text fields.
- The `appKey` is shown with a show/hide toggle.

---

**US-03 — Commission Without External WiFi**
> As an installer, I want to access the WLED web UI using the device's built-in AP hotspot so that I can commission the device without needing a pre-existing WiFi network on site.

**Acceptance Criteria:**
- On first boot with no WiFi configured, device broadcasts `WLED-AP` hotspot.
- Connecting to the hotspot and navigating to `http://4.3.2.1` opens the full WLED UI.
- The entire commissioning flow (read creds, optionally configure WiFi) is completable via the AP.

---

**US-04 — Confirm Join Status**
> As an installer, I want to see whether the device has successfully joined the LoRaWAN network in the WLED UI so that I can confirm the setup is complete without opening the LNS console.

**Acceptance Criteria:**
- Join status (`joining`, `joined`, `failed`) is visible on the WLED Info page.
- Join timestamp and last uplink age are also shown.
- Status updates within 30 seconds of join completing.

---

### Lighting Operator / Venue Tech

---

**US-05 — Trigger Preset via LoRa**
> As an operator, I want to send a LoRa downlink to switch the device to a named preset so that I can quickly change scenes during a show.

**Acceptance Criteria:**
- A `{"preset": N}` downlink payload triggers `applyPreset(N)` within 500ms.
- If the preset does not exist, the command is dropped and noted in the diagnostics counter.
- The device sends an acknowledgment uplink on success.

---

**US-06 — Power On/Off via LoRa**
> As an operator, I want to send a single-byte LoRa command to turn all lights on or off.

**Acceptance Criteria:**
- Byte `0x00` sets WLED power state to off.
- A subsequent `{"on": true}` or non-zero single-byte command restores power.
- DMX fixtures and addressable LEDs both respond.

---

**US-07 — Global Brightness via LoRa**
> As an operator, I want to send a LoRa command to set the WLED global brightness so that I can dim or brighten all outputs at once.

**Acceptance Criteria:**
- `{"bri": N}` (0-255) sets WLED master brightness.
- Change is applied within 500ms.
- WLED web UI reflects the new brightness.

---

**US-08 — Duplicate Command Idempotency**
> As an operator, I want duplicate LoRa commands to be safely ignored so that network retransmissions don't cause flickering.

**Acceptance Criteria:**
- Commands with duplicate sequence IDs are dropped without applying effects.
- The replay counter in `/json/info` increments on each dropped duplicate.

---

### Cloud / Integration Developer

---

**US-09 — Predictable API Fields**
> As a developer, I want the usermod's diagnostics to appear at a documented path in `/json/info` and `/json/state` so that my cloud integration can monitor device health reliably.

**Acceptance Criteria:**
- `u.LoRaWLED` key exists in `/json/info` response.
- Fields include: `join_status`, `last_cmd_id`, `last_cmd_age_s`, `dropped`, `replayed`, `loop_warn`.
- Fields are consistent across firmware versions (no unannounced renames).

---

**US-10 — Bulk Provisioning via HTTP**
> As a developer, I want to read device credentials programmatically via HTTP so that I can automate registration of multiple devices.

**Acceptance Criteria:**
- `GET /json/info` returns `devEUI` and `appEUI` in `u.LoRaWLED`.
- `appKey` is NOT returned in `/json/info` (security).
- `appKey` is only accessible via the Settings UI (human-facing copy flow).

---

**US-11 — Command Test Vector Compliance**
> As a developer, I want every LoRa command type defined in the command spec to produce the documented WLED action so that my LNS payload formatter can rely on the device behaving as specified.

**Acceptance Criteria:**
- All command types in [08-command-spec.md](08-command-spec.md) are implemented.
- Each command type produces the WLED action documented in the test vector table.
- Malformed payloads increment `dropped` counter and produce no visual change.

---

### WLED Power User

---

**US-12 — Transparent WLED Operation**
> As a WLED power user, I want the LoRa usermod to be invisible during normal WLED use so that all existing effects, presets, and settings work exactly as they do without the usermod.

**Acceptance Criteria:**
- The WLED web UI loads and operates at full speed with the usermod enabled.
- Existing WLED presets, effects, segments, and palettes all function normally.
- The usermod can be disabled via its config toggle without requiring a firmware rebuild.

---

**US-13 — WiFi and AP Mode Unaffected**
> As a WLED power user, I want the LoRa usermod to not interfere with WLED's WiFi AP mode or station mode so that I can still access the web UI whether or not I have a WiFi network available.

**Acceptance Criteria:**
- Device boots in AP mode if no WiFi is configured (standard WLED behavior).
- Device connects to WiFi if configured (standard WLED behavior).
- LoRa operation continues correctly in both modes.
