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

### Calibration: implement-phase forecast over-estimates when PRD has verbatim diffs
- **Symptom:** Forecasts consistently over-ran by 30-50% on nf5.1, .2/.3, .4, .5, .8 — all beads where the PRD had verbatim file diffs and the work was mechanical copy-and-verify rather than derivation.
- **Root cause:** Forecasts were sized using "comparable prior items" rationale, which assumes some derivation work. When the PRD IS the contract (file:line + exact replacement text given), the implement phase is much shorter.
- **Suggested fix:** When sizing a bead at `triage:ready`, if Cynefin is `clear` or `complicated` AND the description's Changes Needed table cites verbatim PRD content (vs. paraphrased intent), multiply implement-phase turn estimate by 0.6-0.8. Document this as a sizing note in `work-item-templates.md` Effort Forecast section.

### Patterns worth promoting to rules/development-standards.md (firmware-specific)
- **Logging:** Always `Log.print(...)` from `<Logger.h>` (extern via globals.h), never `Serial.print(...)`. Reason: Log mirrors to TCP port 6053 for remote debugging.
- **Cross-module calls:** Both the caller and the callee's #include must be guarded by the same `#ifdef HAS_X`. Reason: any unguarded reference breaks non-HAS_X builds at link time.
- **New module gating:** `#ifdef HAS_FEATURE` at file top wrapping the entire body; defaults macros in defaults.h gated by the same flag. Reason: empty TU is free at link time; runtime `if (!enabled) return` wastes flash.
- **Suggested fix:** Discovered during nf5.6 implementation. File a chore bead to promote these to `rules/development-standards.md` with proper CORRECT/WRONG examples once 2-3 more modules cite them.

## 2026-05-02 — feature/athom-plug-v3 close-out session

### Gap: scribe-refine inlining a PRD verbatim block transcribed Svelte syntax incorrectly
- **What was needed:** Faithful copy of `ui/src/routes/hardware/+page.svelte` Svelte template-binding syntax into the bead description.
- **Symptom:** `nf5.11`'s refined Changes Needed contained `placeholder={.defaults['relay_pin']}` and `bind:value={.values['relay_pin']}` — the leading store name `$hardwareSettings` was lost. A naive copy-paste would have compiled (Svelte tolerates the partial-object reference) but never bound to the actual store, causing silent default-population breakage in the captive portal.
- **Workaround used:** Read existing pattern in `+page.svelte:408-417` before pasting; manually corrected to `{$hardwareSettings.defaults[...]}` / `{$hardwareSettings.values[...]}`.
- **Suggested fix:** When `scribe-refine` inlines a PRD verbatim block that includes templating/binding syntax, it should cross-check the contract against an existing reference implementation in the target file (the bead's "Patterns to Reuse" already names lines 408-417 — refine should diff against those). Alternatively: require the implementer to verify the verbatim block against a named reference pattern as a Readiness Checklist item, not just trust the PRD.

### Gap: UI build deps (node_modules) not tracked; `/leroy` health check doesn't catch this
- **Symptom:** Running `npm run build` after editing the Svelte file returned `sh: 1: vite: not found`. `architecture.md § Environment Health Checks` doesn't include a check for `ui/node_modules` presence.
- **Workaround used:** `cd ui && npm install`.
- **Resolution:** [RESOLVED] — Added `UI deps` row to `architecture.md § Environment Health Checks` (commit during nf5.14 close-out).

### Retro 2026-05-02 — what went well, what burned time, calibration

**Went well:**
- Parallel `scribe-refine` dispatch on three beads concurrently — three ready beads in one round-trip rather than three sequential cycles.
- Build verification after every commit (athom + esp32 + esp32c3) caught nothing but kept confidence high; no rework.
- Catching the Svelte syntax artifact in nf5.11 *before* pasting verbatim — saved a silent runtime breakage that wouldn't have surfaced as a compile error.
- Auto-close of `ESPresense-nf5` epic when `nf5.14` closed — confirms beads dependency graph is correctly modeling the epic boundary.

**Burned time:**
- `cd ui && npm install` was an unanticipated detour because `node_modules` is gitignored and there was no first-time-setup checklist for UI work. RESOLVED via the new health-check row.
- Re-reading existing Svelte pattern before pasting added ~2 turns; the bead's "Patterns to Reuse" section (lines 408-417) was the saving grace — without it I'd have either trusted the broken `{.defaults[...]}` syntax or grep'd blindly.

**Pattern worth promoting (workflow guidance, not code rule):**
- **Rule:** When a refined bead's Changes Needed inlines a verbatim block of templating/binding/markup (Svelte, JSX, Mustache, etc.), do not paste blind. Cross-check against an existing reference implementation in the same file (typically named in "Patterns to Reuse") and reconcile any syntactic divergence before applying.
- **Why:** Templating engines often tolerate partial syntax (Svelte accepts `{.foo['bar']}` as a valid expression — just not the one we want), so the failure surfaces as a runtime misconfiguration rather than a compile error. Build verification doesn't catch it.
- **How to apply:** Add a single read of the cited reference range to the implementation pre-flight when the bead inlines templating syntax. Skip when the inlined block is plain code or PRD-grade verbatim text.
- **Suggested fix for scribe-refine:** When inlining a verbatim block from PRD that targets a file already cited in "Patterns to Reuse", the agent should diff the verbatim block against the reference range in the target file and surface any syntactic mismatch as a refine warning.

**Calibration validation:**
- `nf5.9` (PRD verbatim diffs) ran ~10t actual vs ~14t forecast = 0.71x — confirms the prior session's 0.6-0.8x calibration note for cynefin:complicated beads with verbatim PRD content.
- `nf5.11` (PRD verbatim diffs but with first-time UI setup overhead + syntax artifact correction) ran ~12t actual vs ~9t forecast = 1.33x. The overrun was non-implementation work (env bootstrap + bead-defect repair), not the implementation itself. **Implication:** the 0.6-0.8x calibration applies to *implementation phase* but should not be applied if the bead's environment isn't already proven-clean (no reference build run prior). For greenfield UI work, the first session always pays the npm-install tax — bake that into the test-phase estimate.

---

## Resolved (prior sessions)

### Gap: scribe-init has no template/label/triage-state contract for bead creation
- **Symptom:** Beads created via scribe-init have title + type only. No description, labels, or triage state. Forced manual stub-template touchup post-creation.
- **Workaround used:** Wrote scribe-init patch + new scribe-refine agent in `.archive/upstream-patches/`; applied locally.
- **Resolution:** [RESOLVED] — local file at `.claude/agents/scribe/scribe-init.md` updated to v2.0.0; new `.claude/agents/scribe/scribe-refine.md` added; SKILL.md registered the new agent.
