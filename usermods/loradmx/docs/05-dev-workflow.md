# Development Workflow

## Core Principle: Spec-Driven Development

No code is written without a spec. No spec exists without a ticket. No ticket is closed without passing tests and a reviewer approval. This loop is enforced by role discipline (see [06-agent-team.md](06-agent-team.md)).

---

## The Cycle

```
1. SPEC       -> Architect writes or updates a doc in docs/
2. TICKET     -> Architect writes a ticket in tickets/ (references the spec doc)
3. IMPLEMENT  -> Coder builds the ticket against the spec
4. VERIFY     -> Tester validates all acceptance criteria
5. REVIEW     -> Reviewer approves (or requests changes)
6. MERGE      -> Change lands in the fork's main branch
7. VALIDATE   -> Full build + test run confirms no regressions
```

---

## Step-by-Step Rules

### Step 1 — Spec
- The Architect owns all documents in `docs/`.
- Before any ticket is written, the relevant spec doc must exist and cover the area.
- If a new area of work emerges that isn't covered by a spec doc, the Architect writes or updates the spec first.
- Spec documents are living documents -- they are updated if decisions change.
- All design decisions are documented in the spec, not in code comments or chat.

### Step 2 — Ticket
- Tickets live in `usermods/loradmx/tickets/` as individual `.md` files.
- Every ticket has: ID, title, role assignee, status, description, spec references, and acceptance criteria.
- Tickets are written by the Architect before implementation begins.
- The Coder may not start a ticket until all acceptance criteria are written and unambiguous.
- If the Coder has a question, they raise it to the Architect. The Architect answers in the ticket file (under `## Clarifications`) and updates the spec if needed.

### Step 3 — Implement
- The Coder picks the next `Open` ticket (no cherry-picking out of dependency order).
- Before writing any code; read the ticket fully, read all linked spec docs, confirm no open questions.
- Implement only what the ticket asks for. Scope creep goes to a new ticket.
- Run `npm run build` before any changes (ensures web UI headers are current).
- Self-review against acceptance criteria before marking as `Ready for Test`.
- Commit message format: `[MVP-NNN] Brief description of change`

### Step 4 — Verify
- The Tester picks up the ticket when status is `Ready for Test`.
- Each acceptance criterion is tested explicitly. Result is written into the ticket file.
- Two types of tests:
  - **Compilation test:** `pio run -e heltec_loradmx` must succeed with zero errors.
  - **Regression test:** `npm test` must pass (validates web UI build system).
  - **On-device test** (for MVP-007 onwards): Flash to Heltec V3, send test vectors from [08-command-spec.md](08-command-spec.md), verify WLED response.
- If any criterion fails: status -> `Blocked`, route back to Coder with specific failure note.

### Step 5 — Review
- Reviewer checks the diff and the completed test results.
- Checks: no unintended core file changes, no hardcoded pins/values, correct use of WLED hooks, backwards-compatible DMX patch.
- Decision: `Approved` or list of required changes.
- If changes requested: routes back to Coder, no re-review from Tester needed for small changes unless logic changed.

### Step 6 — Merge
- Once approved, the change merges into the fork's working branch.
- The ticket status is updated to `Closed`.
- The `README.md` ticket table is updated to show the new status.

### Step 7 — Validate
- After every merge, run:
  ```bash
  npm test               # web UI build system validation (~40s)
  pio run -e heltec_loradmx   # firmware compilation (~15-20min first run)
  ```
- If either fails, the merge is reverted immediately and a new ticket is opened.

---

## Ticket Status Lifecycle

```
not-started -> in-progress -> in-review -> blocked (fail) -> in-progress (retry)
                                        -> completed
```

| Status | Meaning |
|--------|--------|
| `not-started` | Ready to be picked up by Coder |
| `in-progress` | Coder is actively building |
| `in-review` | Coder self-review complete, handed to Tester or Reviewer |
| `blocked` | Test or review found a failure; back to Coder or Architect |
| `completed` | Reviewer approved and merged |
| `backlog` | Phase 2 placeholder; not ready for work |

---

## Dependency Order (MVP)

Tickets must be completed in dependency order. Some can be parallelized.

```
MVP-001 (Data Contract)
    |
    +-> MVP-002 (Module Scaffold)
            |
            +-> MVP-003 (Hardware Init)    <-- Parallel with MVP-005
            +-> MVP-005 (Device Identity) <-- Parallel with MVP-003
                    |
                    +-> MVP-006 (Commissioning UI)
            |
            +-> MVP-004 (Radio Loop)        <-- After MVP-003
            |
            +-> MVP-007 (Command Parser)    <-- After MVP-001, MVP-002
            |
            +-> MVP-008 (WLED State Mapper) <-- After MVP-007
            |
            +-> MVP-009 (Config Schema)     <-- After MVP-002
            |
            +-> MVP-010 (Diagnostics)       <-- After MVP-007, MVP-008
            |
            +-> MVP-011 (Uplink Policy)     <-- After MVP-004
            |
MVP-012 (DMX Pin Patch) -- Independent, can start anytime
MVP-013 (PlatformIO Env) -- After MVP-009, MVP-012
MVP-014 (Build Verification) -- After MVP-013
MVP-015 (WiFi+LoRa Coexistence) -- After MVP-014
```

---

## Branch Strategy

| Branch | Purpose |
|--------|---------|
| `main` | Stable WLED upstream fork base |
| `loradmx/dev` | Active development; all MVP tickets merge here |
| `loradmx/release` | Validated builds promoted here |

Never commit directly to `main`. All work goes through `loradmx/dev`.

---

## Build Commands Reference

```bash
# Build web UI (run before any hardware build)
npm run build

# Run web UI build system tests
npm test

# Build firmware for Heltec V3 with loradmx usermod
pio run -e heltec_loradmx

# Flash to device
pio run -e heltec_loradmx --target upload

# Clean build artifacts
pio run --target clean
```
