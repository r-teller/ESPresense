# Patch: navigator-survey — add triage gate (hoist gaps; plan ready subset)

**Target repo:** vibe-templates
**Target file:** `agents/navigator/navigator-survey.md`
**Type:** Hardening (codify existing-good behavior into contract)
**Severity:** Low — survey caught the issue this session through diligence; this patch makes the catch contractual rather than incidental
**Companion:** `navigator-recon-triage-filter.md` (canonical fix), `navigator-maintenance-triage-gate.md` (sibling defense-in-depth)

---

## Summary

`navigator-survey` accepts a list of bead IDs + `bd show` output for each, and produces a sequenced multi-bead plan. The current Procedure has no explicit step requiring a triage state check on input beads — survey *can* notice and call out non-ready beads (and did so this session for `ESPresense-nf5.11`), but only because of model diligence, not because the spec demands it.

A future invocation under load, with a less rigorous reading, could silently include `triage:backlog` or `triage:triaged` beads in the sequenced output and let them propagate to the orchestrator's claim step.

## Real-world precedent

This session, survey was given 5 beads including `ESPresense-nf5.11` at `triage:triaged` (Stub Template only). Survey:
- Correctly identified the readiness gap
- Recommended deferral with enrich-then-implement as alternative
- Surfaced the cross-item dependency (UI form keys must match firmware module keys not yet defined)

That's the right outcome. But: nothing in the current spec *required* survey to do that. The diligence was an emergent property of the prompt's quality, not a contract. We should make it a contract.

Unlike maintenance (which refuses non-ready beads outright), survey legitimately needs to *plan around* mixed input — a user might submit 5 beads where 4 are ready and 1 isn't, and the right output is "here's the plan for the 4 ready ones, here's what the 5th needs." So this patch hoists gaps to a top-level output section instead of refusing the whole batch.

## Root cause

Steps 1-5 go from metadata parsing through cross-item dependency mapping to sequenced plan. Triage state is not extracted in Step 1 and not used as a sequencing constraint in Step 4. The Output Contract has no field for surfacing readiness gaps; survey writers can mention them in `Cross-Item Notes` (a free-form section), but they aren't required to.

## Fix (unified diff)

```diff
--- a/agents/navigator/navigator-survey.md
+++ b/agents/navigator/navigator-survey.md
@@ -4,9 +4,10 @@ description: Builds detailed, sequenced implementation plans for complex or multi-item work. Reads source code, maps dependencies, and produces per-item scope with handoff criteria.
 tools: Read, Grep, Glob, LS, Bash
 model: opus
 tier: scale
-version: 1.1.0
+version: 1.2.0
 created: 2026-03-21
 changelog:
+  - 1.2.0 (2026-05-01): Add Triage Gate (Step 0) — surface non-ready input beads in a top-level "Readiness Gaps" output section (Section 0); proceed with sequencing for ready beads only. Mirrors navigator-recon v1.2.0 and navigator-maintenance v1.2.0; codifies existing-good diligence as contract.
   - 1.1.0 (2026-04-25): Add tracker-conditional blocks
   - 1.0.0 (2026-03-21): Initial version
 ---
@@ -39,6 +40,25 @@ The dispatcher passes via prompt:

 ## Procedure

+### Step 0 — Triage Gate
+
+For each input bead, parse the LABELS line from its `bd show` output and check the `triage:*` label:
+
+- `triage:ready` → include in sequencing (Steps 1-5).
+- No triage label at all (legacy, pre-triage system) → include in sequencing (Steps 1-5).
+- `triage:backlog` → EXCLUDE from sequencing. Add to Readiness Gaps list.
+- `triage:triaged` → EXCLUDE from sequencing. Add to Readiness Gaps list.
+- Malformed input (no LABELS line) → EXCLUDE. Add to Readiness Gaps with reason "missing-labels".
+
+For each excluded bead, record:
+- `id`, `triage_state`, `missing_sections` (which template sections the bead lacks per `work-item-templates.md`), and a `recommendation` (one of: `enrich-then-implement`, `classify-first`, `defer-to-follow-up`).
+
+If ALL input beads fail the gate: produce only Section 0 (Readiness Gaps) and skip Sections 1-3. Section 4 (Context Files) lists `work-item-templates.md` only.
+
+Otherwise: produce Section 0 with the gap list, then proceed with Steps 1-5 for the ready subset. The Item Sequence and Per-Item Plans cover only the ready beads.
+
+Why: survey can receive any combination of input beads. Silently sequencing a non-ready bead lets the readiness gap propagate downstream — past `/leroy`'s claim gate, into the implementing agent's session, where the cost of discovery is much higher. Hoisting gaps to a top-level section makes them the first thing the user (or orchestrator) reads.
+
 ### Step 1 — Parse Work Item Metadata

 For each work item from the prompt input, extract:
@@ -77,6 +97,12 @@ Produce a sequenced implementation plan with:
 Return ALL sections below in this exact order. Use raw structured format (no markdown tables).

 ```
+## 0. Readiness Gaps
+- [item-id] | [triage state] | missing: [comma-separated template sections] | rec: [enrich-then-implement / classify-first / defer-to-follow-up]
+- [item-id] | [triage state] | missing: [...] | rec: [...]
+(or "none" if all input beads passed the Triage Gate)
+
 ## 1. Item Sequence
 - order: [item-id-1], then [item-id-2], then [item-id-3]
 - rationale: [why this order — dependencies, shared files, risk]
@@ -103,6 +129,6 @@ Return ALL sections below in this exact order. Use raw structured format (no mar
 - migration_order: [migration dependencies, if any]
 - handoff_criteria: [what to verify before moving from item N to item N+1]

 ## 4. Context Files
 - [filename.md] — [reason, which items need it]
 ```

 **Every section is required. If a section has no data, output the section header with "none".**
```

## Justification

**Why hoist instead of refuse (different from maintenance):**

Maintenance is single-bead — one bead in, one plan out. Refusing is binary and clean. Survey is multi-bead — 5 beads in, sequenced plan out. Refusing the whole batch when 1 of 5 isn't ready throws away useful work. The right behavior is "plan the 4, surface the 1." This is also the actual behavior survey demonstrated this session; the patch just promotes it from an emergent property to a required output.

**Why Section 0 (top of output), not Cross-Item Notes:**

`Cross-Item Notes` is free-form and easy for a downstream consumer to skim past. Readiness gaps are blocking issues the user must resolve before the affected beads can be claimed — they belong at the top of the output where a glance reveals them. Numbering as `0` (rather than renumbering everything) preserves the existing 1-4 section structure for orchestrators that already parse it.

**Why `recommendation` is one of three fixed values:**

Free-form recommendations drift toward generic prose ("consider enriching the bead first"). The three values cover the actual decisions:
- `enrich-then-implement`: bead has the right type/classification, just needs description detail. User's call whether to do it this session or defer.
- `classify-first`: bead is at `task` or unclassified — needs the type-classification pass before enrichment is even possible.
- `defer-to-follow-up`: bead has cross-item dependency on work not yet done (the nf5.11 case — UI form keys depend on firmware modules not yet implemented). Even if you enriched it now, the spec would shift after the dependency lands.

These three cover the cases I've actually seen survey reach. If a fourth case emerges, expand the enum then.

**Why the patch is light on enforcement language compared to the maintenance patch:**

Survey is permitted to plan around gaps, so it doesn't need "STOP" / "REFUSE" / "MUST NOT" framing. The contract is: gaps land in Section 0, ready work lands in Sections 1-3. Soft framing works because the Output Contract structurally separates the two — there's no path for a gap-bead to leak into Section 1 if Step 0 was followed.

**Why this patch is "low severity" while maintenance is "medium":**

Survey already exhibited the correct behavior this session. The patch is hardening, not bug-fixing. Maintenance, by contrast, has no diligence safety net — its whole value prop is "fast and lightweight," which means it skips the kind of thorough reading that would catch a triage gap. Maintenance is a real bug; survey is contract drift waiting to happen.

## Backwards compatibility

- Repos using the triage label system: behavior changes (correctly) so non-ready beads land in Section 0 instead of Section 2. Existing consumers reading Sections 1-4 are unaffected for ready-bead output.
- Repos NOT using triage labels (legacy): no change. Step 0 routes all beads to Steps 1-5.
- Output Contract: Section 0 is new but follows the existing "every section required, output 'none' if empty" pattern. Consumers that strictly parse `## 1.` through `## 4.` will need to also accept `## 0.` — same migration as the recon patch's tail-block.

## Alternatives considered

1. **Squeeze gaps into Cross-Item Notes (existing free-form section).** Rejected — too easy to skim; doesn't promote gap-handling to required behavior.
2. **Add a `readiness_state` field to each Per-Item Plan section.** Rejected — gives each non-ready bead a full plan section it shouldn't have, and forces the consumer to filter at read time. Hoisting is cleaner.
3. **Refuse the whole batch on any gap (mirror maintenance).** Rejected — destroys legitimate value when 4 of 5 beads are ready. The whole point of survey is multi-bead planning under partial readiness.

## Test plan

1. All 5 input beads at `triage:ready` → Section 0 = "none", Sections 1-4 unchanged from current behavior.
2. 4 input beads at `triage:ready`, 1 at `triage:triaged` (the nf5.11 shape) → Section 0 lists the triaged bead with missing sections + recommendation; Sections 1-3 cover only the 4 ready beads.
3. All 5 input beads at `triage:backlog` → Section 0 lists all 5; Sections 1-3 = "none"; Section 4 lists `work-item-templates.md` only.
4. Mix of ready + legacy unlabeled beads → all included in sequencing; Section 0 = "none".
5. Confirm Section 0's `recommendation` field uses one of the three enum values (no free-form text).
6. Re-run the nf5.11 scenario: confirm survey output matches the in-session output (same gap surfaced) but now via spec-required Section 0 rather than embedded prose in Item 5's plan.
