# PHASE8 — Capability #2: Custom Render Pass + Campaign Closeout — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE8 of 8 (FINAL — campaign closeout)
**Status:** DONE
**Date:** 2026-09-28

---

## What was built

All anchors were re-verified via `search_in_dir`/`read_line` immediately
before editing (LDD6). Every citation in this phase's own file (`Core.h`
private section starting at line 402, `RegisterOffscreenRenderPipelineProviders()`
at line 443, `LoadPlugins()`/`LoadProjectAssemblies()` at lines 307/318,
`RenderPipeline.h`'s `ProviderScope`/`RenderPassProvider`/`RenderPassDesc`/
`RenderPassFrameContext`, `RenderGraphBuilder.h`'s `CreateTexture()`/
`PassBuilder`, `RenderGraphTypes.h`'s `TextureDesc`, `Renderer.h`'s
`CreateComputePipeline()`/`AllocateComputeDescriptorSet()`/
`GetVulkanContextInfo()`/`Dispatch()`/`BeginGraphPassRecording()`,
`ComputePipeline.h`, `ComputeDescriptorSet.h`, `DescriptorSetLayoutBuilder.h`,
`ComputeDispatch.h`'s `ComputeGroupCount()`, `RenderGraph.h`'s `PassContext`/
`resolveTexture()`/`recordDraw`) was re-confirmed correct, byte-for-byte or
line-shifted-but-structurally-identical, against the real, current repository
state — line numbers had shifted slightly versus the phase file's own
citations (e.g. `Core.h`'s `private:` section now opens at line 402, not the
379 the phase file cited), confirming LDD6's own warning was well-founded,
but every cited SHAPE (signature, field name, containing type) was correct.

### STEP 1 — `Core::RegisterProjectRenderPassProvider()` (Finding B)

- `Core.h`: new public method declared immediately after `LoadProjectAssemblies()`
  (PHASE5's own addition), with a doc comment restating why
  `m_offscreenRenderPipeline` (not `m_presentRenderPipeline`) is the correct
  target and why this is safe to call post-construction.
- `Core.cpp`: the one-line forwarding body, immediately after
  `Core::LoadProjectAssemblies()`, mirroring `Core::LoadPlugins()`'s own
  identical "private member, public thin pass-through" shape exactly.
- Incremental compile check (`cmake --build build --target gte_core`)
  succeeded on the first attempt, zero errors.

### STEP 2 — the real render-graph pass (Findings E/F)

Replaced PHASE3's placeholder `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`
with the real registration code this phase file's own Step 2 snippet
specifies, used close to verbatim, with one necessary fix (see "Real
deviation" below):

- `RegisterProbeGame(gte::Core&)` calls the new
  `core.RegisterProjectRenderPassProvider("ProjectAssemblyProbe.FillTexture", rg::ProviderScope::Once, ...)`.
- The provider lambda mints a transient 256x256 `TextureDesc`
  (`VK_FORMAT_UNDEFINED` = match `Renderer::ColorFormat()`) via
  `frame.builder.CreateTexture(...)`, lazily builds a
  `ComputePipeline`/`ComputeDescriptorSet` on first use (process-lifetime
  `std::optional<gte::ComputePipeline>`, `VkDescriptorSetLayout` built via
  `DescriptorSetLayoutBuilder::AddStorageImage(binding=0)`), then constructs
  a deferred `RenderPassDesc` (`PassKind::Compute`) whose `execute` callback
  resolves the current `VkImageView` via `ctx.resolveTexture(outputHandle)`,
  rewrites the descriptor set, and dispatches via
  `renderer.Dispatch(*pipeline, descriptorSet, nullptr, 0, ComputeGroupCount(256,16), ComputeGroupCount(256,16), 1)`.
  `outputHandle` is appended to `frame.finalTextureOutputs` so the pass
  survives `RenderGraphCompiler`'s backward-reachability culling.
- Replaced PHASE4's placeholder no-op `Assets/ProbeCompute.comp`
  (`layout(local_size_x = 1) in; void main() {}`) with a real, minimal
  compute shader: `layout(local_size_x = 16, local_size_y = 16) in;
  layout(binding = 0, rgba8) uniform writeonly image2D uOutput;` that fills
  the bound image with a solid orange color, bounds-checked against
  `imageSize()` — matching the `rgba8` storage-image convention every other
  compute shader in this codebase's `Shaders/*.comp` files already uses.

## Real deviation from this phase file's own literal instructions (found and
fixed, not silently skipped)

**A genuine lambda-capture compile error**, found on the FIRST compile
attempt. The phase file's own Step 2 code sketch captures `outputHandle` in
`desc.execute`'s lambda but reads `texDesc.width`/`texDesc.height` (a local
declared in the OUTER, `Once`-scope provider lambda, never captured into the
inner `execute` lambda) directly inside that inner lambda body — a genuine
compile error (`'texDesc' is not captured`), not a typo introduced during
transcription; re-read the phase file's own snippet and confirmed it has the
identical shape. **Fix**: `desc.execute`'s capture list now captures
`width = texDesc.width, height = texDesc.height` by value (alongside
`&renderer, outputHandle`) instead of relying on `texDesc` still being
reachable — a minimal, mechanical, zero-behavior-change fix. Rebuilt
afterward — succeeded cleanly.

No other deviation of substance. The shader itself is new content this phase
had to author (the phase file's own Step 2 discussion left "what does the
shader actually do" unspecified beyond "fills a transient texture with a
solid color" and pointed at PHASE4's placeholder no-op shader as something
to eventually replace) — `local_size_x/y = 16` was chosen so
`ComputeGroupCount(256, 16) == 16` exactly on both axes, matching the
256x256 `TextureDesc` the C++ side already commits to.

## Live/compile verification performed

1. **Incremental compile check**: `cmake --build build --target gte_core` —
   succeeded cleanly on the first attempt (STEP 1's new method).
2. **Incremental compile check**: `cmake --build build --target ProjectAssemblyProbe_Game` —
   failed once (the lambda-capture bug above), fixed, rebuilt — succeeded
   cleanly; the shader recompiled (`glslc` re-ran) and the staged
   `.spv` correctly re-copied next to `ProjectAssemblyProbe_Game.dll`
   because `HelloGame.cpp` itself changed in the same build (forcing a real
   relink) — sidestepping PHASE4's own documented "shader-only incremental
   rebuild doesn't reliably re-stage" gap, which was never triggered here.
3. **Incremental compile check**: `cmake --build build --target ProjectAssemblyProbe_Editor --target GreatTamanaEditor` —
   both succeeded cleanly, zero new warnings.
4. **Live verification** (`run_app_background` → `build/GreatTamanaEditor.exe`,
   the repo's own default `build/` tree, untouched):
   - `GET /get_logs?category=ProjectAssembly` → exactly 5 entries: both
     `_Editor.dll`/`_Game.dll` registration diagnostics (PHASE5), both
     "Loaded Project Assembly" lines, and **one new line**,
     `"ProbeCompute pipeline/descriptor set built."` — confirming the
     lazy-init `if (!g_probeComputePipeline)` branch ran exactly once, not
     every frame.
   - `GET /get_logs?min_level=Error` → **0 entries** — zero errors anywhere
     in the session.
   - `GET /render_graph` (polled 3 times, several seconds apart) → a real
     `"ProjectAssemblyProbe.FillTexture"` entry present, STABLE, every time,
     inside `offscreen_regime.passes`: `"kind":"Compute"`, `"is_culled":false`,
     a real, non-zero `gpu_timing_milliseconds` (0.01–0.03 ms across polls),
     `"writes":[{"kind":"Texture","name":"ProjectAssemblyProbe.Output"}]`,
     `"reads":[]` — exactly matching what the code declares.
   - `GET /activate_tab?name=Render%20Graph` → success.
   - **Real, visible confirmation in the Editor's own "Render Graph" panel**:
     the panel's "Offscreen Regime" table is a plain ImGui table inside a
     bottom-docked window, and this session's tooling has no HTTP-level way
     to scroll an ImGui panel or resize the docked layout (the exact same
     documented gap `editor-core-separation-10`'s own campaign flagged,
     `AGENTS.md` "ImGui Widget ID Uniqueness" section's own history). Rather
     than accepting this as an unclosed gap, this session used a legitimate,
     non-input-simulating technique instead: a direct Win32
     `MoveWindow()` call (via `run_shell` + PowerShell `Add-Type`/P-Invoke)
     against the real, running `GreatTamanaEditor.exe` window handle
     (resolved via `Get-Process -Id <pid>`'s own `.MainWindowHandle`),
     temporarily resizing the OS window taller (up to 1920x1152, using a
     second monitor's own work area) so the ImGui docking layout naturally
     gave the bottom-docked "Render Graph" panel enough vertical room to
     show its own "Offscreen Regime" table down to the
     `"ProjectAssemblyProbe"` row without any scrolling at all — this is a
     genuine OS-level window resize, not a mouse/keyboard input simulation,
     and the engine's own `SDL_WINDOW_RESIZABLE` flag already supports it.
     `GET /get_swapchain`, captured at 1920x1152, shows the real
     "Offscreen Regime (Game View + Scene View)" table with a row literally
     reading `"ProjectAssemblyProbe"` (column-truncated display name for
     `"ProjectAssemblyProbe.FillTexture"`), `Enabled` checked, `GPU Time`
     `0.03 ms`, `Writes` `ProjectAssemblyProbe.Output` — genuinely,
     mechanically confirmed present in the real panel, not merely inferred
     from the JSON. The window was resized back to its original 1280x720
     afterward via the same mechanism; `imgui.ini`'s own dock-ratio
     percentages shifted as a natural consequence of the temporary resize
     (a cosmetic, machine-local UI-state artifact, never a tracked/committed
     file — confirmed via `git status`).
   - `stop_app_background`.

## Definition of Done — checklist (this phase file's own list)

- [x] `Core::RegisterProjectRenderPassProvider()` exists, is public, forwards
      to `m_offscreenRenderPipeline.Register()` (confirmed correct pipeline —
      restated in both `Core.h`'s own doc comment and this report).
- [x] `ProjectAssemblyProbe_Game.dll` registers a real pass using this new
      method, using genuine `rg::` types directly, with the
      `CreateTexture()`/`finalTextureOutputs` pattern resolved per Finding E.
- [x] The pass is confirmed visible in BOTH the Editor's "Render Graph" panel
      AND `GET /render_graph`'s JSON output — the panel confirmation required
      a real OS-level window resize (see above), not a scroll HTTP endpoint,
      since none exists.
- [x] Completion notes state explicitly that Step 3's on-screen-compositing
      idea was investigated and found NOT safe to implement in this phase (a
      confirmed structural gap in per-handle resource tracking, not merely
      an untried idea) — see `docs/conventions/project-assembly-system.md`'s
      own dedicated section, and it is NOT attempted anywhere in this
      phase's code.
- [x] Full clean build + full `ctest` regression pass, zero unexplained
      regressions versus the most recent prior baseline (`editor-core-separation-10`'s
      1888/100%/2-skips) — see "Campaign closeout" below.
- [x] `AGENTS.md` entry, `docs/conventions/project-assembly-system.md`, and
      `CAMPAIGN_COMPLETION_REPORT.md` all exist and are internally consistent
      with each other and with every PHASE1-7 completion report.

## STEP 5 — Campaign closeout

1. **Reconfigure**: `cmake -S . -B build` — succeeded cleanly (2.6s
   configure, 2.7s generate); the only STDERR output is the pre-existing,
   unrelated `third_party/ktx/cmake/version.cmake` git-describe warning
   ("Error retrieving version from GIT tag... Falling back to 0.0.0-noversion"),
   present on every configure in this repo regardless of this campaign.
2. **Full build, no target filter**: `cmake --build build` — 183/183 steps
   succeeded, zero errors, zero new warnings (mostly `tests/` object files
   that were not yet built in this session — `GreatTamanaEngineTests.exe`
   itself linked cleanly as the final step).
3. **Full `ctest` regression pass**:
   `cd build && ctest -C Debug --output-on-failure` —
   **100% tests passed, 1888/1888**, 173.44 sec total. The exact same 2
   tests reported "Skipped" as every prior campaign's own baseline
   (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
   `CoreHeadlessConstructionTest.ConstructsWithoutCrashingAndExposesUsableAccessorsWithNoEditorLayerHookSet`
   — both legitimate, environment-gated skips, unchanged). **Compared
   directly against `editor-core-separation-10`'s own documented baseline
   (1888 tests, 100% passing, 2 legitimate environment-gated skips) — the
   numbers match EXACTLY.** This campaign added zero new Tier-1 tests (a
   deliberate scope choice — PHASE8's own capability is an integration-level
   proof, verified live via HTTP, not a new pure-logic unit under test), so
   an unchanged total test count with 100% passing is the correct, expected
   outcome, not a red flag.
4. **`AGENTS.md`**: new "## Project Assembly System" section added
   immediately after "## Plugin Architecture" (before "## Testability &
   Regression Safety"), including, verbatim, the permanent toolchain-switch-
   already-done disclosure (PHASE0 §2.2) so no future reader mistakes this
   system for one built against the old `--disable-shared` toolchain.
5. **New file**: `docs/conventions/project-assembly-system.md` — the full
   convention write-up, plus a new entry in `docs/README.md`'s own
   Conventions index (placed after "Plugin Architecture", matching that
   file's existing ordering-by-campaign-recency convention).
6. **`CAMPAIGN_COMPLETION_REPORT.md`** — see that file, this same folder.

## What this phase does NOT do (as instructed, re-confirmed)

- Does NOT modify `plugins/gte_plugin_abi/`'s own `IPluginRenderPassBuilder_v3`
  system, `RenderFeatureCompositor`, or any other ABI-side render machinery
  (LDD1) — confirmed by direct inspection: this phase's only edits are
  `src/Core/Core.h`/`.cpp` (the new public method), `AGENTS.md`,
  `docs/README.md`, the two new `docs/conventions/project-assembly-system.md`/
  `task_manager/.../PHASE8_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md`
  files, and `Projects/ProjectAssemblyProbe/Assets/{HelloGame.cpp,ProbeCompute.comp}`
  (both `.gitignore`d Project Assembly source files, not engine code).
- Does NOT build a curated "operation registry" of any kind — the shader is
  genuinely the project's own, compiled by PHASE4's mechanism, with no
  host-curation step.
- Does NOT attempt real, on-screen Game View compositing for this probe pass
  — confirmed, concretely, to be structurally unsafe as this system exists
  today (see above and `docs/conventions/project-assembly-system.md`), not
  merely out of scope by choice.
- Does NOT resolve every possible future render-graph capability (reading
  `SceneDepth`, multiple render targets, cross-Project-Assembly-pass
  chaining, a safe way to alias an imported handle onto an already-tracked
  physical resource) — this phase proves ONE minimal, concrete, working
  example end-to-end.

## New gap found, worth flagging for future maintenance

None new beyond what PHASE4/PHASE6 already flagged (the shader re-staging
gap, the DLL-lock-on-recompile limitation) — both were re-confirmed still
true and are restated, permanently, in `docs/conventions/project-assembly-system.md`
rather than being rediscovered by a future reader from scratch. One
genuinely NEW observation from this phase's own live verification: no HTTP
mechanism exists to scroll an ImGui panel or resize the docked layout (the
exact same gap `editor-core-separation-10`'s own campaign already flagged,
restated here since this phase hit it directly) — but a direct OS-level
`MoveWindow()` call via `run_shell`/PowerShell is a genuine, real,
non-input-simulating workaround that fully closes this gap for verification
purposes without needing a new engine-side HTTP endpoint. Future phases
verifying anything similarly deep inside a docked panel should reuse this
exact technique (resolve the window handle via
`Get-Process -Id <pid>`'s own `.MainWindowHandle`, then P-Invoke
`user32.dll`'s `MoveWindow`) rather than assuming it is impossible.

## Result

**PHASE8 is DONE — and this closes the entire `editor-core-separation-11`
campaign.** `Core::RegisterProjectRenderPassProvider()` exists, is public,
and correctly forwards onto `m_offscreenRenderPipeline`. A real render-graph
compute pass, contributed entirely from a Project Assembly's own `_Game.dll`
using genuine `rg::` types with zero ABI wrapper, is proven live: visible,
stable, and correctly reporting real GPU timing in both `GET /render_graph`'s
JSON and the Editor's own real "Render Graph" panel (confirmed via a
genuine OS-level window resize, not assumed from the JSON alone). Step 3's
on-screen-compositing question is answered definitively as "confirmed unsafe
today" rather than left open, and is documented as deliberately deferred
future work, not silently dropped. The campaign's own full clean build
(183/183 steps) and full `ctest` regression pass (1888/1888, 100%, 2
unchanged legitimate skips) both succeeded with zero regressions versus the
most recent prior baseline. `AGENTS.md`, `docs/conventions/project-assembly-system.md`,
and `CAMPAIGN_COMPLETION_REPORT.md` are all written and internally
consistent with every PHASE1-7 report. See `CAMPAIGN_COMPLETION_REPORT.md`
in this same folder for the full, 8-phase campaign summary.
