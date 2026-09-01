Implement the firmware ticket specified in $ARGUMENTS (e.g. "M2-005").

Steps:
1. Read the ticket at `WLED Cloud/tickets/$ARGUMENTS.md` — check Spec Refs, Scope, Acceptance Criteria, Depends On.
2. Confirm any "Depends On" tickets are `completed`.
3. **Update the ticket Status to `in-progress` now**, before writing any code.
4. Read every spec in Spec Refs. Always read `WLED Cloud/docs/12-usermod-spec.md` and `WLED Cloud/docs/09-websocket-spec.md`.
5. Only modify files listed in the ticket's Scope — all within `usermods/wledcloud/`. Do not touch `wled00/`.
6. Implement the changes. Follow rules in `.claude/rules/firmware.md`. Key constraints:
   - No `delay()` > 10ms
   - `serializeConfig()` only from `loop()`
   - `CALL_MODE_NOTIFICATION` for cloud-applied state
   - `PROGMEM` for constant strings
7. Build to verify: `pio run -e esp32dev_wledcloud`
8. Check off each Acceptance Criterion satisfied. If all pass, set Status to `testing`.
9. Summarize: which files changed, which acceptance criteria satisfied, any blockers.
