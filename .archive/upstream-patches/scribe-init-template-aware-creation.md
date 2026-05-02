# Patch: scribe-init template-aware creation + new scribe-refine enrichment agent

**Target repo:** vibe-templates
**Target files:**
- `agents/scribe/scribe-init.md` (modify)
- `agents/scribe/scribe-refine.md` (new)
- `skills/scribe/SKILL.md` (modify — register the new agent)

**Type:** Bug fix + new capability
**Severity:** Medium-high — beads created via scribe-init never reach `triage:ready`, forcing every downstream agent (recon, survey, maintenance) to either (a) hit the triage gate and refuse, or (b) bypass it by reading external PRDs. The downstream triage-gate patches (`navigator-recon-triage-filter.md`, `navigator-maintenance-triage-gate.md`, `navigator-survey-triage-gate.md`) treat the symptom; this patch treats the cause.
**Discovered:** 2026-05-01 during ESPresense feature/athom-plug-v3 implementation; beads `ESPresense-nf5.6`, `nf5.7`, `nf5.8`, `nf5.11` shipped with Stub Template only (Summary + Persona + Phase + "see PRD § X") and required out-of-band PRD reading at claim time.

---

## Summary

`scribe-init` § 3.2 ("Offer to create initial work items") ends with a single instruction:

> If the user approves, generate `bd create "..." --type feature|task` commands.

That is the entire bead-creation contract. It produces beads with **title + type only** — no description, no template, no labels (`size:*`, `cynefin:*`, `layer:*`, `persona:*`), no triage state. The Readiness Checklist (`work-item-templates.md`) requires every one of those before a bead can reach `triage:ready`, but `scribe-init` doesn't know about any of them.

The downstream effect is that the ENTIRE backlog created at project init is below `triage:ready`. Every claim becomes a triage-gate decision: enrich now, defer, or bypass. In practice (observed both on ESPresense and likely elsewhere), the bypass path wins — agents read external PRDs to compensate for thin bead bodies. The bead system becomes a "list of titles," not a contract.

There's also no documented enrichment workflow. `scribe-plan` is session-level; `scribe-prd` writes architecture docs; `scribe-brief` writes executive memos. None walk the backlog and promote `triage:triaged` → `triage:ready`. So once the gap opens at init, nothing closes it.

## Real-world failure mode

Observed during ESPresense Phase 1 implementation, 2026-05-01:

- 14 beads created from `PRD/phase-1-implementation.md` via scribe-init pattern (or equivalent manual pass).
- All shipped at `triage:backlog` initially, hand-promoted to `triage:triaged` via Stub Template (Summary, Persona, Phase, Open Questions = "see PRD § X", Rough Size).
- 4 of 14 (`nf5.6`, `nf5.7`, `nf5.8`, `nf5.11`) never advanced past `triage:triaged` because no agent was responsible for the enrichment.
- Implementation session for `nf5.6`+`nf5.7` proceeded by reading `PRD/phase-1-implementation.md § Step 3, 7, 11` directly. Code was correct (PRD has verbatim diffs), but the bead descriptions are now permanent records that don't match what was built.
- Recovery cost: per-bead enrichment after the fact (~5-10 min each) OR accept the documentation drift and let beads serve as title-only pointers.

Both options are fine in isolation. The problem is they're decided ad-hoc per bead, by whichever agent picks it up — not by contract at init time.

## Root cause

Two gaps, sequential:

1. **scribe-init has no template awareness.** It treats `bd create` as a name-and-type call, not a structured-content emission. No reference to `work-item-templates.md`, no label application, no `triage:*` state assignment.
2. **No enrichment agent exists.** Even if scribe-init creates `triage:backlog` stubs (the correct initial state per `work-item-templates.md`), there's no documented agent for the `backlog → triaged → ready` progression. The progression is documented but unowned.

## Fix — Part 1: scribe-init diff

```diff
--- a/agents/scribe/scribe-init.md
+++ b/agents/scribe/scribe-init.md
@@ -1,8 +1,9 @@
 ---
 name: scribe-init
 description: "Use proactively to set up a repo for autonomous/assisted development: review any provided source doc (e.g., foo.md or prd.md), detect missing template placeholders across project markdown files, initialize the project's issue tracking, and produce completed, ready-to-use docs (updating files when appropriate)."
 tools: Read, Write, Edit, MultiEdit, Grep, Glob, LS, Bash
 model: sonnet
+version: 2.0.0
 permissionMode: default
 color: green
 ---
@@ -53,28 +54,67 @@ Build a concise "missing info" list per file.

 ## 3) Tracker initialization + initial work item creation

 ### 3.1 Initialize the tracker if needed

 - First, check whether beads is already initialized (look for common markers like `.beads/`, `beads/`, or whatever `bd status` indicates).
 - If not initialized, run: `bd init`
 - Report what you did and any commands run.

-### 3.2 Offer to create initial work items
-Ask the user a single yes/no:
-- "Do you want me to create initial work items from your PRD/features?"
+### 3.2 Offer to create initial work items
+
+Ask the user a single yes/no:
+- "Do you want me to create initial work items from your PRD/features?"
+
+If yes:
+- Read `rules/work-item-templates.md` (or your project's equivalent) to understand the template families: Stub, Small/Medium/Large Feature, Bug, Chore, Epic, Decision.
+- Extract candidate work items from `architecture.md` Product Guidance (or the provided doc).
+- For each candidate, choose the appropriate template:
+  - If the PRD section is fully spec'd (file paths, AC, effort estimates present): use Small/Medium/Large Feature based on layer count.
+  - If the PRD section is light or sequencing-only: use the Stub Template.
+  - If the work is investigative: use the Decision (Spike) template.
+  - If the work is maintenance-only: use the Chore template.
+- Group into phases and propose 5–20 items.

-If yes:
-- Extract candidate work items from `architecture.md` Product Guidance (or the provided doc).
-- Propose 5–20 items grouped by theme.
-
-- If the user approves, generate `bd create "..." --type feature|task` commands.
+If the user approves, for EACH item generate a structured `bd create` invocation that includes:
+
+1. **Title** — short, action-oriented.
+2. **Type** — `feature`, `bug`, `chore`, `decision`, or `epic`. Never `task` (reserved for quick-capture only per `work-item-templates.md`).
+3. **Description body** via `--body-file` (preferred for >1 paragraph) or `--description` — fill the chosen template's required sections, NOT a one-liner. For Stub Template items: Summary + Persona + Phase + Open Questions + Rough Size. For Small/Medium/Large items: full template per `work-item-templates.md`.
+4. **Labels** via `--add-label`, ALL of:
+   - `size:small` / `size:medium` / `size:large` (per Size Classification Guide)
+   - `cynefin:clear` / `cynefin:complicated` / `cynefin:complex` / `cynefin:disorder` (per Cynefin Domain Classification)
+   - `persona:end-user` / `persona:developer` / `persona:administrator` / `persona:system` / `persona:api-consumer` (per Persona Definitions)
+   - `layer:frontend` / `layer:backend` / `layer:data` / `layer:infra` / `layer:workflow` (one or more, matching the files the item touches)
+5. **Initial triage state** — `--add-label triage:backlog` for stub-only items; `--add-label triage:triaged` if the description was filled with Small/Medium/Large template content but is missing AC or Effort Forecast; `--add-label triage:ready` ONLY if the item passes the Readiness Checklist self-sufficiency test (a fresh session can implement from `bd show` alone).
+6. **Parent epic** via `--parent <epic-id>` — every non-epic item must have a parent epic per the Epic Template's "Decomposition" requirement.
+7. **Dependencies** via `bd dep add` follow-up calls — every prose mention of "blocks on / depends on / requires" in the description MUST resolve to a formal dependency entry, per the Readiness Checklist "Dependencies formalized" rule.
+
+**Default initial triage state is `triage:backlog`, not `triage:ready`.** Honesty over optimism: a freshly created stub is rarely self-sufficient. Promotion to `ready` happens via `scribe-refine` (see § 3.3) once the spec is fully inlined.
+
+(If beads CLI differs in this repo, adapt, but keep the intent: structured body, complete label set, explicit triage state, no naked-title creates.)
+
+### 3.3 Hand off to scribe-refine for promotion

-(If beads CLI differs in this repo, adapt, but keep the intent.)
+After bead creation, the backlog is at `triage:backlog` (or `triage:triaged` for items that landed with full template but incomplete sections). To advance to `triage:ready`, invoke the `scribe-refine` agent with the list of bead IDs and the source PRD/spec doc. scribe-refine walks each bead, inlines the relevant PRD section into the bead description, and promotes triage state when the Readiness Checklist passes.
+
+Do NOT promote to `triage:ready` from inside scribe-init. Two reasons:
+1. Promotion requires reading the bead's full template content and validating against the Readiness Checklist — that's scribe-refine's contract, not scribe-init's.
+2. Keeping the create/refine roles separate allows phase-staged enrichment (create whole backlog at init, refine only the items you're about to claim).
+
+Recommend the next command to the user:
+
+    "Backlog created at `triage:backlog`. To promote items to `triage:ready` for execution, run `/scribe refine <bead-ids> --source <prd-path>`. Do this just-in-time as you approach each item, not all at once."
```

## Fix — Part 2: new file `agents/scribe/scribe-refine.md`

```markdown
---
name: scribe-refine
description: "Walk one or more triage:backlog or triage:triaged beads and promote them to triage:ready by inlining the source PRD/spec section, applying the appropriate work-item-templates.md template, and validating against the Readiness Checklist. Use just-in-time before claim, not en masse."
tools: Read, Write, Edit, MultiEdit, Grep, Glob, LS, Bash
model: sonnet
version: 1.0.0
permissionMode: default
color: green
---

# Scribe Refine — Bead Enrichment Specialist

Purpose: Take beads at `triage:backlog` or `triage:triaged` and promote them to `triage:ready` by inlining their full spec into the bead body — eliminating the "spec lives in PRD, bead is a pointer" anti-pattern.

> This agent does NOT claim beads, write source code, or modify git state. It only enriches bead descriptions and updates triage labels.

---

## Non-Goals

- Does NOT claim beads (`bd update -s in_progress`)
- Does NOT write or modify source code
- Does NOT modify git state
- Does NOT create new beads (use scribe-init for that)
- Does NOT enrich beyond what the source PRD/spec contains — if the PRD itself has gaps, surface them rather than invent answers

---

## Input Contract

The caller passes via prompt:
- One or more bead IDs to refine
- The source PRD/spec doc path(s) — the canonical content to inline
- (Optional) the appropriate template family per bead, if not derivable from existing labels

---

## Procedure

### Step 1 — Read each bead's current state

For each input bead:
- Run `bd show <id>` to capture: title, type, current labels (size, cynefin, layer, persona, triage), existing description, parent, dependencies.
- Note which sections of the appropriate template (per `work-item-templates.md`) are present vs missing.

### Step 2 — Locate the PRD section for each bead

Use the bead's title and any inline references ("see PRD § X", "per Step Y") to find the corresponding section in the source doc. If the bead description has no pointer and the title is ambiguous, surface a question and stop — do not guess.

### Step 3 — Inline the PRD content into the bead body

Build the full template body per the bead's type and size:
- **Small Feature**: Summary, Persona, Changes Needed (with file paths), Acceptance Criteria, Effort Forecast.
- **Medium Feature**: above + API Contract (if backend), Frontend Component (if UI), Scope Boundaries, Patterns to Reuse, Testing Strategy.
- **Large Feature**: above + Data Model, File Manifest, Verification & UAT, Deferred Work.
- **Bug**: Summary, Persona, Steps to Reproduce, Root Cause Hypothesis, Files to Investigate, Fix Approach, Acceptance Criteria, Effort Forecast.
- **Chore**: Summary, Persona, Changes Needed, Scope Boundaries, Effort Forecast.
- **Epic**: Summary, Persona, Success Criteria, Decomposition, Scope Boundaries, Dependencies.
- **Decision (Spike)**: Summary, Persona, Questions to Answer, Time Box, Output Artifacts, Scope Boundaries, Effort Forecast.

Copy verbatim from the PRD where the PRD has the content (file diffs, AC checklists, command examples). Paraphrase only when the PRD is structured differently (e.g., merging two PRD subsections into one Changes Needed table).

### Step 4 — Apply missing labels

For each bead, ensure ALL of these labels are present (`bd update <id> --add-label X`):
- `size:small` / `size:medium` / `size:large`
- `cynefin:clear` / `cynefin:complicated` / `cynefin:complex` / `cynefin:disorder`
- `persona:*` (one or more)
- `layer:*` (one or more, matching files in Changes Needed)

### Step 5 — Formalize dependencies

For each prose mention of "blocks on", "depends on", "requires", "after", "prerequisite" in the enriched description, ensure a corresponding `bd dep add` call exists. Run `bd dep tree <id>` to verify.

### Step 6 — Run the Readiness Checklist

For each bead, verify ALL boxes per `work-item-templates.md` Readiness Checklist:
- [ ] Type classified (not `task`)
- [ ] Template filled
- [ ] Size labeled
- [ ] Cynefin classified
- [ ] Layers identified
- [ ] Persona identified
- [ ] No TBDs
- [ ] Section contracts met
- [ ] Hazard check done
- [ ] Acceptance criteria specific and testable
- [ ] Regression test (for bugs)
- [ ] Scope boundaries (for medium+ and chores)
- [ ] A11y considered (for UI work)
- [ ] Effort forecast per-phase
- [ ] Dependencies formalized
- [ ] Upstream dependencies resolved
- [ ] Lint passes (`bd lint`)
- [ ] Priority set
- [ ] Epic assigned

### Step 7 — Promote triage state

Only when the Readiness Checklist passes:
- `bd update <id> --remove-label triage:backlog --remove-label triage:triaged --add-label triage:ready`
- (Or `bd set-state <id> triage=ready` if your project uses the state-event command.)

If any checklist box fails: leave the bead at `triage:triaged`, surface the gap, and stop. Do not promote a bead with known gaps.

---

## Output Contract

Return ALL sections below in this exact order. Use raw structured format.

\`\`\`
## 1. Beads Processed
- [bead-id] | [previous triage state] → [new triage state] | [outcome: promoted / blocked / skipped]

## 2. Per-Bead Summary

### [bead-id]: [title]
- previous_state: [triage:backlog | triage:triaged | other]
- new_state: [triage:ready | triage:triaged]
- enrichment: [what sections were added/expanded]
- labels_added: [comma-separated]
- dependencies_formalized: [bd dep add commands run, or "none"]
- readiness_checklist: [pass / fail with specific failing items]
- prd_gaps_surfaced: [if PRD itself was missing content; otherwise "none"]

## 3. Open Questions
- [question 1]
- [question 2]
(Or "none" if all beads were enriched cleanly.)

## 4. Next Steps
- [recommended next action — typically "claim and execute" if all promoted, or "address PRD gaps in <doc>" if blocked]
\`\`\`

**Every section is required. If a section has no data, output the section header with "none".**
```

## Fix — Part 3: SKILL.md registration

```diff
--- a/skills/scribe/SKILL.md
+++ b/skills/scribe/SKILL.md
@@ -22,6 +22,12 @@ Use this skill when:
 ### 1. `scribe-init`
 Use for: project setup, onboarding, bootstrapping, template population.
 Keywords: setup, init, initialize, bootstrap, scaffold, onboarding, templates, fill placeholders, configure docs, beads setup.

+### 2. `scribe-refine`
+Use for: promoting backlog beads to triage:ready by inlining PRD/spec content into bead bodies. Just-in-time enrichment before claim.
+Keywords: refine, enrich, promote, triage, ready, inline spec, bead body, work-item-templates.
+
+(scribe-init creates beads at triage:backlog. scribe-refine promotes them to triage:ready. The two are paired.)
+
 ---

 ## Routing
@@ -52,6 +58,7 @@ Examples by intent:
 - repo docs/templates + issues setup → `scribe-init`
+- promote backlog beads to triage:ready → `scribe-refine`
 - draft a PRD section in architecture.md → `scribe-prd`
 - exec brief / decision memo → `scribe-brief`
 - planning session with goals + sequencing → `scribe-plan`
```

## Justification

**Why fix at the agent boundary, not in the workflow docs:**

`work-item-templates.md` already specifies the templates, the Readiness Checklist, and the triage progression. The gap is operational: no agent owns the create-with-template step, and no agent owns the promote-with-enrichment step. Documenting "the user should do this" is what we have today — and the result is the bug described above.

**Why split into scribe-init (create) and scribe-refine (promote):**

Three reasons:
1. **Scope**: scribe-init runs once at project setup; scribe-refine runs many times throughout the project lifecycle. Different agents, different invocation cadence.
2. **Token cost**: scribe-init at full project init reads all project docs and writes 5-20 bead bodies — that's a heavy session. Scribe-refine processes 1-5 beads at a time. Splitting keeps each session bounded.
3. **Just-in-time enrichment**: enriching 20 beads at init means writing detail for items you may not implement for weeks (and which may shift before you do). Enriching just-before-claim means the spec is fresh against current code state.

**Why default to `triage:backlog`, not `triage:ready` at create:**

Honesty. A bead written at project init from a PRD section is rarely self-sufficient until cross-references and dependencies are resolved. Defaulting to `backlog` (or `triaged` when the body is structured-but-incomplete) sets accurate expectations. Calling something `ready` when it isn't creates exactly the bug we're fixing.

**Why scribe-refine refuses to promote with checklist gaps:**

Promoting a bead to `triage:ready` is a contract claim. If any checklist box fails, the bead isn't ready — even if "close enough" feels true. Soft promotion drift caused the bug we're fixing. Hard refuse + surfaced gap is the right behavior.

**Why scribe-refine surfaces PRD gaps rather than inventing content:**

If the PRD itself doesn't have the content the template requires (e.g., no Effort Forecast, no AC), the right move is to flag the PRD gap, not paper over it. scribe-refine is a copy-and-validate agent, not an authoring agent. Authoring belongs to scribe-prd.

## Backwards compatibility

- Projects already using scribe-init: behavior changes (correctly) so new beads land at `triage:backlog` with full template/labels rather than naked title-only. No existing beads modified.
- Projects with existing naked-title or stub-only beads: scribe-refine handles them — same enrichment path as new beads. Migration is opt-in per bead.
- `work-item-templates.md` and Readiness Checklist: unchanged; this patch operationalizes them rather than redefining them.
- Other slash commands and agents: unchanged. The triage-gate patches in this repo (`navigator-recon-triage-filter.md`, etc.) become belt-and-suspenders rather than load-bearing — they catch any bead that slips past scribe-init/scribe-refine, but the primary fix is upstream.

## Alternatives considered

1. **Fold scribe-refine into scribe-plan.** Rejected — scribe-plan is session-level (goals, sequencing, owners). Bead-level enrichment is a different scope; mixing them bloats both.
2. **Have navigator-survey do enrichment as a side effect.** Rejected — survey produces plans, not bead-body content. Mixing planning and authoring violates single responsibility.
3. **Auto-promote to `triage:ready` if "obvious" content matches.** Rejected — the whole point is that subjective "obvious" calls produced the current bug. Make the criteria explicit (Readiness Checklist) and gate on them.
4. **Skip scribe-refine; require manual promotion.** Rejected — manual promotion is what we have today and it's not happening. Automation here is the leverage.

## Test plan

1. Fresh repo + `scribe-init` invocation with a PRD containing 5 work items: confirm 5 beads created, each with full template body, complete label set, parent epic linked, formal dependencies registered, and `triage:backlog` (or `triage:triaged` for partial-template items).
2. `scribe-refine <bead-id> --source <prd>` on a `triage:backlog` bead: confirm description grows to full template, Readiness Checklist passes, label updated to `triage:ready`.
3. `scribe-refine` on a bead whose PRD section is missing AC: confirm refuse-to-promote, surfaces specific PRD gap, leaves bead at `triage:triaged`.
4. `scribe-refine` on a bead with prose dependencies: confirm `bd dep add` calls are emitted and `bd dep tree <id>` reflects the new edges.
5. End-to-end: scribe-init creates 5 backlog beads, scribe-refine promotes 2 of them, navigator-recon (with the recon triage-filter patch applied) lists only the 2 ready beads in Section 5, the other 3 in Needs Triage.
6. Confirm `bd lint` passes for all promoted beads (no missing required fields).
