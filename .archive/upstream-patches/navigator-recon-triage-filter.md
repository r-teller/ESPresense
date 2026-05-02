# Patch: navigator-recon — filter "Ready to Start" by triage state

**Target repo:** vibe-templates
**Target file:** `agents/navigator/navigator-recon.md`
**Type:** Bug fix
**Severity:** Medium — surfaces non-selectable items as selectable; user can act on items that fail the workflow Start Checklist
**Discovered:** 2026-05-01 during `/leroy` startup on ESPresense feature/athom-plug-v3

---

## Summary

`navigator-recon` lists beads in Section 5 ("Ready to Start") sourced from `bd ready`, which checks **dependency** readiness only. Beads in `triage:backlog` or `triage:triaged` (which fail the workflow Start Checklist) appear in the same selectable list as `triage:ready` beads, with only an inline parenthetical noting their state.

`/leroy`'s downstream procedure treats Section 5 as the canonical pick list. A user (or model) selecting from that list ends up trying to claim a bead that should not be claimed — bypassing the triage gate that workflow-execution.md requires.

## Real-world failure

ESPresense session 2026-05-01:

- Recon listed `ESPresense-nf5.11` in "Ready to Start" with `triage:triaged, effort forecast missing from description` as inline annotation.
- Item is at `triage:triaged` — Stub Template only, no Changes Needed / AC / Effort Forecast — which fails 4 boxes on the Readiness Checklist (`work-item-templates.md`).
- `/leroy` step 3e requires `triage:ready` (or legacy unlabeled) before claim. Recon's output forced the main context to do the filtering, which it did not.
- The downstream survey caught the issue (recommended deferral with enrich-then-implement as alternative), so no work was lost — but only because survey re-checked. Maintenance dispatch would not have re-checked.

## Root cause

Two issues in `navigator-recon.md`:

1. **Step 3** runs `bd ready` and treats its output as the "ready set" for Section 5, but `bd ready` filters on dependency state, not triage state. It already has `bd list -l triage:backlog -s open` and `bd list -l triage:triaged -s open` queries, but the data has nowhere to land in the output checklist — it is collected but discarded.
2. **Section 5** ("Ready to Start") in the output spec does not specify the filter. Section 7 ("Recommended Work") inherits the same ambiguity for `next_up`.

## Fix (unified diff)

```diff
--- a/agents/navigator/navigator-recon.md
+++ b/agents/navigator/navigator-recon.md
@@ -4,9 +4,10 @@ description: Session orientation agent. Reads handoff, changelog, and work item state. Returns raw structured data for /leroy to format.
 tools: Read, Grep, Glob, LS, Bash
 model: sonnet
 tier: scale
-version: 1.1.0
+version: 1.2.0
 created: 2026-03-21
 changelog:
+  - 1.2.0 (2026-05-01): Filter "Ready to Start" by triage:ready (or legacy unlabeled) only; surface backlog/triaged items as informational "Needs Triage" tail-block under Section 5; require Recommended Work picks only from the filtered ready set.
   - 1.1.0 (2026-03-21): Add tracker-conditional blocks
   - 1.0.0 (2026-03-21): Initial version
 ---
@@ -60,12 +61,21 @@ If null or missing: `- no previous handoff`
 ### Step 3 — Work Item State

 ```bash
 bd epic status
 bd list -s in_progress
-bd ready
+bd ready                            # Dependency-ready (does NOT check triage state)
 bd list -l triage:backlog -s open   # Needs analysis
 bd list -l triage:triaged -s open   # Needs final review
 ```

-Note: Beads without a triage label are legacy (pre-triage system). Treat them as implicitly `triage:ready` until they are retroactively labeled.
+**Build the selectable-ready set:** intersection of `bd ready` AND (`triage:ready` label OR no triage label at all). Concretely, for each bead in `bd ready`:
+- If it has label `triage:ready` → include in selectable-ready set.
+- If it has NO triage label (legacy, pre-triage system) → include in selectable-ready set.
+- If it has label `triage:backlog` or `triage:triaged` → EXCLUDE from selectable-ready set; it goes in the Needs Triage tail-block of Section 5 instead.
+
+`bd ready` returns items whose dependencies are clear regardless of triage state, so this filter is mandatory. A bead at `triage:triaged` with no blockers will appear in `bd ready` output but MUST NOT be presented as selectable — it fails the Readiness Checklist in `work-item-templates.md` and the Start Checklist in `workflow-execution.md`.
+
+Note: Beads without ANY triage label are legacy (pre-triage system). Treat them as implicitly `triage:ready` until retroactively labeled.

 ### Step 4 — Effort Forecasts

-For each bead from `bd ready`, run `bd show <id>` and look for:
+For each bead in the selectable-ready set (Step 3), run `bd show <id>` and look for:

 ```
 Effort Forecast:
@@ -120,16 +130,22 @@ Produce ALL 7 sections below in this exact order. Use raw bullet points and pipe
 - [item-id] | [type] | [description]
 (or "none")

 ## 5. Ready to Start
 - [priority] | [item-id] | [type] | ~turns:[N or ??] | [description]
-(top 10 sorted by priority)
+(top 10 sorted by priority — ONLY beads in the selectable-ready set per Step 3; NEVER include triage:backlog or triage:triaged beads here)
+
+Needs Triage (informational — NOT selectable):
+- [item-id] | [triage state] | [description]
+(beads from bd ready that are at triage:backlog or triage:triaged; max 5; this list exists so the user knows non-selectable work is queued, but Section 7 must not recommend them)

 ## 6. Load Into Main Context
 - [filename.md] — [reason from work item description]
 - [filename.md] — [reason from work item description]
 (only files relevant to top 3 recommended items)

 ## 7. Recommended Work
-- continue: [item-id + title, or "none"]
+- continue: [in-progress item-id + title, or "none"]
 - close_out: [epic name + remaining item IDs]
-- next_up: [item-id + title]
+- next_up: [item-id + title — MUST come from the selectable-ready set, NEVER from Needs Triage]
 ```
```

## Justification

**Why filter at the agent boundary, not in `/leroy`:**

`/leroy` runs in the main context window. Pushing the triage filter into `/leroy` means main context has to either (a) re-fetch `bd state <id> triage` for every listed bead, or (b) trust agent-supplied annotations and apply rules in main context. Both leak token budget for a check the agent already has the data for. The agent collects `triage:backlog` and `triage:triaged` lists today — fix the output spec to use them.

**Why preserve the 7-section contract:**

`/leroy` validates that all 7 section headers (`## 1.` through `## 7.`) appear in the recon output. Adding a Section 8 would force a parallel `/leroy` patch. Adding a tail-block under Section 5 keeps the validator happy with no `/leroy` change required. The 7-section contract is honored verbatim.

**Why "Needs Triage" is informational, not hidden:**

The user benefits from knowing non-selectable work is queued — it tells them the backlog has unprocessed items waiting on enrichment. Hiding the list would create a different bug (silent neglect of triaged work). Surfacing it as informational with explicit "NOT selectable" framing keeps it visible without giving Section 7 license to pick from it.

**Why explicit "NEVER" / "MUST" language in Section 5 and 7:**

Agent prompts that hint without enforcing tend to drift under load. The bug being fixed here originates from Section 5's instruction to list "top 10 sorted by priority" without specifying the source set — a polite convention rather than a hard filter. The replacement language is imperative because soft guidance demonstrably failed in production.

**Why minor: legacy unlabeled beads stay implicit-ready:**

The original line ("Beads without a triage label are legacy ... Treat them as implicitly `triage:ready`") is preserved. Pre-existing repos using the agent without the triage label system continue to work without retroactive labeling. The fix is additive for legacy users — they see no behavior change.

## Backwards compatibility

- Repos using the triage label system: behavior changes (correctly) so backlog/triaged items move out of the selectable list into the new tail-block.
- Repos NOT using triage labels (legacy): no change. `bd ready` output flows directly into Section 5 as before, since none of those beads carry a `triage:*` label.
- `/leroy` and downstream `/gogogo` validators: no change required — all 7 section headers are preserved exactly.

## Alternatives considered

1. **Add a Section 8 "Needs Triage."** Rejected — forces a paired patch to `/leroy`'s validator (which checks for "ALL 7 sections"). Higher blast radius for the same fix.
2. **Drop backlog/triaged items entirely (don't surface them).** Rejected — creates silent neglect; user loses visibility into items needing attention.
3. **Filter in `/leroy` after recon returns.** Rejected — duplicates work the agent already does; main context pays token cost for a re-check.

## Test plan

After applying:

1. Project with mixed triage states: confirm `triage:backlog` and `triage:triaged` items appear ONLY in the Needs Triage tail-block, never in the main Section 5 list.
2. Project with no triage labels (legacy): confirm `bd ready` output flows into Section 5 exactly as before; Needs Triage tail-block is empty.
3. `/leroy` end-to-end: confirm validator still recognizes all 7 sections and `next_up` recommendations come exclusively from the filtered set.
4. Edge case: bead at `triage:ready` with all dependencies clear — appears in Section 5 unchanged.
5. Edge case: bead at `triage:triaged` with all dependencies clear — appears ONLY in Needs Triage, NOT in Section 5; Section 7 never recommends it as `next_up`.
