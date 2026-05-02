# Patch: navigator-maintenance — add triage gate (refuse non-ready beads)

**Target repo:** vibe-templates
**Target file:** `agents/navigator/navigator-maintenance.md`
**Type:** Bug fix (defense in depth)
**Severity:** Medium — maintenance is the lightest-weight dispatch path and the most likely to silently bypass the triage gate
**Companion:** `navigator-recon-triage-filter.md` (the canonical fix; this patch closes the same hole at a different entry point)

---

## Summary

`navigator-maintenance` accepts a bead ID + `bd show` output and produces a fix plan. It does not check the bead's `triage:*` label. A bead at `triage:backlog` (Stub Template only) or `triage:triaged` (sized but missing AC/Effort Forecast/etc.) fails the Readiness Checklist in `work-item-templates.md` and the Start Checklist in `workflow-execution.md`, but maintenance happily produces a plan for it.

If a user (or an orchestrator like `/leroy` after the recon filter is fixed) invokes maintenance directly with such a bead, the resulting plan looks actionable — and the workflow gate gets bypassed silently.

## Real-world failure mode

This session, `ESPresense-nf5.11` was at `triage:triaged`, missing four required template sections. Recon surfaced it as selectable (the bug being fixed in the recon patch). Had `/leroy`'s dispatcher routed it to maintenance instead of survey (it's labeled `size:medium`, borderline for either dispatcher), maintenance would have produced a plan with no triage warning — letting the user act on under-specified work.

Survey caught it in this session through diligence, not contract. Maintenance has neither.

## Root cause

The Procedure (Steps 1-4) goes directly from "parse metadata" to "identify affected files" to "produce fix plan". There is no gate between accepting input and committing to a plan output. The Input Contract explicitly says the dispatcher passes "a single bead ID" and "`bd show` output" — the LABELS line containing `triage:*` is in that output, but Step 1 doesn't read it.

## Fix (unified diff)

```diff
--- a/agents/navigator/navigator-maintenance.md
+++ b/agents/navigator/navigator-maintenance.md
@@ -4,9 +4,10 @@ description: Produces concise action plans for simple, isolated work items — bugs, small chores, config tweaks, doc fixes, minor refactors. Fast and lightweight.
 tools: Read, Grep, Glob, LS, Bash
 model: sonnet
 tier: scale
-version: 1.1.0
+version: 1.2.0
 created: 2026-03-21
 changelog:
+  - 1.2.0 (2026-05-01): Add Triage Gate (Step 0) — refuse to plan beads at triage:backlog or triage:triaged; return Readiness-Gap output instead of a fix plan. Mirrors navigator-recon v1.2.0; closes a direct-invocation bypass of the workflow Start Checklist.
   - 1.1.0 (2026-04-25): Add tracker-conditional blocks
   - 1.0.0 (2026-03-21): Initial version
 ---
@@ -55,6 +56,21 @@ The dispatcher passes via prompt:

 ## Procedure

+### Step 0 — Triage Gate
+
+Before any other work, parse the LABELS line from the `bd show` output and check the `triage:*` label:
+
+- `triage:ready` → proceed to Step 1.
+- No triage label at all (legacy, pre-triage system) → proceed to Step 1.
+- `triage:backlog` → STOP. Skip Steps 1-4. Produce Readiness-Gap output (see below).
+- `triage:triaged` → STOP. Skip Steps 1-4. Produce Readiness-Gap output.
+- No LABELS line in the input (malformed) → STOP. Produce Readiness-Gap output with reason "no labels".
+
+Why: maintenance can be invoked with any bead ID. Beads in backlog or triaged fail the Readiness Checklist (`work-item-templates.md`) and the Start Checklist (`workflow-execution.md`). Producing a fix plan for an under-specified bead leads to an agent claiming work it cannot safely execute. The dispatcher is not responsible for this check — maintenance owns its own triage gate so direct invocations are also covered.
+
+Do not attempt to enrich the bead. Do not search for files. Do not read source. Refuse cleanly and tell the user what's missing.
+
 ### Step 1 — Parse Work Item Metadata

 From the prompt input, extract:
@@ -101,6 +117,18 @@ Write a focused plan. List specific changes — no architecture discussion.
 - [filename.md] — [reason]
 ```

+### Output: Readiness Gap (when Step 0 fails)
+
+If the Triage Gate (Step 0) fails, REPLACE the four standard sections above with this single output. Do not produce both.
+
+```
+## Readiness Gap
+- id: [item-id]
+- triage_state: [backlog / triaged / missing-labels]
+- recommendation: [enrich-then-plan / classify-first]
+- missing_sections: [comma-separated list of template sections the bead lacks per work-item-templates.md, e.g. "Changes Needed, Acceptance Criteria, Effort Forecast"]
+- next_action: [exact bd command or doc reference, e.g. "Edit bead description per work-item-templates.md SMALL Feature Template before claiming. Then: bd set-state <id> triage=ready"]
+```
+
 **Every section is required. If a section has no data, output the section header with "none".**
```

## Justification

**Why a hard refuse, not a soft warning:**

Maintenance produces "concise, actionable plans" by spec. A plan that says "do X, but heads up, the bead isn't fully spec'd" still gets actioned by an agent under load — soft warnings demonstrably drift. The right behavior for a non-ready bead is to refuse and tell the user how to fix it. Cheap to bypass deliberately (`bd set-state <id> triage=ready`); hard to bypass accidentally.

**Why Step 0 instead of folding into Step 1:**

Step 0 is conceptually a gate, not metadata extraction. Conflating them means the gate condition might evolve (new triage states, new exception paths) and changes ripple through Step 1's metadata-extraction code path. A separate step keeps the gate isolated and grep-able.

**Why "do not attempt to enrich":**

Maintenance is meant to be lightweight (~5 turn budget). Bead enrichment is a planning task that can require reading the codebase, discussing scope with the user, and writing 100+ lines of structured template content. That's survey or scribe work, not maintenance. Refuse cleanly; let the user route the enrichment elsewhere.

**Why "next_action" is a concrete command:**

The Readiness-Gap output is read by either the user or `/leroy`. A concrete `bd` command or doc reference ("edit bead per X template, then bd set-state ...") is actionable. Generic guidance ("enrich the bead first") is not. Specificity here saves a round-trip.

**Why the patch leaves Steps 1-4 alone:**

The actual planning logic is correct as written — only the entry condition was wrong. Surgical change preserves existing behavior for the (overwhelming majority of) cases where the gate passes.

## Backwards compatibility

- Repos using the triage label system: behavior changes (correctly) so non-ready beads return a Readiness-Gap instead of a fix plan. This is the desired change.
- Repos NOT using triage labels (legacy): no change. Step 0 falls through to Step 1 because "no triage label at all" is treated as legacy-ready.
- `/leroy` and downstream consumers: the Output Contract gains a conditional alternate; consumers that only handle the standard 4 sections will need to handle the Readiness-Gap output too. The output is self-identifying via the `## Readiness Gap` header — easy to branch on.

## Alternatives considered

1. **Soft warning in metadata output** ("triage_state: triaged, but here's a plan anyway"). Rejected — soft warnings drift; the whole point of this fix is to harden the gate.
2. **Auto-enrich and produce a plan** (read the codebase, fill in Changes Needed, then plan). Rejected — that's a much wider scope expansion; maintenance becomes survey-lite. Keep responsibilities sharp.
3. **Delegate the gate to `/leroy` step 3e** (where it already exists at claim time). Rejected — maintenance can be invoked outside `/leroy` (other slash commands, manual Agent calls). Defense in depth means the gate lives at every entry point.

## Test plan

1. Bead at `triage:ready` → standard 4-section fix plan output (no change from current behavior).
2. Bead at `triage:triaged`, all dependencies clear, full Stub Template only → Readiness-Gap output, no source files read, no fix plan emitted.
3. Bead at `triage:backlog` → Readiness-Gap output.
4. Bead with no triage label at all (legacy) → standard 4-section fix plan (treated as implicit ready).
5. Bead with malformed `bd show` (no LABELS line) → Readiness-Gap with reason "missing-labels".
6. Confirm `next_action` field contains an exact bd command or template reference, not generic guidance.
