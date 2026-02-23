# Ticket Lifecycle: End-to-End Workflow

This document describes how a ticket moves from idea to merged code. Each step specifies who acts, what model runs, and what artifact is produced.

---

## Workflow Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         LORAWLED TICKET LIFECYCLE                            │
└─────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────┐
  │  BACKLOG     │  tickets/ with status: not-started
  └──────┬───────┘
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │  ARCHITECT  (Claude Opus — lorawled-architect.prompt.md)  │
  │                                                          │
  │  1. Select next ticket from dependency order             │
  │  2. Read all linked spec docs                            │
  │  3. Verify: AC written? Spec refs linked? No open ?s?   │
  │  4. If gaps: update spec doc first, then ticket          │
  │  5. Set ticket status → in-progress                      │
  │  6. Hand off to Coder                                    │
  └──────────────────────────┬───────────────────────────────┘
                             │ Ticket ready
                             ▼
  ┌──────────────────────────────────────────────────────────┐
  │  CODER  (Claude Sonnet — lorawled-coder.prompt.md)        │
  │                                                          │
  │  1. Read ticket + all spec refs                          │
  │  2. Implement exactly what ticket describes              │
  │  3. Run: npm run build                                   │
  │  4. Run: pio run -e heltec_lorawled                       │
  │  5. Self-review against each AC checkbox                 │
  │  6. If ambiguity found → open question in ticket →       │
  │     route back to Architect                              │
  │  7. Hand off to Tester                                   │
  └──────────────────────────┬───────────────────────────────┘
           ▲                 │ Build passes, self-review done
           │  Spec gap       ▼
           │         ┌──────────────────────────────────────────────────────────┐
           │         │  TESTER  (GPT Codex/o3 — lorawled-tester.prompt.md)      │
           │         │                                                          │
           │         │  1. Run static checks (grep for violations)             │
           │         │  2. Run: npm run build + npm test                       │
           │         │  3. Run: pio run -e heltec_lorawled + esp32dev           │
           │         │  4. Run test vectors (parser/mapper tickets)            │
           │         │  5. Check every AC checkbox                             │
           │         │  6. Add ## Test Results section to ticket               │
           │         │  7a. If FAIL (code) → route back to Coder ─────────────┤
           │         │  7b. If FAIL (spec)  → route back to Architect ─────────┘
           │         │  7c. If PASS → hand off to Reviewer                     │
           │         └──────────────────────────┬───────────────────────────────┘
           │                                    │ All ACs pass
           │                                    ▼
           │         ┌──────────────────────────────────────────────────────────┐
           │         │  REVIEWER  (GPT Codex/o3 — lorawled-reviewer.prompt.md)  │
           │         │                                                          │
           │         │  1. Verify Tester verdict is PASS, no open blockers     │
           │         │  2. Work through review checklist (A→G)                 │
           │         │  3. Check diff against spec, WLED boundaries, security  │
           │         │  4a. If REQUEST CHANGES → route back to Coder ──────────┤
           │         │  4b. If APPROVED → add ## Review section to ticket      │
           │         │  5. Set ticket status → completed                       │
           │         └──────────────────────────┬───────────────────────────────┘
           └── Coder ◄── Change req ─────────── │ APPROVED
                                                 ▼
                                          ┌────────────┐
                                          │   MERGED   │
                                          └────────────┘
```

---

## Dependency Order (MVP)

Tickets must be started in this order. A ticket cannot begin until all dependencies have status `completed`.

```
MVP-001 (Data Contract)
    └── MVP-007 (Parser)
            └── MVP-008 (Mapper)

MVP-002 (Scaffold)
    ├── MVP-003 (HW Init)
    │       └── MVP-004 (Radio Loop)
    │               └── MVP-007 (Parser) ──── see above
    ├── MVP-005 (Credentials)
    │       ├── MVP-004 (Radio Loop)
    │       └── MVP-006 (Commissioning UI)
    └── MVP-009 (Config Schema)
            ├── MVP-004 (Radio Loop)
            └── MVP-011 (Uplink Policy)

MVP-012 (DMX Pin Patch)
    └── MVP-013 (PlatformIO Env)
            └── MVP-014 (Build Verification) ← needs ALL tickets complete

MVP-004 + MVP-008
    └── MVP-015 (WiFi+LoRa Coexistence)

MVP-004 + MVP-007
    └── MVP-010 (Diagnostics)
```

---

## Status Values

| Status | Meaning |
|--------|---------|
| `not-started` | In backlog, not ready or not scheduled |
| `blocked` | Waiting on a dependency or spec gap |
| `in-progress` | Architect has released it, Coder is working |
| `in-review` | Coder handed off; Tester or Reviewer active |
| `completed` | Reviewer approved, merged |

---

## How to Start a Session

When starting a new work session, open the agent prompt for your role from `.github/prompts/`:

| Role | Command in VS Code |
|------|-------------------|
| Architect | `> GitHub Copilot: Open Prompt` → `lorawled-architect` |
| Coder | `> GitHub Copilot: Open Prompt` → `lorawled-coder` |
| Tester | `> GitHub Copilot: Open Prompt` → `lorawled-tester` |
| Reviewer | `> GitHub Copilot: Open Prompt` → `lorawled-reviewer` |

Then tell the agent which ticket ID to work on (e.g., `Work on MVP-003`).

---

## Escalation Policy

If an agent is stuck (cannot proceed without information outside its role):

1. **Coder stuck on spec gap** → add `## Open Questions` to ticket, set status `blocked`, notify Architect
2. **Tester stuck on flaky hardware** → document in Test Results, set status `in-review` with note, do not block indefinitely
3. **Reviewer finds architectural issue** → open new sub-ticket (e.g., `MVP-003a`), do not merge until resolved
4. **Build system broken** → Coder owns recovery; all other agents pause that ticket

---

## Files Produced Per Ticket Lifecycle

| Step | Agent | Files Modified |
|------|-------|---------------|
| Spec ready | Architect | `tickets/MVP-XXX.md` (status → in-progress) |
| Implementation | Coder | `usermods/lorawled/*.h`, `*.cpp`, patches |
| Test results | Tester | `tickets/MVP-XXX.md` (adds `## Test Results`) |
| Review | Reviewer | `tickets/MVP-XXX.md` (adds `## Review`, status → completed) |
