# LoRa-WLED WLED Usermod — Documentation Catalog

This folder contains all spec, architecture, workflow, and API documentation for the `lorawled` WLED usermod. Documents are numbered in reading order. Tickets live in `tickets/`.

---

## Spec Documents

| # | Document | Description |
|---|----------|-------------|
| 01 | [Project Brief](01-brief.md) | What this project is, why it exists, success criteria, and constraints |
| 02 | [User Stories](02-user-stories.md) | Who uses this, what they need, and acceptance criteria per persona |
| 03 | [Architecture](03-architecture.md) | System diagram, component breakdown, data flow, integration boundaries |
| 04 | [Tech Stack](04-tech-stack.md) | All technologies, libraries, versions, and rationale |
| 05 | [Dev Workflow](05-dev-workflow.md) | How development works: spec -> ticket -> agent implementation -> review -> merge |
| 06 | [Agent Team](06-agent-team.md) | The four agent roles, responsibilities, handoff protocol, and ownership matrix |
| 07 | [API Spec](07-api-spec.md) | WLED JSON API fields added by the usermod (state, info, config) |
| 08 | [Command Spec](08-command-spec.md) | LoRa downlink payload format: all command types with byte-level encoding |
| 09 | [Commissioning](09-commissioning.md) | Step-by-step device provisioning guide: flash -> credentials -> LNS -> join |
| 10 | [Build Spec](10-build-spec.md) | PlatformIO environment, build flags, partitions, and DMX TX pin patch |

---

## Tickets

### MVP

| ID | Title | Status |
|----|-------|--------|
| [MVP-001](../tickets/MVP-001-data-contract.md) | Finalize LoRa command data contract | ✅ completed |
| [MVP-002](../tickets/MVP-002-scaffold.md) | Scaffold usermod module structure | ✅ completed |
| [MVP-003](../tickets/MVP-003-hw-init.md) | Hardware initialization (SPI2, PinManager, radio setup) | ✅ completed |
| [MVP-004](../tickets/MVP-004-radio-loop.md) | Non-blocking radio loop and latency watchdog | ✅ completed |
| [MVP-005](../tickets/MVP-005-credentials.md) | Device identity and credential generation | ✅ completed |
| [MVP-006](../tickets/MVP-006-commissioning-ui.md) | Commissioning UI (credentials in Info/Settings pages) | ✅ completed |
| [MVP-007](../tickets/MVP-007-command-parser.md) | LoRa downlink command parser | ✅ completed |
| [MVP-008](../tickets/MVP-008-wled-mapper.md) | WLED state mapper (commands -> WLED API calls) | ✅ completed |
| [MVP-009](../tickets/MVP-009-config-schema.md) | Usermod config schema and persistence | ✅ completed |
| [MVP-010](../tickets/MVP-010-diagnostics.md) | Diagnostics and status exposure (JSON info/state) | ✅ completed |
| [MVP-011](../tickets/MVP-011-uplink-policy.md) | Uplink policy and duty cycle management | ✅ completed |
| [MVP-012](../tickets/MVP-012-dmx-pin-patch.md) | DMX TX pin patch (GPIO 2 -> configurable) | ✅ completed |
| [MVP-013](../tickets/MVP-013-platformio-env.md) | PlatformIO build environment (heltec_lorawled) | ✅ completed |
| [MVP-014](../tickets/MVP-014-build-verification.md) | Build verification and compilation validation | ✅ completed |
| [MVP-015](../tickets/MVP-015-wifi-lora-coexistence.md) | WiFi + LoRaWAN Class C coexistence validation | ⚠️ in-progress |

### Phase 2 (Placeholder)

| ID | Title | Status |
|----|-------|--------|
| [PHASE2-001](../tickets/PHASE2-001-raw-dmx-channels.md) | Per-fixture DMX channel control over LoRa | backlog |
| [PHASE2-002](../tickets/PHASE2-002-cloud-tenancy.md) | Cloud tenancy and site-agent contract | backlog |
| [PHASE2-003](../tickets/PHASE2-003-conflict-policy.md) | Multi-writer conflict policy | backlog |
| [PHASE2-004](../tickets/PHASE2-004-canary-ota.md) | Canary rollout and OTA validation | backlog |

---

## Reading Order for New Contributors

1. [01-brief.md](01-brief.md) — understand the project
2. [06-agent-team.md](06-agent-team.md) — understand your role
3. [05-dev-workflow.md](05-dev-workflow.md) — understand the process
4. [03-architecture.md](03-architecture.md) — understand the system
5. Review completed MVP tickets to understand what has been implemented

## MVP Milestone Status

**14 / 15 tickets completed.** The MVP milestone is functionally complete and
builds successfully (`heltec_lorawled`: Flash 57.1%, RAM 26.1%).

| Remaining | Blocker |
|-----------|---------|
| [MVP-015](../tickets/MVP-015-wifi-lora-coexistence.md) — T1/T2/T3/T5 scenarios | Requires physical Heltec V3 hardware for hardware-in-loop testing |
