# MVP-003 — Hardware Init / SPI2

| Field | Value |
|-------|-------|
| **ID** | MVP-003 |
| **Title** | Hardware Init: SX1262 on SPI2 (FSPI) |
| **Role** | Coder |
| **Status** | completed |
| **Priority** | P0 |
| **Spec Refs** | [03-architecture.md](../03-architecture.md), [10-build-spec.md](../10-build-spec.md) |
| **Depends On** | MVP-002 |
| **Blocks** | MVP-004 |

---

## Description

Initialize the SX1262 radio hardware in `setup()` using a dedicated `SPIClass` on SPI2 (FSPI). The SX1262 PCB traces on the Heltec WiFi LoRa 32 V3 are hardwired — no flexibility. Do **not** use the default `SPI` object.

---

## Pin Assignments (Fixed by PCB)

| Signal | GPIO |
|--------|------|
| SCK | 9 |
| MISO | 11 |
| MOSI | 10 |
| NSS (CS) | 8 |
| RST | 12 |
| BUSY | 13 |
| DIO1 (IRQ) | 14 |

---

## Scope

- Instantiate `SPIClass loraSPI(FSPI)` as a file-scope variable
- Call `loraSPI.begin(9, 11, 10, 8)` in `setup()`
- Allocate pins via `PinManager::allocatePin()` with `PinOwner::UM_Unspecified`
- Initialize `SX126xRadio` (or equivalent from `SX126x-Arduino`) with above pins
- Expose `radioReady` bool (true = init succeeded, false = init failed)
- Log init result to serial: `[LoRaDMX] Radio init OK` / `[LoRaDMX] Radio init FAILED`

---

## Acceptance Criteria

- [x] `setup()` completes without panic or watchdog reset on target hardware
- [x] Serial log shows `[LoRaDMX] Radio init OK` on successful init
- [x] `radioReady` is `true` after successful init
- [x] All 7 SX1262 GPIOs are registered with PinManager
- [x] No default `SPI` object usage — `lora_hardware_init()` calls `SPI_LORA.begin(sck, miso, mosi, nss)` internally (not the default `SPI`)
- [x] `pio run -e heltec_loradmx` compiles without errors (SUCCESS 33.9s)

---

## Implementation Notes

- Used `lora_hardware_init(hw_config)` from SX126x-Arduino v2 (not direct Radio.Init)
- `hw_config` populated with all 7 pin numbers + SX1262_CHIP, TCXO_CTRL_1_8V via DIO3 (Heltec V3 uses 1.8 V 32 MHz TCXO), USE_LDO=false (DC-DC regulator)
- The library's `initSPI()` (in `boards/mcu/espressif/spi_board.cpp`) calls `SPI_LORA.begin(sck, miso, mosi, nss)` internally, so no separate `_loraSPI.begin()` call is needed or correct
- Removed `_loraSPI.begin()` from `setup()` — `_loraSPI` member retained in header for documentation
- Static instance pointer `s_loraDmxInstance` set in `setup()` prior to callbacks being registered
- `_lastJoinAttemptMs` initialized to `millis() - _joinRetryInterval` so first join fires on the very next `loop()` call

---

## Out of Scope

- LoRaWAN stack initialization (MVP-004)
- Credential loading (MVP-005)
