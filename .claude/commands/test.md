Validate the acceptance criteria for firmware ticket $ARGUMENTS (e.g. "M2-005").

Steps:
1. Read `WLED Cloud/tickets/$ARGUMENTS.md` — identify Acceptance Criteria and Scope.
2. Build the firmware: `pio run -e esp32dev_wledcloud`
   - Build must complete with 0 errors (warnings acceptable).
3. For each Acceptance Criterion:
   - If it requires a hardware test: describe the exact test procedure and expected serial output.
   - If it requires a build check: show that the relevant code compiles correctly.
   - If it requires protocol compliance: compare implementation against `WLED Cloud/docs/09-websocket-spec.md` or `docs/12-usermod-spec.md`.
4. Check memory: verify no obvious stack overflows or large static allocations. RAM budget is under 20KB additional.
5. Report ✅/❌ per criterion with output. If all pass → set ticket to `review`. If any fail → leave at `testing` and describe exactly what needs fixing.
