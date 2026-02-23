# Agent Team

## Overview

The `lorawled` project uses four agent roles organized by **responsibility**. Each role owns a slice of the work, produces specific artifacts, and hands off to the next role via defined interfaces. All agents reference the spec documents in this `docs/` folder.

Agent prompt files live in `.github/prompts/` and are loaded directly by VS Code Copilot. See `docs/flows/ticket-lifecycle.md` for the end-to-end workflow.

---

## Model Assignments

| Role | Model | Prompt File |
|------|-------|-------------|
| Architect | Claude Opus (`claude-opus-4-5`) | `.github/prompts/lorawled-architect.prompt.md` |
| Coder | Claude Sonnet (`claude-sonnet-4-5`) | `.github/prompts/lorawled-coder.prompt.md` |
| Tester | GPT Codex (`o3`) | `.github/prompts/lorawled-tester.prompt.md` |
| Reviewer | GPT Codex (`o3`) | `.github/prompts/lorawled-reviewer.prompt.md` |

---

## The Four Roles

### 1. Architect
**Model:** Claude Opus (`claude-opus-4-5`)
**Prompt:** `.github/prompts/lorawled-architect.prompt.md`
**Owns:** Spec documents, system design, data contracts, and integration boundaries.

**Does:**
- Writes and maintains all docs in `docs/` (brief, architecture, command spec, API spec, etc.)
- Makes all binding design decisions (recorded in the plan and as doc updates)
- Reviews proposed changes for architectural consistency before they are implemented
- Defines the acceptance criteria for each ticket
- Resolves ambiguity questions from Coder or Tester before work begins

**Does NOT:**
- Write implementation code
- Approve tests without reviewing the corresponding spec

**Output artifacts:**
- `docs/` spec documents
- Ticket definitions in `tickets/`
- Architecture diagrams (updated in `03-architecture.md`)
- Answers to Coder/Tester clarification questions (documented in the relevant ticket)

**Handoff to Coder:** Ticket is complete (acceptance criteria written, spec references linked, no open questions).

---

### 2. Coder
**Model:** Claude Sonnet (`claude-sonnet-4-5`)
**Prompt:** `.github/prompts/lorawled-coder.prompt.md`
**Owns:** Implementation of all C++ firmware code in `usermods/lorawled/` and the DMX TX pin patch.

**Does:**
- Implements each ticket's requirements exactly as specified in the ticket and linked spec docs
- Follows existing WLED code style (2-space indents, tabs in web files)
- Uses WLED's existing hooks (`setup()`, `loop()`, `addToConfig`, etc.) -- no inventing new patterns
- Writes inline code comments for non-obvious decisions
- Flags spec gaps or ambiguities to the Architect before guessing
- Runs `npm run build` before any work touching web UI
- Must not modify WLED core files except the documented DMX TX pin patch

**Does NOT:**
- Decide what to build (that's the Architect's job)
- Approve their own work (that's the Reviewer's job)

**Output artifacts:**
- `usermods/lorawled/*.cpp`, `*.h`
- `usermods/lorawled/library.json`
- `platformio_override.ini` env entry
- DMX pin patch changes in `wled00/src/dependencies/dmx/` and `wled00/wled.cpp`

**Handoff to Tester:** Implementation is complete, compiles without errors, and a self-review has been done against the ticket's acceptance criteria.

---

### 3. Tester
**Model:** GPT Codex (`o3`)
**Prompt:** `.github/prompts/lorawled-tester.prompt.md`
**Owns:** Verification that the implementation meets the acceptance criteria defined in each ticket.

**Does:**
- Validates each acceptance criterion from the ticket with a pass/fail result
- Uses the test vectors in `docs/08-command-spec.md` for functional testing
- Runs `npm test` and verifies all existing WLED tests still pass
- Runs `pio run -e heltec_lorawled` to verify the firmware compiles
- On-device tests: flashes firmware to Heltec V3, sends LoRa downlinks, checks WLED response
- Documents test results in the ticket file (adds a `## Test Results` section)
- Opens a **blocker** back to the Architect or Coder if a criterion fails

**Does NOT:**
- Fix failing code (routes back to Coder)
- Change acceptance criteria (routes back to Architect)

**Output artifacts:**
- Updated ticket file with `## Test Results` section
- List of any new defect tickets opened

**Handoff to Reviewer:** All acceptance criteria pass, `npm test` passes, firmware compiles.

---

### 4. Reviewer
**Model:** GPT Codex (`o3`)
**Prompt:** `.github/prompts/lorawled-reviewer.prompt.md`
**Owns:** Final code quality, consistency, and merge readiness.

**Does:**
- Reviews the full diff for the ticket against the spec and acceptance criteria
- Checks for: style consistency, no unintended WLED core changes, no hardcoded values that should be configurable, no missing `PinManager` allocations, correct use of WLED hooks
- Confirms the DMX TX pin patch is guarded with `#ifndef` and backwards-compatible
- Approves or requests changes before merge

**Does NOT:**
- Change acceptance criteria (Architect)
- Re-run tests (Tester already did)
- Implement fixes (Coder)

**Output artifacts:**
- Approval comment on ticket or list of required changes
- Merge decision

---

## Handoff Flow

```
Architect                 Coder                  Tester               Reviewer
    |                        |                      |                      |
    |-- Ticket ready ------> |                      |                      |
    |   (AC written,         |                      |                      |
    |    spec linked)        |-- Build + self-review|                      |
    |                        |-- Impl done -------> |                      |
    |                        |                      |-- Run tests         |
    |                        |                      |-- Pass?             |
    |                        |  <-- Blocker (fail)  |                      |
    |  <-- Spec gap          |                      |                      |
    |-- Answer + update spec |                      |                      |
    |                        |-- Re-implement -------> (retest)            |
    |                        |                      |-- All pass -------> |
    |                        |                      |                      |-- Review
    |                        |                      |               Pass ->|-- Merge
    |                        |  <-- Change req ---------- (fail)          |
    |                        |-- Fix -> resubmit -----------------------> |
```

---

## Ownership Matrix

| Area | Architect | Coder | Tester | Reviewer |
|------|-----------|-------|--------|----------|
| `docs/` spec documents | OWNER | Reader | Reader | Reader |
| `tickets/` definitions | OWNER | Reader | UPDATER (test results) | Reader |
| `usermods/lorawled/*.cpp/.h` | Spec | OWNER | Validator | Approver |
| `usermods/lorawled/library.json` | Spec | OWNER | Validator | Approver |
| `platformio_override.ini` | Spec | OWNER | Validator | Approver |
| DMX pin patch files | Spec | OWNER | Validator | Approver |
| WLED core files | No-touch (spec only) | Patch-only | Validator | Approver |
| Test results in tickets | N/A | N/A | OWNER | Reader |

---

## Rule: No Spec, No Ticket, No Code

**The Architect must complete the relevant spec document and ticket before the Coder begins any implementation.** If an implementation need is discovered mid-build that isn't in the spec, work stops and the Architect updates the spec first.
