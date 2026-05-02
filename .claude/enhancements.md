# Documentation Enhancements

Log of gaps discovered during sessions. Each entry: what was needed, where the gap is, suggested fix. Mark `[RESOLVED]` once the target doc is updated.

---

## 2026-05-01 — feature/athom-plug-v3 implementation session

### Gap: PlatformIO not in default PATH; setup step undocumented
- **What was needed:** `pio` CLI to build firmware envs.
- **Symptom:** `pio --version` returned `command not found` at session start. `architecture.md § Environment Health Checks` lists `pio --version` as a check but doesn't tell `/leroy` what to do when it fails.
- **Workaround used:** `python3 -m venv .venv-pio && .venv-pio/bin/pip install platformio`. Added `.venv-pio/` to `.gitignore`.
- **Suggested fix:** Update `architecture.md § Local Development` to include venv bootstrap step. Or add a `scripts/setup-pio.sh` helper. Either way, `/leroy`'s health check should suggest the fix command when `pio` is missing.

### Gap: Workflow doesn't address bead descriptions that point to PRD instead of inlining spec
- **What was needed:** Clear policy on whether `Open Questions: None — see PRD § X` is a valid form for `triage:ready`.
- **Symptom:** `nf5.6` and `nf5.7` shipped at `triage:triaged` with Stub Template + PRD pointer. Implementation proceeded by reading PRD directly. Closed bead descriptions are now permanent records that don't reflect what was built.
- **Workaround used:** Bypassed triage gate at claim time (flagged to user); backfilled AC on closed beads at wrapup.
- **Suggested fix:** Either (a) amend `work-item-templates.md` Readiness Checklist to allow precise PRD-citation as a valid spec-link form, OR (b) require all PRD content to be inlined into the bead body before promotion to ready. The `scribe-refine` agent (added this session, see `.archive/upstream-patches/scribe-init-template-aware-creation.md`) is option (b).

### Gap: Effort forecasts use the 3-arg phase format inconsistently across templates
- **What was needed:** Single source-of-truth on the Effort Forecast contract.
- **Symptom:** Some Phase 1 beads had per-phase forecasts (plan/implement/test + Total + Confidence). Others had only "Rough Size Estimate: M" stub-template format. `bd lint` doesn't catch the variance — it warns on missing AC but not on missing per-phase Effort Forecast.
- **Suggested fix:** Add `bd lint` rule to flag missing per-phase Effort Forecast on items at `triage:ready`. Update `work-item-templates.md` Readiness Checklist to make per-phase explicit (it already says this — make `bd lint` enforce it).

### Gap: navigator-recon listed triage:triaged beads in "Ready to Start"
- **Symptom:** First `/leroy` of session listed `nf5.11` (triage:triaged) as selectable. Survey caught it; maintenance dispatch wouldn't have.
- **Workaround used:** Wrote 3 navigator patches to `.archive/upstream-patches/`; applied locally.
- **Resolution:** [RESOLVED] — local files at `.claude/agents/navigator/{recon,maintenance,survey}.md` updated to v1.2.0.

### Gap: scribe-init has no template/label/triage-state contract for bead creation
- **Symptom:** Beads created via scribe-init have title + type only. No description, labels, or triage state. Forced manual stub-template touchup post-creation.
- **Workaround used:** Wrote scribe-init patch + new scribe-refine agent in `.archive/upstream-patches/`; applied locally.
- **Resolution:** [RESOLVED] — local file at `.claude/agents/scribe/scribe-init.md` updated to v2.0.0; new `.claude/agents/scribe/scribe-refine.md` added; SKILL.md registered the new agent.
