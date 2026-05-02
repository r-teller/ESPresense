# Phase 2 — Build & Local Verification

**Goal**: Verify all platformio environments still build cleanly after Phase 1 changes, confirm binary size fits OTA partition, run static analysis.

**Estimated effort**: 1–2 hours (mostly waiting on builds; minimal active work)

**Prerequisites**:
- Phase 1 complete (see `PRD/phase-1-implementation.md`)
- Branch `feature/athom-plug-v3` exists with ~11 commits
- Working tree clean (`git status`)

## Status check (run before starting)

```bash
cd /opt/git/personal/ESPresense
git branch --show-current             # should be feature/athom-plug-v3
git status                            # should be clean
git log --oneline main..HEAD          # should show ~11 commits
ls PRD/phase-1-handoff.md             # should exist (created at end of Phase 1)
cat PRD/phase-1-handoff.md            # read for any deviations from the PRD
```

If branch isn't checked out:
```bash
git checkout feature/athom-plug-v3
```

If `phase-1-handoff.md` is missing or empty, Phase 1 wasn't fully completed — go back and finish it before running Phase 2.

## Step 1 — Full build matrix (45 min)

Run every env that the CI matrix builds. Each must succeed.

```bash
cd /opt/git/personal/ESPresense

for env in esp32 esp32c3 esp32c3-cdc esp32c6 esp32c6-cdc esp32s3 esp32s3-cdc \
           esp32-verbose esp32c3-verbose esp32c6-verbose esp32s3-verbose \
           m5stickc m5stickc-plus m5atom macchina-a0 athom-smart-plug-v3; do
    echo "=========================================="
    echo "Building $env"
    echo "=========================================="
    pio run -e "$env" || { echo "FAIL: $env"; exit 1; }
done
echo "ALL BUILDS PASSED"
```

If any env fails:
- Read the compiler errors carefully
- The most likely failure is a missing `#ifdef HAS_*` guard in some non-Athom env where the new helper signature doesn't match a caller
- Compare against the design canon (`PRD/00-context-and-design.md`) for what should be `#ifdef`-guarded
- Don't merge until ALL envs pass

## Step 2 — Firmware size verification (5 min)

The CI build job hard-fails if any binary exceeds 1,966,080 bytes (1920 KB, the OTA partition size). Verify locally first.

```bash
cd /opt/git/personal/ESPresense

for env in esp32 esp32c3-cdc athom-smart-plug-v3; do
    fw=".pio/build/$env/firmware.bin"
    if [ ! -f "$fw" ]; then
        echo "$env: FILE NOT FOUND — build did not run"
        continue
    fi
    size=$(stat -c%s "$fw")
    pct=$(( size * 100 / 1966080 ))
    echo "$env: $size bytes ($((size/1024)) KB, ${pct}% of OTA cap)"
done
```

Expected output:
```
esp32:                  1.4-1.5 MB  (~75% of cap)
esp32c3-cdc:           1.3-1.4 MB  (~70% of cap)
athom-smart-plug-v3:   1.4-1.5 MB  (~75% of cap, slightly larger due to relay+CSE7766)
```

**Failure criteria**: any env > 1920 KB. If athom-smart-plug-v3 is over the cap:
- Verify `${sensors.lib_deps}` and `-D SENSORS` are NOT in our env (those add ~30-40 KB)
- Check if NimBLE pulled in extra dependencies
- Last resort: profile with `pio run -e athom-smart-plug-v3 --target size` for breakdown

## Step 3 — Static analysis (10 min)

The project uses cppcheck + clang-tidy via PlatformIO's `pio check`.

```bash
cd /opt/git/personal/ESPresense
pio check -e athom-smart-plug-v3 --skip-packages 2>&1 | tee /tmp/check-output.txt
```

Examine the output for:
- **New errors introduced by our code** (not existing warnings in unrelated files)
- Look for issues in: `src/Relay.cpp`, `src/CSE7766.cpp`, `src/Button.cpp`, `src/GUI.cpp`, `src/mqtt.cpp`, `include/defaults.h`, `src/main.cpp`

Filter for our changes:
```bash
grep -E "Relay\.cpp|CSE7766\.cpp|Button\.cpp|GUI\.cpp|main\.cpp|mqtt\.cpp|defaults\.h" /tmp/check-output.txt
```

Acceptable findings:
- "Function complexity" warnings on the parser — CSE7766 packet parsing is inherently complex
- "Magic number" warnings on the byte indices — these are protocol constants

Fix-required findings:
- Any actual error
- Memory issues (use-after-free, uninitialized read)
- Type mismatches
- Logic errors

If `pio check` reports a real issue, fix it and amend the relevant commit:
```bash
git add <fixed-files>
git commit --amend --no-edit
```

## Step 4 — UI build determinism (5 min)

Re-run `npm run build` and confirm it produces the same output as committed.

```bash
cd /opt/git/personal/ESPresense/ui
npm run build
cd ..
git status                          # should report no changes if build is deterministic
git diff src/ui_*.h                  # should be empty
```

If the rebuild produces different output:
- Could be a non-deterministic timestamp embedded in the SPA build
- Could be a node_modules version difference
- If the diff is whitespace-only or trivial: re-commit with `git commit --amend`
- If the diff is substantial: investigate the build tool

## Step 5 — Boot-trace inspection (10 min)

Quickly review the assembly entry points to confirm `Relay::EarlyInit()` is the first user code running.

```bash
cd /opt/git/personal/ESPresense
grep -n "Relay::EarlyInit\|Serial.begin" src/main.cpp | head -5
```

Expected output (line numbers approximate):
```
611:    Relay::EarlyInit();   // <-- before Serial.begin
615:    Serial.begin(1500000);
617:    Serial.begin(115200);
```

`Relay::EarlyInit()` should be the FIRST executable statement inside `void setup()` (after the `#ifdef HAS_RELAY` guard).

## Step 6 — Cross-reference verification (5 min)

Confirm every reference to a new module is `#ifdef`-guarded:

```bash
cd /opt/git/personal/ESPresense

# These greps should find ONLY guarded references outside of Relay.cpp/.h and CSE7766.cpp/.h
echo "=== Relay:: refs ==="
grep -rn "Relay::" src/ include/ | grep -v "Relay.cpp\|Relay.h"

echo ""
echo "=== CSE7766:: refs ==="
grep -rn "CSE7766::" src/ include/ | grep -v "CSE7766.cpp\|CSE7766.h"
```

Expected: every line should be either:
- Inside an `#ifdef HAS_RELAY` / `#ifdef HAS_POWER_MONITOR` block
- An include line that is itself guarded
- In `main.h` where the include is guarded

If you find an UNGUARDED reference, fix it before continuing.

## Step 7 — Settings JSON endpoint smoke test (10 min)

Verify the firmware exposes our new settings via the `/wifi/hardware` JSON endpoint. Easiest way: emulator or quick flash.

If you have the Athom plug available:
```bash
pio run -e athom-smart-plug-v3 -t upload
pio device monitor -e athom-smart-plug-v3
# After boot + initial config, browse to http://<plug-ip>/wifi/hardware
# Or curl: curl http://<plug-ip>/wifi/hardware | jq .
```

Expected: the JSON should include `defaults.relay_pin = 5`, `defaults.cse7766_rx_pin = 20`, etc.

If you don't have the plug yet, defer this to Phase 3.

## Phase 2 verification gate

All of these must be ✅ to proceed to Phase 3:

- [ ] All 16 platformio envs build cleanly (Step 1)
- [ ] Firmware size for athom-smart-plug-v3 is under 1920 KB (Step 2)
- [ ] No new static-analysis errors in our modified files (Step 3)
- [ ] UI build is deterministic — `npm run build` produces no git diff (Step 4)
- [ ] `Relay::EarlyInit()` is the first statement in `setup()` (Step 5)
- [ ] No unguarded cross-module references (Step 6)
- [ ] (Optional, defer if no hardware yet) `/wifi/hardware` endpoint exposes new settings (Step 7)

## Handoff to Phase 3

If all of Phase 2 verification passed:

1. Update `PRD/README.md` status section:
   ```
   - ✅ Phase 1 — Implementation
   - ✅ Phase 2 — Build verification
   - ⬜ Phase 3 — Hardware testing
   ```

2. Create `PRD/phase-2-handoff.md` with:
   - Confirmation of all 16 envs building
   - Firmware size for athom-smart-plug-v3 (exact bytes)
   - Any static-analysis findings that were intentionally left unfixed (with reasoning)
   - Any deviations from the verification steps

3. Phase 3 picks up by reading the handoff file and beginning hardware verification on the Athom plug.

If Phase 2 verification did NOT all pass:
- Do not proceed to Phase 3
- Fix the failing item(s)
- Re-run the verification step(s)
- If the fix required code changes, those are amendments to the relevant Phase 1 commits OR new commits — pick whichever produces a cleaner git history
