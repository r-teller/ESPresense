# Frontend Reference (Embedded UI)

Purpose: Quick map of the SvelteKit-based on-device configuration UI as it relates to this project. Read `architecture.md` first for the high-level overview.

> The UI is a static SvelteKit export, compiled into C++ headers (`src/ui_*.h`) and served by the firmware's HTTP server. Both Svelte source AND the regenerated headers must be committed.

---

## Build pipeline

```
ui/src/routes/**.svelte
        │  npm run build
        ▼
ui/build/**          (static export — gitignored)
        │  postbuild script
        ▼
src/ui_app_immutable_assets_css.h
src/ui_app_immutable_chunks_js.h
src/ui_app_immutable_entry_js.h
src/ui_app_immutable_nodes_js.h
src/ui_html.h
src/ui_routes.h
src/ui_svg.h
        │  pio run -e athom-smart-plug-v3
        ▼
firmware.bin (UI bytes embedded)
```

**Cardinal rule** (per `AGENTS.md`): after editing anything under `ui/`, run `npm run build` in that folder. The PR must include both the Svelte source AND the regenerated `src/ui_*.h` headers — the firmware build won't pick up source changes otherwise.

---

## What changes in this project

### `ui/src/routes/hardware/+page.svelte`

Two new sections inserted between **Buttons** (~line 477) and **I²C** (line 479):

#### Relay section
| Field name | Type | Notes |
|------------|------|-------|
| `relay_pin` | number | -1 to disable; default per board from firmware |
| `relay_restore_mode` | dropdown (3 options) | `0=Always Off / 1=Always On / 2=Restore Last` |

#### Power Monitor section
| Field name | Type | Notes |
|------------|------|-------|
| `cse7766_rx_pin` | number | -1 to disable |
| `cse7766_current_limit` | float (amps) | 0 disables overcurrent trip |
| `cse7766_update_interval` | int (seconds) | range 1–600 |

> **Field `name` attributes must match firmware's `HeadlessWiFiSettings.*()` keys exactly.** The portal serializes by name; a mismatch silently drops the field. Defaults auto-populate from the firmware's JSON config endpoint.

---

## Existing patterns to follow

- **One section per hardware feature.** Keep the new sections styled identically to the existing Buttons / I²C / LEDs blocks in the same file — no new component primitives needed.
- **No conditional rendering by build flag.** UI shows all sections regardless of compile flags. This is the existing pattern; don't change it (deferred item, see `architecture.md` § Explicit Exclusions).
- **Form layout, not React-style state.** The portal is a plain HTML form posted back to firmware; Svelte is used for layout and conditional sections, not interactive state.

---

## Verification after UI edits

```bash
cd ui
npm install        # first time only
npm run build      # regenerates src/ui_*.h
cd ..
git status         # confirm src/ui_*.h files are modified — these MUST be committed
pio run -e athom-smart-plug-v3   # confirms the firmware picks up the change
```

If `git status` doesn't show updated `src/ui_*.h` files after `npm run build`, the build step didn't run — re-check from `ui/` directory.

---

## Files NOT modified in this project

- BLE-related code consumers (the existing UI sections for BLE remain untouched)
- LED, Buttons, I²C, sensor sections — only the two new sections are added

---

## Reference

- Source page: `ui/src/routes/hardware/+page.svelte`
- Generated headers (do NOT hand-edit): `src/ui_*.h`
- Build configuration: `ui/package.json`, `ui/svelte.config.js`
