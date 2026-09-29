# PHASE6 — COMPLETION REPORT: Hand-wired demo Project Assembly render feature and full live HTTP verification

## What was added — `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`

**Note on git tracking (read this first — it changes what "committed" means for this phase):**
`Projects/` is listed in the repo's own `.gitignore` (`/Projects/`, comment: *"Never
committed - same-toolchain, same-build-run scoped by design"* — a Locked Design
Decision from the `editor-core-separation-11` campaign). Confirmed via
`git ls-files --error-unmatch Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`
→ `did not match any file(s) known to git`, and `git status` reports "nothing to
commit, working tree clean" even with this file heavily modified — this file has
**never** been tracked by git, despite prior campaigns' own completion reports
(`editor-core-separation-13`/`-15`) describing edits to this exact file as
"committed". This is a genuine conflict between PHASE0's Locked Decision #7 ("a
git commit... covering both the code change and the report") and a pre-existing,
deliberate, explicitly-documented repo policy. Used `ask_questions`; the user left
the resolution to the implementer's judgment. **Resolved**: respect the
pre-existing, deliberate `.gitignore` policy rather than force-add
(`git add -f`) around it — this phase's own commit covers the completion report
only; `HelloGame.cpp` itself is real, on-disk, fully live-verified (see below),
just intentionally outside git's tracking, exactly like every other file under
`Projects/`.

### The permanent change

Inside `RegisterProbeGame(gte::Core& core)`, immediately after the existing
`core.RegisterProjectRenderPassProvider("ProjectAssemblyProbe.FillTexture", ...)`
call, added one call to the new `Core::RegisterProjectRenderFeature()` (PHASE3):

```cpp
const bool screenTintRegistered = core.RegisterProjectRenderFeature(
    "ProjectAssemblyProbe.ScreenTint", gte::RenderFeatureStage::PostComposite,
    gte::RenderFeatureBlendMode::AlphaOver,
    /*priority=*/100,
    [](gte::rg::RenderGraphBuilder& builder, gte::rg::TextureHandle privateTarget, VkExtent2D /*extent*/) {
        builder.AddRenderPass(
            "ProjectAssemblyProbe.ScreenTint.Clear", gte::rg::PassKind::Graphics,
            [privateTarget](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(privateTarget, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f });
            },
            [](gte::rg::PassContext&) {},
            gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);
    });
GTE_LOG_INFO("ProjectAssembly", screenTintRegistered
    ? "ProjectAssemblyProbe_Game.dll: RegisterProjectRenderFeature('ProjectAssemblyProbe.ScreenTint') succeeded."
    : "ProjectAssemblyProbe_Game.dll: RegisterProjectRenderFeature('ProjectAssemblyProbe.ScreenTint') FAILED.");
```

Plus `#include <array>` (added alongside the pre-existing `#include <optional>`).

**`priority=100`, NOT the design doc's own sketched `priority=0` — a deliberate,
evidence-driven deviation, discovered live during this phase (see "Real bug hunt"
section below).** The design doc's Step 8.2 sketch used `priority=0`; at that
priority the tint was declared but produced **zero visible pixel difference**
whether enabled or disabled, because a pre-existing, unrelated demo plugin,
`DemoRenderFeatureV3Third` (`PostComposite` stage, priority 10, blend mode
`Replace`), runs later in the same stage's priority-ascending chain and its
`Replace` blend **unconditionally discards everything computed before it** —
this is a real, structural property of `RenderFeatureBlendMode::Replace`
combined with existing demo-plugin ordering, not a bug in
`RegisterProjectRenderFeature()`/`RenderFeatureCompositor` (proof below).
Bumping our own priority to `100` places our AlphaOver blend strictly AFTER that
Replace wipe, so it survives to the final composited image.

## Real bug hunt: why the tint was invisible at `priority=0`, and how it was proven NOT our own code's fault

1. First capture (`priority=0`, feature freshly registered) showed a plausible
   tan+blue-blob Game View — looked fine at a glance.
2. Toggling the feature on/off via `GET /render_graph/set_feature_enabled` and
   re-capturing `GET /get_game_view` produced **byte-identical PNG hashes**
   (`87F6E9AB...`) regardless of enabled state — confirmed via `Get-FileHash`.
   Toggling the pre-existing `DemoRenderFeatureV2` on/off, and even disabling
   the built-in `RenderOpaque` pass, ALSO produced the exact same hash — a
   real, reproducible "nothing I toggle changes anything" signal.
3. Sanity check: disabling **all six** plugin/project render features at once
   DID change the image — to solid magenta (the documented
   `editor-core-separation-20` "undefined memory" signature, since
   `RenderFeatureCompositor::ContributeRenderGraphPasses()` early-returns when
   `combinedList` is empty, so nothing writes `GameViewComposited` that frame).
4. Disabling **only** `DemoRenderFeatureV3Third` (leaving everything else,
   including our own feature, untouched) reproduced a very similar
   magenta-dominated image — and doing the SAME thing with our own ScreenTint
   feature **completely disabled** produced the **identical** result. This is
   the conclusive control: the magenta-exposure behavior depends ONLY on
   `DemoRenderFeatureV3Third`'s own enabled state, never on our feature's own
   state — proving our code was never the problem; `DemoRenderFeatureV3Third`'s
   `Replace` blend was simply overwriting (masking) everything declared before
   it in the same stage, every time, regardless of what those earlier entries
   were.
5. Fix: raised our own feature's priority from `0` to `100` (see above). Full
   before/after evidence below (item 3) confirms this fix works and the pixel
   math matches the documented `AlphaOver` formula exactly.

This is exactly the kind of "PostComposite priority ordering has real,
reachable failure modes when a `Replace`-mode entry exists in the chain"
consequence PHASE0/PHASE2's own design intentionally allows (Locked Decision #4
only mandates the GPU-state key be slot-derived, never the ordering itself) —
restated here plainly as a real, live-discovered interaction for anyone writing
a FUTURE Project Assembly `PostComposite` feature: pick a priority higher than
any known `Replace`-mode entry in the same stage if you want your own
contribution to survive to the final image, or use a distinct blend mode /
verify empirically like this phase did.

## Live verification sequence (Step 3.3), full evidence

All captures below are from a real, running `build\GreatTamanaEditor.exe`,
driven exclusively via `run_app_background`/`gte_send_request`/
`stop_app_background`, with `HelloGame.cpp` in its FINAL, single-feature,
`priority=100` state unless explicitly noted otherwise (bounded-slot-reuse
proof and two-feature-blend proof both used temporary variants of the source,
rebuilt and reverted before this report was written).

### Item 1 — Editor loads `ProjectAssemblyProbe`, registration succeeds

`GET /get_logs?category=ProjectAssembly&limit=10` (fresh boot, final code):

```
"ProjectAssemblyProbe_Game.dll: GTE_RegisterProject called with a real, live gte::Core&."
"ProjectAssemblyProbe_Game.dll: RegisterProjectRenderFeature('ProjectAssemblyProbe.ScreenTint') succeeded."
"Loaded Project Assembly 'ProjectAssemblyProbe_Game.dll' from ...\\build\\project_assemblies\\ProjectAssemblyProbe_Game.dll"
```

### Item 2 — `GET /render_graph` entry + Editor "Render Graph" panel `[Project]` tag

`GET /render_graph`'s `render_features[]` array contains:

```json
{
    "blend_mode": "AlphaOver",
    "enabled": true,
    "is_project_feature": true,
    "is_v3": false,
    "name": "ProjectAssemblyProbe.ScreenTint",
    "priority": 100,
    "stage": "PostComposite"
}
```

`GET /activate_tab?name=Render Graph` followed by `GET /get_swapchain` (real
screenshot, captured live) shows the "Plugin Render Features" section listing:

```
[PostComposite] DemoRenderFeatureV2 - blend Replace
[PostComposite] DemoRenderFeatureV3 - blend AlphaOver           [v3]
[PostComposite] DemoRenderFeatureV3Third - blend Replace        [v3]
[PostComposite] ProjectAssemblyProbe.ScreenTint - blend AlphaOver  [Project]
[PreUI] DemoRenderFeatureV2Second - blend AlphaOver
[PreUI] DemoRenderFeatureV3Second - blend AlphaOver             [v3]
```

— the new `"[Project]"` tag (light green, per PHASE2) renders next to our row,
exactly mirroring `"[v3]"`'s own precedent, and the row is correctly sorted
into position by its own `priority=100` (after `DemoRenderFeatureV3Third`'s
`10`).

### Item 3 — visible in a real Game View screenshot (the single most important check)

Before/after `GET /get_game_view`, `priority=100` state, isolating ONLY the
`ProjectAssemblyProbe.ScreenTint` toggle (all five demo-plugin features left
untouched/enabled throughout):

| State | Background pixel (5,5) RGB | PNG |
|---|---|---|
| Tint OFF | `(212, 199, 185)` | `final_tint_off.png` |
| Tint ON  | `(218, 169, 157)` | `final_tint_on.png` |

**Exact match to the documented `AlphaOver` formula** (`out = dst*(1-a) + src*a`,
`src = (255,0,0)`, `a = 0.15`):
`R: 212*0.85 + 255*0.15 = 218.45 → 218` ✓
`G: 199*0.85 + 0*0.15 = 169.15 → 169` ✓
`B: 185*0.85 + 0*0.15 = 157.25 → 157` ✓

A visibly warmer/pinkish Game View is also directly visible by eye in the
captured screenshots (tan → dusty pink), confirmed via `load_image` during this
session.

### Item 4 — real hot-reload cycle

`POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` (no source
change — a reload with unchanged source is still a genuine unload+recompile+
reload cycle, per the phase file's own explicit allowance):

```json
{"cycle_id":1,"last_error_message":"","last_outcome":"Success","phase":"Idle","phase_elapsed_ms":0,"project_name":"ProjectAssemblyProbe"}
```

`GET /project_assembly/hot_reload/status` polled immediately after — same
`"last_outcome":"Success"`, confirming a completed, non-pending cycle.

### Item 5 — post-reload: exactly one entry, never duplicated, never dropped

`GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` immediately
after the reload:

```json
{"component_type_names":["ProbeHotReloadMarker"],"panel_names":["Probe Panel"],"project_name":"ProjectAssemblyProbe","render_feature_names":["ProjectAssemblyProbe.ScreenTint"],"render_pass_names":["ProjectAssemblyProbe.FillTexture"]}
```

`GET /render_graph`'s own `render_features[]` count for the exact name
`ProjectAssemblyProbe.ScreenTint` (PowerShell `Where-Object` count): **1**.
`GET /get_game_view` re-captured post-reload: same tint still visible (same
byte size, `13103` bytes, as the pre-reload capture).

### Item 6 — two project render features blend correctly, in priority order

Temporarily added a second, `TEMPORARY`-labeled feature,
`"ProjectAssemblyProbe.ScreenTint2"` (blue tint, `priority=101` — one above
ScreenTint's own `100`, so it composites strictly after it in the same
`PostComposite` chain), per the ambiguity-checkpoint resolution below. Rebuilt,
relaunched, captured:

```
both tints bg: (186, 144, 172)
```

Predicted by hand-applying the SAME `AlphaOver` formula twice (red tint from
`(212,199,185)` → `(218,169,157)`, then blue tint on top of that):
`R: 218*0.85 = 185.3 → 185`; `G: 169*0.85 = 143.65 → 144`; `B: 157*0.85 + 255*0.15 = 133.45+38.25 = 171.7 → 172`.
**Actual `(186,144,172)` vs. predicted `(185,144,172)` — matches to within 1
unit of 8-bit rounding.** `GET /render_graph` confirmed both entries present
simultaneously (`ProjectAssemblyProbe.ScreenTint` priority 100,
`ProjectAssemblyProbe.ScreenTint2` priority 101, both `is_project_feature:true`).

**Per this phase's own ambiguity checkpoint (Step 3.4), resolved via
`ask_questions`** — the user left the choice to the implementer's judgment.
**Resolved: temporary.** `ScreenTint2` was added, this proof captured, then
fully removed again (stopped the Editor, deleted the temporary block, rebuilt,
confirmed the file byte-for-byte matches the single-feature final state shown
above, relaunched, re-confirmed `ProjectAssemblyProbe.ScreenTint` alone is
registered and visible again). The final, permanently-kept `HelloGame.cpp`
therefore has only the ONE `ScreenTint` feature.

### Item 7 — bounded-slot-reuse proof (≥ `kMaxConcurrentProjectRenderFeatures + 4` = 20 cycles)

**Per this phase's own ambiguity checkpoint (Step 3.4), resolved via
`ask_questions`** — the user left the choice to the implementer's judgment.
**Resolved: real `POST /project_assembly/hot_reload` cycles, source edited
between each one** (option offered by the phase file itself) — chosen over
building a new, temporary HTTP debug command, since it needed zero new engine
code, exercises PHASE4's ledger teardown wiring 20 times for free, and most
faithfully mirrors a real developer renaming their effect during iteration.

Procedure: a small PowerShell loop edited `HelloGame.cpp`'s registered
`debugName` string (`ProjectAssemblyProbe.ScreenTint` → `...Cycle01` →
`...Cycle02` → ... → `...Cycle20`), calling
`POST /project_assembly/hot_reload?name=ProjectAssemblyProbe` after each edit
(20 real unload+recompile+reload cycles against the SAME running Editor
process). All 20 reported `last_outcome:"Success"`:

```
2:Success; 3:Success; 4:Success; 5:Success; 6:Success; 7:Success; 8:Success; 9:Success; 10:Success;
11:Success; 12:Success; 13:Success; 14:Success; 15:Success; 16:Success; 17:Success; 18:Success; 19:Success; 20:Success
```//(cycle 1 was the earlier, unrelated no-source-change reload from item 4, run first — 20 register/unregister cycles total across the whole sequence)

**Post-loop state**: `GET /render_graph`'s `render_features[]` filtered to
`*ScreenTint*` → count **1**, name `ProjectAssemblyProbe.ScreenTintCycle20`.
`GET /project_assembly/debug/ledger?name=ProjectAssemblyProbe` →
`"render_feature_names":["ProjectAssemblyProbe.ScreenTintCycle20"]` — exactly
one, matching. `GET /get_game_view` still shows the tint correctly.

**Bounded GPU-state slot consumption — real, mechanical, log-based proof**
(`GET /get_logs?category=RenderFeatureCompositor&keyword=ScreenTint&limit=200`),
every single register/unregister pair across the ENTIRE 20-cycle run:

```
RegisterProjectFeature('ProjectAssemblyProbe.ScreenTint') succeeded - claimed GPU-state slot 15.
UnregisterProjectFeature('ProjectAssemblyProbe.ScreenTint') succeeded - released GPU-state slot back to the free list.
RegisterProjectFeature('ProjectAssemblyProbe.ScreenTintCycle01') succeeded - claimed GPU-state slot 15.
UnregisterProjectFeature('ProjectAssemblyProbe.ScreenTintCycle01') succeeded - released GPU-state slot back to the free list.
... (identical claimed/released "GPU-state slot 15" pair for every one of cycles 02 through 20, confirmed individually for Cycle19/Cycle20 too) ...
RegisterProjectFeature('ProjectAssemblyProbe.ScreenTintCycle20') succeeded - claimed GPU-state slot 15.
```

**Every single one of the 21 register calls (original + 20 renames) claimed
the EXACT SAME slot, `15`** — the free-list genuinely reuses one bounded slot
indefinitely rather than growing, confirming `kMaxConcurrentProjectRenderFeatures`-
bounded GPU descriptor-set consumption live, mechanically, not merely by code
review. `GET /get_logs?min_level=Error` across the whole session: **0
entries** — no crash, no assertion, no error of any kind across the whole
20-cycle sequence.

After this proof, the source was reverted to the plain
`"ProjectAssemblyProbe.ScreenTint"` name (regex-reverted, rebuilt, and the
full file re-read to confirm a byte-clean, single-feature final state — see
"What was added" above), then relaunched one final time to re-confirm the
`priority=100` fix still shows the correct tint from a completely fresh boot.

### Item 8 — `gte_plugin_abi`'s existing `_v2`/`_v3` demo plugins unaffected

`GET /render_graph`'s `render_features[]`, captured after every mutation this
phase performed (hot reload, 20 rename cycles, temporary second feature added
then removed), still lists all five, unchanged, still enabled:

```
DemoRenderFeatureV2        (PostComposite, priority 0,  Replace)
DemoRenderFeatureV3        (PostComposite, priority 0,  AlphaOver)  [v3]
DemoRenderFeatureV3Third   (PostComposite, priority 10, Replace)    [v3]
DemoRenderFeatureV2Second  (PreUI,         priority 0,  AlphaOver)
DemoRenderFeatureV3Second  (PreUI,         priority 0,  AlphaOver)  [v3]
```

## Ordering safety net (PHASE5) confirmed live

`GET /get_logs?keyword=contradiction` and
`GET /get_logs?category=RenderPassHonesty` /
`GET /get_logs?category=FrameDebuggerCoverage` all returned **0 entries**
across the whole session (fresh boot through the 20-cycle rename run) —
`DetectRenderPassEventContradictions()` never fired for our
`RenderPassEvent::AfterEverything`-tagged pass, and neither the render-pass
honesty detector (`editor-core-separation-21`) nor the Frame Debugger coverage
detector (`editor-core-separation-22`) found anything to complain about.

## Self-double-check (PHASE0's Locked Decision #9)

Per PHASE0_MASTER_STRATEGY.md's Locked Decision #9 (this phase explicitly named
as a strong candidate), a `delegate_task(position: "next")` self-double-check
was issued, instructed to: re-read `HelloGame.cpp` for syntactic sanity and
confirm exactly one, final, non-temporary `ScreenTint` registration; confirm
`git status` shows nothing unexpected; run an incremental build; and
independently re-confirm registration success + the `render_features[]` entry
shape + a real `get_game_view` 200 response against a freshly launched Editor,
closing it again afterward. That sub-task runs as a subsequent step in this
same task sequence rather than returning synchronously inside this message —
its own findings, if any, surface independently of this report. Every item it
was asked to check was ALSO independently, first-hand verified by this same
implementing session before writing this report (see every "Item N" section
above), so this report's own conclusions do not depend solely on that
delegated check's outcome.

## Ambiguity encountered and resolved (summary)

1. **Reusing `ProjectAssemblyProbe` vs. a fresh project** — no ambiguity
   surfaced; reused it exactly as the phase file's own Step 2 recommended, with
   zero destructive change to its pre-existing capabilities (the
   `ProbeHotReloadMarker` entity/component and the `FillTexture` render-pass
   provider are both byte-for-byte untouched).
2. **Bounded-slot-reuse driving mechanism** (Step 3.4) — resolved via
   `ask_questions`; user left it to the implementer. Chose the real
   `hot_reload`-loop-with-edited-source approach (see item 7 above) over
   building a new temporary HTTP debug command.
3. **Whether to keep the second demo feature permanently** (Step 3.3 item 6) —
   resolved via `ask_questions`; user left it to the implementer. Chose
   temporary (added, proof captured, then fully removed) to keep the final,
   permanently-kept `HelloGame.cpp` minimal and focused on the one feature this
   campaign's own Definition of Done actually requires.
4. **`HelloGame.cpp` is gitignored, contradicting this phase's own "commit the
   code change" instruction** (discovered fresh during this phase, not
   anticipated by the phase file) — resolved via `ask_questions`; user left it
   to the implementer. Chose to respect the pre-existing, deliberate
   `/Projects/` "never committed" policy rather than force-add around it — see
   the dedicated section at the top of this report for the full reasoning.
5. **The invisible-tint bug at `priority=0`** — not a pre-anticipated ambiguity
   checkpoint in the phase file, but a genuine, live-discovered correctness
   question about WHOSE code was at fault. Resolved by direct, controlled A/B
   investigation (see "Real bug hunt" section) rather than guessing or
   silently reporting a false "looks fine" — root-caused to a pre-existing
   demo plugin's `Replace` blend mode, fixed by choosing a higher priority for
   our own feature, `ask_questions` not needed since the root cause and fix
   were both mechanically, unambiguously provable.

## Build / test results

- Incremental build (`cmake --build build`, working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`): succeeded, zero errors,
  run repeatedly across this phase (initial feature add, priority fix,
  temporary second-feature add, temporary second-feature removal, final
  restore) — every single incremental build was clean.
- No new/changed Tier-1 tests this phase (PHASE6 is the live-verification
  phase per PHASE0's own plan; PHASE1-5 already added every Tier-1 test this
  campaign needed) — no `ctest` filter to run for this phase's own code beyond
  what PHASE1-5 already covered (unaffected, not re-run here per PHASE0's
  Locked Decision #2 — targeted `ctest` is reserved for a phase's own new/
  changed tests, and this phase added none).
- Live verification: every item in Step 3.3 (1 through 8) completed with real,
  captured evidence (screenshots, JSON snippets, log excerpts, and — for items
  3/6 — hand-verified pixel-level blend math) as itemized above.
- `stop_app_background` called at the end of every launch cycle;
  `stop_app_background(image_name: "GreatTamanaEditor.exe")` confirmed no
  stray process remains at the very end of this phase.

## What was deliberately left alone / cleaned up

- `gte_plugin_abi`'s `_v2`/`_v3` demo plugins — untouched (confirmed, item 8).
- `RenderFeatureCompositor.h/.cpp`, `Core.h/.cpp`,
  `ProjectAssemblyRegistrationLedger.h/.cpp`, `EditorCapabilities.h`,
  `EditorHotReloadDebugCapability.cpp`, `RenderGraphCompilerTests.cpp` — all
  PHASE1-5's own already-completed, already-committed work; NOT touched by
  this phase at all.
- The temporary `"ProjectAssemblyProbe.ScreenTint2"` feature (item 6) and every
  temporary `"ProjectAssemblyProbe.ScreenTintCycleNN"` rename (item 7) — both
  fully removed; the final, permanently-kept `HelloGame.cpp` registers exactly
  one feature, named `"ProjectAssemblyProbe.ScreenTint"`, at `priority=100`.
  Confirmed via a full, fresh re-read of the final file (reproduced in full in
  the "What was added" section above).
- Scratch evidence files (`*.png`/`*.json` captures, a handful of PowerShell
  one-liners) were all written OUTSIDE this repository
  (`C:\Users\F5954\Documents\TAMANA\*.png`/`*.json`, the parent folder of the
  repo itself, never inside `GreatTamanaEngine\`) — confirmed via
  `git status` reporting a clean tree with zero untracked files inside the
  repo.

## End-of-phase checklist

1. ✅ Incremental build (`cmake --build build`) succeeds.
2. ✅ Every live verification item in Step 3.3 completed, with real evidence
   (screenshot pixel values/byte sizes, JSON snippets, log excerpts) — never
   merely "looked fine" (see the invisible-tint-at-priority-0 investigation,
   which is the clearest proof this session did NOT stop at a superficial
   glance).
3. ✅ `stop_app_background` called; confirmed no stray `GreatTamanaEditor.exe`
   process is left running.
4. ✅ This report.
5. `git_add` + `git_commit` covering this report (and any other real,
   non-gitignored permanent change from this phase — there is none;
   `HelloGame.cpp` itself is intentionally NOT committed, per the resolved
   ambiguity above) — done immediately after this report is written.
