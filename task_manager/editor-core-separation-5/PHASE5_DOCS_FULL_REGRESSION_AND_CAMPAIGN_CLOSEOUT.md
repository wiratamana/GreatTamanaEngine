# PHASE5 — Docs, full regression pass, and campaign closeout

**Parent:** `PHASE0_MASTER_STRATEGY.md` — read it first. Also read
`PHASE1`-`PHASE4`'s own `PHASEn_COMPLETION_REPORT.md` files in full before
starting — this phase's own regression pass is only meaningful once all four
prior phases' real diffs are known.

**Use `ask_questions` if anything below is ambiguous, or if the full regression
pass in Step 3.3 surfaces something this phase file does not already tell you
how to handle — do not silently guess, and do not silently loosen a failing
test's expectation without understanding why it failed.**

**This is the ONE phase in this campaign allowed to run a full clean build and
full `ctest` pass (`PHASE0_MASTER_STRATEGY.md` Workflow Rule 1).**

---

## Step 1: The Goal

Three things, in order: (1) bring `docs/conventions/plugin-architecture.md` and
`plugins/gte_plugin_abi/PublicSurface.md` fully up to date with what Phases 1-4
actually shipped (not just what Phase 1 already added to `PublicSurface.md`);
(2) run the one full, real regression pass this whole campaign is allowed to
run, proving nothing regressed anywhere in the engine (not just in the four
touched files); (3) write `CAMPAIGN_COMPLETION_REPORT.md` closing out this
campaign.

## Step 2: The Situation

By the time this phase starts, `plugins/gte_plugin_abi/` has two new header
files (Phase 1), and all four demo plugins under `plugins/demo_*/` have been
rewritten onto them (Phases 2-4) with (per every prior phase's own Verification
section) zero observable behavior change. `docs/conventions/plugin-architecture.md`
does not yet mention any of this (Phase 1 only touched `PublicSurface.md`,
per its own Step 3.3 scope) — a reader of that convention doc today would have
no idea `PluginExportsMacro.h`/`SingleCapabilityPluginModule.h`/
`ZeroCapabilityPluginModule` exist, or that every demo plugin now uses them.

Run `git_status` before Step 3.1 (`PHASE0_MASTER_STRATEGY.md` Workflow Rule
10) — confirm the branch still reads `feature/editor-core-separation` and the
tree is clean or contains only Phase 1-4's already-committed diffs.

## Step 3: The Plan

### 3.1 — Update `docs/conventions/plugin-architecture.md`

Read the file in full first. Add one new subsection, placed right after the
existing "`IPluginModule` and the three fixed exports" section (before "The
shared/DLL CRT requirement..." section), titled something like "Authoring
sugar — `PluginExportsMacro.h` / `SingleCapabilityPluginModule.h` (optional,
additive)". Content should cover, briefly (this doc is a CONVENTION reference,
not a tutorial — point to the two header files' own doc comments and the source
proposal document for full detail, do not duplicate the whole rationale here):

- What the two files add (`GTE_DEFINE_PLUGIN_EXPORTS`/
  `GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE` macros hiding the `extern "C"`
  block; `SingleCapabilityPluginModule<T>`/`ZeroCapabilityPluginModule` hiding
  the `IPluginModule` glue class for the one-capability/zero-capability case).
- That this is 100% optional, additive, zero-ABI-change sugar — `PluginHost`
  and every existing interface are completely unaware of which flavor a given
  plugin `.dll` used to produce its three exports; a plugin implementing 2+
  capabilities from one module still hand-writes its own `IPluginModule`
  exactly as before.
- That all four of this repository's own demo plugins (`demo_hello_world`,
  `demo_render_feature`, `demo_render_feature_second`, `demo_editor_panel`) now
  use this sugar, as living proof — point at their current file contents as
  the canonical, up-to-date example to copy for a new plugin, rather than
  re-pasting a full before/after code listing into this convention doc (that
  full before/after already lives in the source proposal document and in
  `PHASE2`/`PHASE3`/`PHASE4`'s own phase files, permanently, under
  `task_manager/editor-core-separation-5/`).

### 3.2 — Confirm `plugins/gte_plugin_abi/PublicSurface.md` is complete

Re-read it. Phase 1 already added the one short bullet documenting the two new
files (Step 3.3 of `PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md`) — confirm
that bullet is still present and accurate (e.g. still names
`ZeroCapabilityPluginModule` if Phase 1 added it, matches this campaign's final
decision). If Phase 1's own addition text needs a small correction, fix it here
— do not leave a stale/inaccurate line.

### 3.3 — Confirm `AGENTS.md`'s "Plugin Architecture" section still reads true

`search_in_dir` for `"Plugin Architecture"` in `AGENTS.md` (it is a short
summary section, not the full convention doc). Read it. It should NOT need any
change (this campaign changes nothing about the architecture itself, only how
much boilerplate a plugin author writes) — but confirm this by actually reading
it rather than assuming; if it happens to over-claim something this campaign
now makes MORE true (e.g. "even less boilerplate now"), a one-sentence addition
is fine, but is not required.

### 3.4 — The one full regression pass this campaign is allowed to run

1. Full clean build of the MAIN tree:
   ```
   cmake --build build
   ```
   (This is the tree Phases 2-4 already incrementally rebuilt piece by piece —
   this is the first time in this campaign every target, including
   `GreatTamanaEngineTests`, gets rebuilt together. It is not a from-scratch
   `rd /s /q build` reconfigure — that is not required, the existing `build/`
   tree's own CMake cache is still valid, nothing in this campaign touched
   `CMakeLists.txt`/toolchain settings.)
2. Confirm zero errors across the whole build.
3. Full regression test:
   ```
   cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
   ```
   Confirm 100% of executed tests pass (compare the total test count against
   the most recent prior campaign's own closeout number — e.g.
   `editor-core-separation-4/CAMPAIGN_COMPLETION_REPORT.md` or whatever the
   latest `CAMPAIGN_COMPLETION_REPORT.md`/regression report anywhere under
   `task_manager/` reports most recently — a materially LOWER total test count
   than that baseline, with no explanation, is itself a signal something is
   wrong, e.g. a test file failed to even compile/register).
4. **If any test newly fails and the cause is not immediately obvious from the
   failure output**, this is the one narrow case this whole campaign is
   allowed to use `delegate_task` for (`PHASE0_MASTER_STRATEGY.md` Workflow
   Rule 7's own stated exception) — delegate a focused fix-it task, instructing
   it to use `ask_questions` if it hits its own ambiguity, then re-run `ctest`
   once fixed before continuing.
5. Live HTTP smoke test — `run_app_background` on the freshly-rebuilt
   `build\GreatTamanaEditor.exe`, then via `gte_send_request` (do NOT call
   `POST /clear_logs` before the first `GET /get_logs` check below — the four
   `"Loaded plugin '...'"` lines are only ever logged once, during this exact
   process's own startup, which already happened by the time
   `run_app_background` returns; clearing the log buffer first would erase
   them permanently for this session and make the very check below
   impossible to ever pass):
   - `GET /get_logs?limit=50` — confirm all four plugins still log a
     successful load line (`HelloWorldPlugin`, `DemoRenderFeaturePlugin`,
     `DemoRenderFeaturePluginSecond`, `DemoEditorPanelPlugin`) with the exact
     same names as every prior campaign's own completion reports quote.
   - `GET /get_logs?limit=50&min_level=warning` — confirm the pre-existing
     shared-CRT-risk warning (`editor-core-separation-4` PHASE1) and the
     pre-existing "2 loaded plugins implement IRenderFeatureModule_v1"
     warning (`editor-core-separation-4` PHASE5) are BOTH still present,
     unchanged — this campaign must not add, remove, or reword either one.
   - `GET /list_tabs` — confirm `"Demo Plugin Panel"` still listed.
   - `GET /get_swapchain` (or `/get_game_view`) — `load_image`/inspect the
     result and confirm the Game/Scene View still shows the same solid magenta
     baseline documented since `editor-core-separation-3`/`-4` (both
     render-feature demo plugins still clear to `(1,0,1,1)` — this is the one
     visual proof that `SingleCapabilityPluginModule<IRenderFeatureModule_v1>`'s
     `QueryCapability()` dispatch genuinely still reaches
     `AddRenderGraphPasses()` at runtime, not just at compile time).
   - **Consumer #3 from `PHASE0_MASTER_STRATEGY.md` Step 2 — the literal pass
     name string `"DemoRenderFeaturePlugin_Clear"` shown in the Editor's own
     "Render Graph" panel — is not covered by any check above, and this is
     the only phase in this whole campaign left that can still catch a
     regression in it, so it must be confirmed here**: `GET
     /activate_tab?name=Render%20Graph` (a known built-in panel name, already
     confirmed present in `GET /list_tabs`'s own output), then `GET
     /get_swapchain` again and `load_image`/inspect it — confirm the
     now-focused "Render Graph" panel's own pass list visibly shows both
     `"DemoRenderFeaturePlugin_Clear"` and
     `"DemoRenderFeatureSecondPlugin_Clear"` text, byte-identical to the exact
     strings `plugins/demo_render_feature/RenderFeaturePlugin.cpp` and
     `plugins/demo_render_feature_second/RenderFeaturePlugin.cpp` pass to
     `AddFullscreenClearPass()`.
   - `stop_app_background` when done.
6. Re-run BOTH standalone probes one final time from their OWN dedicated inner
   builds (`build-plugin-abi-handshake-probe\...`,
   `build-plugin-isolation-probe\...`) exactly as Phases 2/4 already did, purely
   as a final confirmation nothing drifted between phases (each phase's own
   local probe run already passed individually — this is the "all four
   migrations coexist correctly at once" final check).

### 3.5 — Write `CAMPAIGN_COMPLETION_REPORT.md`

Summarize, per this repository's own established campaign-closeout shape
(mirror `editor-core-separation-4/CAMPAIGN_COMPLETION_REPORT.md`'s own
structure — it has these exact sections, in this order, and this report must
too): what this campaign set out to do (link back to the source proposal
document), what shipped phase by phase (one short paragraph per Phase 1-4,
each naming its own real file changes), the full clean build + full `ctest` +
every-probe + live-smoke-test evidence gathered in 3.4 (including the exact
before/after `ctest` total-test-count comparison from Step 3.4.3), an
explicit, itemized restatement of "what does NOT change" (mirroring the
source proposal document's own "What does NOT change" section — every ABI
field, every interface, every export name, byte-for-byte identical), the two
docs updated (3.1/3.2/3.3), and the four demo plugins migrated with their
exact before/after line counts (pull the real final numbers from each of
`PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md`/
`PHASE4_COMPLETION_REPORT.md` — do not re-guess them here). Also restate,
honestly, this campaign's own "Decisions made without `ask_questions`" (from
`PHASE0_MASTER_STRATEGY.md`) as real, final decisions now that they have been
implemented and verified, not hypothetical ones.

**Two more sections `editor-core-separation-4/CAMPAIGN_COMPLETION_REPORT.md`
also has, that this report must not skip:**

- **"Deviations from the original plan"** — read every one of
  `PHASE1_COMPLETION_REPORT.md`..`PHASE4_COMPLETION_REPORT.md`'s own
  "Completion"/deviation notes and consolidate them into one list here (one
  bullet per phase, even if it is just "no deviations" for that phase — do
  not silently omit a phase that had none), plus anything this phase (5)
  itself deviated on or discovered (e.g. a stale doc line, an environment
  quirk, an unexpected `ctest`/probe result and how it was resolved).
- **"What remains genuinely open (honest, not silently dropped)"** — restate,
  briefly, every Non-Goal from `PHASE0_MASTER_STRATEGY.md` Step 1 that is
  still true after this campaign (no new capability interface, a
  2+-capability plugin still hand-writes its own `IPluginModule`, no hot
  reload, no toolchain switch, no real feature migrated onto the plugin
  mechanism, etc.) — none of these were ever meant to be solved by this
  campaign, but a reader of this report alone (without also reading PHASE0)
  should still come away knowing they are still open, exactly like every
  other `editor-core-separation-*` campaign's own closeout has always done.

## Completion

Run `git_status` (Workflow Rule 10) and confirm the diff about to be staged is
exactly the two docs files (3.1/3.2, plus 3.3's own edit if it made one) and
`CAMPAIGN_COMPLETION_REPORT.md` — nothing else (no leftover build artifact, no
stray `build-plugins-off`-style throwaway directory left behind by this phase's
own regression pass). Then `git_add` + `git_commit` (message referencing PHASE5
and this campaign's closeout, e.g. `"editor-core-separation-5 PHASE5: docs +
full regression + campaign closeout"`).
