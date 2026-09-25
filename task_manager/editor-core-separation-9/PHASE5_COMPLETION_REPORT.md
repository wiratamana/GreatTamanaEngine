# editor-core-separation-9 — PHASE5 COMPLETION REPORT

**Phase:** `PHASE5_DOCS_FULL_REGRESSION_AND_CAMPAIGN_CLOSEOUT.md`
**Status:** DONE. Docs updated, one full clean build + full `ctest` regression
pass + one final live HTTP smoke test run (the ONE time this whole campaign is
allowed to do so), zero regressions found, `CAMPAIGN_COMPLETION_REPORT.md`
written.

## What changed

### Docs (the actual deliverable of this phase)

- `AGENTS.md` — "Plugin Architecture" section: added one new dense paragraph
  (after the existing `editor-core-separation-3` paragraph, before "Full
  convention:") summarizing what `_v3` is, why it exists (the `_v2`
  closed-enumeration problem), the operation-registry mechanism, the 2-pass
  GPU blur demo as concrete proof, the blackboard, the `Dispatch()`
  group-count cap's "no device-lost recovery path" caveat, and a pointer to
  this campaign's `PHASE0_MASTER_STRATEGY.md`/`CAMPAIGN_COMPLETION_REPORT.md`.
- `docs/conventions/plugin-architecture.md` — new "`_v3` Generic
  Render-Feature System — Feature-Agnostic Resource Graph + Operation
  Registry" section (inserted after the existing "`_v2` Render-Feature
  System" section), covering: the resource vocabulary/two-phase pass builder,
  the handle-translation table + the real use-after-free hazard found and
  fixed during PHASE2, the operation registry's exact entry shape + the full
  4-row built-in-operations table (copied verbatim from
  `PHASE2_COMPLETION_REPORT.md`/`PHASE3_COMPLETION_REPORT.md`, not
  re-derived), the 2-pass blur demo's exact shape, the blackboard's exact
  contract (including the host-side-logging design decision), the
  diagnostics confirmation (individual passes already generic; the one real
  `isV3` gap found and fixed), and the caps/limits section restating the
  `Dispatch()` group-count cap's own "no device-lost recovery path" caveat.
- `plugins/gte_plugin_abi/PublicSurface.md` — new "Added by later phases"
  bullet (editor-core-separation-9, PHASE5) listing every type that actually
  crosses the ABI boundary this campaign (`PluginRenderResource.h`'s types,
  `IPluginRenderPassBuilder_v3.h`'s types, `IRenderFeatureModule_v3`),
  explicitly confirming `PluginRenderOperationRegistry`/
  `PluginRenderPassBuilderAdapter_v3`/`PluginRenderResourceTranslation.h`/
  `PluginRenderPassBuilderAdapterV3Validation.h` are `gte_core`-internal —
  every file this bullet names was re-read directly (via `read_file`
  earlier this same session, before starting PHASE5's own work) rather than
  written from memory of the phase plan, per this phase's own explicit
  instruction (mirroring `editor-core-separation-8`'s own "CORRECTED
  FINDING" precedent for honest disclosure).

### Full regression pass (this phase's own mandatory, one-time checkpoint)

1. `git_status` at phase start: branch `feature/editor-core-separation`,
   working tree clean (only this campaign's own already-committed PHASE1-4
   diffs and this same folder's docs).
2. **Ambiguity resolved via `ask_questions`**: whether to do a genuine clean
   rebuild (delete `build/` and reconfigure) or reuse the existing
   incremental tree. The human was not available in time (`ask_questions`
   returned "user is not at office, leave the decision making up to you").
   Decision: mirrored the established precedent this phase's own file cites
   (`editor-core-separation-1` PHASE19: "deleted `build/` entirely,
   reconfigured, rebuilt") — confirmed FIRST, before deleting anything, that
   every third-party dependency this project needs (SDL3, Vulkan headers,
   volk, VMA, stb, httplib, nlohmann/json, KTX-Software, glm, saba, imgui,
   imguizmo, googletest) is a persistent, gitignored, ALREADY-DOWNLOADED tree
   living in `include/`/`third_party/` (outside `build/`), per `BUILDING.md`'s
   own explicit "subsequent configures reuse what was already downloaded and
   don't need the network again" statement — so a genuine clean rebuild would
   NOT trigger any network access, de-risking the "genuine clean rebuild"
   choice entirely.
3. **Genuine clean rebuild**: `rmdir /s /q build`, then
   `cmake -S . -B build -G Ninja` (explicit `CMAKE_C_COMPILER`/
   `CMAKE_CXX_COMPILER` pointing at the same scoop-installed GCC 15.2.0
   toolchain the prior tree used) — configure succeeded with only the
   pre-existing, already-documented `MingwRuntime.cmake` shared-CRT-no-op
   warnings and the pre-existing `third_party/ktx` "no names found" `git
   describe` warning (both confirmed benign, unrelated to this campaign, in
   every prior phase's own report). Then `cmake --build build` (no target
   filter — everything): **557/557 build steps, zero errors.**
4. **Full `ctest -C Debug --output-on-failure`**: **1882 tests total, 1880
   PASSED (100% of executed tests), 2 legitimate, pre-existing,
   environment-gated skips** (`PmxLoaderRealModelSmokeTest.
   LoadsAnMmdModelIfPresentOnThisMachine`;
   `CoreHeadlessConstructionTest...WithNoEditorLayerHookSet` — this
   development machine's Vulkan driver lacks `VK_EXT_headless_surface`), **0
   FAILED.** This exactly matches PHASE1's own established 1857 baseline plus
   PHASE2's own +25 `PluginRenderPassBuilderAdapterV3ValidationTest` cases
   (1882 total) — re-confirmed unchanged across PHASE2/PHASE4's own fast,
   no-filter test-binary runs — proving the full clean rebuild introduced
   ZERO new regressions and ZERO count drift versus every prior phase's own
   incremental-build test count.
5. **Live, HTTP-driven end-to-end smoke test** — `run_app_background` on the
   freshly-built `build\GreatTamanaEditor.exe` (PID 3068):
   - `GET /get_logs?limit=500` — confirms ALL 9 real, permanent plugin `.dll`s
     load successfully and simultaneously in one process: `DemoEditorPanelPlugin`,
     `HelloWorldPlugin`, `DemoRenderFeaturePlugin`/`...Second` (`_v1`),
     `DemoRenderFeatureV2Plugin`/`...V2SecondPlugin` (`_v2`), and
     `DemoRenderFeatureV3Plugin`/`...V3SecondPlugin`/`...V3ThirdPlugin` (`_v3`)
     — BOTH `_v2` and `_v3` demo plugins loaded and running at once, exactly
     as this phase's own file requires. Only pre-existing, already-documented
     benign warnings appear: the shared-CRT-risk warning (PHASE1 of
     `editor-core-separation-3`), the `_v1` multi-plugin "last write wins"
     warning, and the same-priority lexical-tie-break warnings for the
     `_v2`/`_v3` demo pairs (both already documented in
     `PHASE2_COMPLETION_REPORT.md`).
   - `GET /get_logs?min_level=Error&limit=200` — **`"count":0`** throughout.
   - `GET /get_logs?category=RenderFeatureCompositor.Blackboard&limit=50` —
     the exact same 2-entry publish/fetch round-trip PHASE4 first
     demonstrated (`"Published key 'DemoV3.BlurStrength'..."` /
     `"Fetch succeeded..."`, both `frame:1`, both `f=0.500000`) — the
     blackboard mechanism is real and still working after a genuine clean
     rebuild.
   - `GET /get_logs?category=PluginRenderPassBuilderAdapter_v3&limit=50` —
     **`"count":0`** — zero adapter-side rejections of any kind.
   - `GET /get_logs?keyword=Fetch%20failed` — **`"count":0`**.
   - `GET /get_logs?keyword=DemoRenderFeatureV3` — confirms every `_v3` demo
     plugin's own real passes actually ran this frame:
     `DemoRenderFeatureV3_Game_Blend`, `DemoRenderFeatureV3_Scene_Blend`,
     `DemoRenderFeatureV3Second_Vignette`/`_Game_Blend`,
     `DemoRenderFeatureV3Third_Fill`/`_Grade`/`_Game_Blend` — every one of the
     "could not be assigned a GPU-timing slot" lines is the SAME pre-existing,
     already-documented 16-slot GPU-timing-slot-budget-exhaustion condition
     the `render-pass-6` campaign's own `AGENTS.md` entry already explains —
     not a new warning class.
   - `GET /activate_tab?name=Game` then `GET /get_game_view` — **HTTP 200**,
     a real 186756-byte PNG (a blue-centered radial gradient fading to a
     pale cream at the corners — the real, composited result of `_v1`'s
     magenta-clear feeding into `_v2`'s red-fill+blue-vignette, further
     composited on top by `_v3`'s own blur/fill/grade/vignette-widening
     passes, all seven render-feature plugins active simultaneously,
     exactly the maximum real stress this repository can produce, mirroring
     PHASE2's own "all 7 render-feature plugins loaded and enabled
     simultaneously" stress precedent).
   - `GET /render_graph` (both while "Scene" and after activating "Game")
     structurally confirmed every real internal pass (`RenderOpaque`,
     `DrawSkyBackground`, `RenderTransparent`, all 4 Atmosphere LUT/volume
     passes, `AtmosphereAerialPerspectiveCompositePass`) with correct
     `reads`/`writes`/`render_pass_event` — real `rg::PassRecord`s
     indistinguishable from any internal pass, confirming Step 2.7's
     "already generically visible" claim held after the full clean rebuild
     too.
   - `stop_app_background(pid: 3068)` — clean shutdown.

## Test-count delta vs. `AGENTS.md`'s most recent prior campaign baseline

`AGENTS.md`'s own `editor-core-separation-1` paragraph cites 1773 tests as of
that campaign's own closeout; `render-pass-7`'s own paragraph (a later
campaign, chronologically) cites 1753 (a DIFFERENT, smaller baseline —
`render-pass-7` finished before `editor-core-separation-1`'s own final count,
confirming these are two independent lineages of the same overall test suite,
not a strict monotonic sequence one can just "take the biggest of"). This
campaign (`editor-core-separation-9`) is the most recent chronologically and
its own PHASE1 completion report already established the correct, real,
current-at-the-time baseline directly from a fresh, no-filter
`GreatTamanaEngineTests.exe` run: **1857** (before this campaign's own PHASE1
work), growing to **1882** by PHASE2 (+25 new
`PluginRenderPassBuilderAdapterV3ValidationTest` cases), confirmed unchanged
through PHASE3/PHASE4, and now RE-CONFIRMED via the full, mandatory `ctest`
pass this phase: **1882 tests, 100% of executed tests passing, 2 legitimate
pre-existing skips, 0 failures, 0 count drift.**

## Deviations from the plan

None beyond the one already-disclosed `ask_questions` outcome above (the
human was unavailable; this agent made the documented, precedent-mirroring
choice itself, per the tool's own "proceed as best you can" guidance). No
regression was found by the full `ctest` pass, so Workflow Rule 7's
`delegate_task` exception was never triggered.

## `ask_questions` rounds this phase

One round, described above (clean rebuild vs. incremental) — timed out with
no human response; resolved autonomously via direct precedent-matching
(`editor-core-separation-1` PHASE19) plus a real, verified fact
(`BUILDING.md`'s own documented no-network-on-reconfigure guarantee) that made
the "genuine clean rebuild" choice low-risk and consistent with every prior
campaign's own closeout discipline.

## Verification summary

- Full clean build (`rmdir /s /q build` + fresh `cmake -S . -B build -G
  Ninja` + `cmake --build build`): **557/557 steps, zero errors.**
- Full `ctest -C Debug --output-on-failure`: **1882 total, 1880 passed (100%
  of executed), 2 legitimate skips, 0 failed.**
- Live, HTTP-driven smoke test with BOTH `_v2` and `_v3` demo plugins loaded
  and rendering simultaneously — see the numbered list above. Zero
  unexpected warnings/errors across the entire session.
- `git_status` immediately before this phase's own final commit: only the
  files this phase's own plan named were touched (`AGENTS.md`,
  `docs/conventions/plugin-architecture.md`,
  `plugins/gte_plugin_abi/PublicSurface.md`, plus this same folder's two new
  reports) — no unrelated file touched, no source code changed (no
  regression was found, so no `delegate_task` fix was ever needed).

## Next phase

None — this is the final phase of the `editor-core-separation-9` campaign.
See `CAMPAIGN_COMPLETION_REPORT.md` (this same folder) for the full
five-phase writeup.
