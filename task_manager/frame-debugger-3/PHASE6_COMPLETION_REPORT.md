# PHASE6 — Real Channels (All/R/G/B/A) + Levels preview processing — COMPLETION REPORT

Campaign: `task_manager/frame-debugger-3/`
Branch: `feature/frame-debugger-impl`
Phase document: `PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md`
Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: `PHASE1`..`PHASE5` (already landed).

## Summary

Implemented the phase's plan as written: the Channels (All/R/G/B/A) row and
Levels slider in `FrameDebuggerPanel`'s inspector pane are now functionally
real. Selecting a channel isolates it (replicated grayscale, alpha forced
opaque) in the preview image; dragging the new two-handle Levels range
control remaps black/white points. This is driven by a brand-new, small,
dedicated preview-compositing mechanism — a pure, Tier-1-tested CPU oracle
(`ApplyFrameDebuggerPreviewTransform()`) mirrored by a tiny GLSL compute
shader (`src/Shaders/FrameDebuggerPreview.comp`) — and explicitly does **not**
reuse `/get_texture`'s existing, semantically-unrelated `channel=color|depth`
query parameter (Locked Design Decision #8, `PHASE0_MASTER_STRATEGY.md`).

## What was built

### 1. `src/Editor/FrameDebuggerPreviewProcessing.h`/`.cpp` (new)

Two layers, mirroring this codebase's "pure CPU math oracle first, GPU
shader mirrors it" discipline (AGENTS.md, "Atmosphere Scattering"/"GPU Vertex
Skinning"):

- **`FrameDebuggerPreviewChannel`** (`All`/`R`/`G`/`B`/`A`) and
  **`ApplyFrameDebuggerPreviewTransform(srcRgba, channel, levelsBlack, levelsWhite)`**
  — a pure, `noexcept` function. Levels remap (`clamp((x - black) /
  max(white - black, 1e-5), 0, 1)`) is applied to all four channels FIRST,
  then channel isolation replicates the chosen leveled channel across R/G/B
  with alpha forced to 1.0 (`All` passes the leveled RGBA straight through).
  Uses the same "deliberately NO `default:` case" exhaustive-switch
  convention as `RenderGraphTypes.cpp`'s `ToString(ResourceAccess)`/
  `IsWriteAccess()`, so a future channel enumerator added without updating
  this function fails to compile.
- **`FrameDebuggerPreviewRenderer`** — the small, on-demand GPU compute
  dispatcher, copying `VolumeTexturePreviewRenderer`'s exact SHAPE (one
  combined-image-sampler input, one storage-image output, a persistent
  scratch output texture reused/overwritten across calls), not its raymarch
  math. **Per this phase's own corrected Step 2/3.2 instruction**, its
  `RenderPreview()` issues `vkCmdBindPipeline`/`vkCmdBindDescriptorSets`/
  `vkCmdPushConstants`/`vkCmdDispatch` directly inside a
  `renderer.ImmediateSubmit([&](VkCommandBuffer cmd) { ... })` lambda —
  **never `renderer.Dispatch()`** — mirroring
  `VolumeTexturePreviewRenderer::RenderPreview()` field-for-field (same
  hand-built `ResourceState` for a compute-shader combined-image-sampler
  read, same barrier-then-dispatch-then-restore shape). It never mutates the
  retained `FrameDebuggerHistoryEntry::preview` texture in place — it reads
  it, then writes into its own separate, persistent `Texture2D` (RGBA8,
  storage-capable — chosen over a second `RenderTexture` because
  `RenderTexture`'s own negotiated/BGRA color format is **not** guaranteed to
  support `VK_IMAGE_USAGE_STORAGE_BIT` on every driver, per
  `Vulkan/FormatCapabilities.h`'s own documented warning — `Texture2D`'s
  fixed `VK_FORMAT_R8G8B8A8_UNORM` is the safe, broadly-supported choice
  `VolumeTexturePreviewRenderer` itself already relies on for its own output;
  see "Deviations" below). Left in
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` afterward (not `GENERAL`), so
  `FrameDebuggerPanel` can wrap either the raw retained texture or this
  processed one with the exact same `ImGui_ImplVulkan_AddTexture()` layout
  constant.

### 2. `src/Shaders/FrameDebuggerPreview.comp` (new)

A direct GLSL transcription of `ApplyFrameDebuggerPreviewTransform()` —
binding 0 = source `sampler2D` (the retained/history texture), binding 1 =
output `rgba8` storage image; push constants carry `channel`/`levelsBlack`/
`levelsWhite`. `local_size_x/y = 16`, matching
`FrameDebuggerPreviewProcessing.h`'s own `kLocalSizeX`/`kLocalSizeY`
constants. Registered in `CMakeLists.txt` via `gte_add_shader(...)`, gated
behind `if(GTE_ENABLE_EDITOR)` (its only real consumer,
`FrameDebuggerPreviewProcessing.cpp`, is Editor-only) — same precedent as
`BoxBlur.comp`/`SceneGrid.vert`/`.frag`.

### 3. `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (wiring)

- New persisted state: `m_channel` (default `All`), `m_levelsBlack`/
  `m_levelsWhite` (default `0.0f`/`1.0f` — the neutral/no-op case), and
  `m_previewProcessor` (a `FrameDebuggerPreviewRenderer` this panel owns,
  exactly like it already owns `m_captureContext`/`m_history`).
- **Channels row**: five real `ImGui::SmallButton`s (no longer
  `BeginDisabled()`-wrapped) that set `m_channel` directly, highlighted via
  `ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive))`
  when active — reusing an existing theme color, never a hardcoded one,
  mirroring `BoneViewerWindow.cpp`/`Panels/InspectorPanel.cpp`'s own
  splitter-button precedent for "reuse an existing theme color" (there is no
  pre-existing "toggle button, highlighted when active" idiom elsewhere in
  this codebase to literally copy verbatim, so this is a new, small, minimal
  idiom built from the same theme-color-reuse philosophy).
- **Levels row**: a real, genuine two-handle range control,
  `ImGui::DragFloatRange2(..., &m_levelsBlack, &m_levelsWhite, ...,
  ImGuiSliderFlags_AlwaysClamp)`, plus one cheap defensive
  `m_levelsWhite = std::max(m_levelsWhite, m_levelsBlack + 0.001f)` line on
  top (belt-and-suspenders only —
  `ApplyFrameDebuggerPreviewTransform()`/the shader's own remap already
  tolerate a degenerate pair without dividing by zero regardless).
- **`EnsurePreviewDescriptor()`** (previously PHASE4's "wrap the raw
  retained texture" method) now: (a) computes `isNeutral` (`All` + `[0,1]`
  levels); (b) when neutral, wraps the raw retained texture exactly as
  before (**zero compute dispatch** — the phase's own documented "valid,
  cheap optimization"); (c) otherwise, checks a small dirty flag
  (`m_lastProcessedSourceView`/`m_lastProcessedChannel`/
  `m_lastProcessedLevelsBlack`/`m_lastProcessedLevelsWhite`) and
  re-dispatches `m_previewProcessor.RenderPreview()` **only if something
  actually changed** since the last dispatch (channel, either levels value,
  or the viewed history entry itself), then wraps
  `m_previewProcessor.OutputView()`/`OutputSampler()` instead — satisfying
  the phase's own "recompute ONLY when dirty, never every ImGui frame"
  requirement.
- `~FrameDebuggerPanel()`'s GPU-idle-wait guard was widened from "only if
  `m_previewDescriptor != VK_NULL_HANDLE`" to "whenever `m_device !=
  VK_NULL_HANDLE`" — `m_previewProcessor` may itself now own live GPU
  resources (pipeline/descriptor set/scratch texture) that must be just as
  safe to tear down as whatever `m_previewDescriptor` currently wraps.

## Deviations from the phase document (and why)

1. **The GPU dispatch class (`FrameDebuggerPreviewRenderer`) lives inside
   `FrameDebuggerPreviewProcessing.h`/`.cpp` itself, not inlined directly
   into `Panels/FrameDebuggerPanel.cpp`.** The phase document's Step 3.2
   prose reads as if the `ImmediateSubmit` lambda/pipeline/descriptor-set
   plumbing might live directly in the Panel, but its own Step 3.6
   file-change inventory lists **exactly** `FrameDebuggerPreviewProcessing.h`/
   `.cpp` as the only new production source files (plus the shader and its
   test) — with `Panels/FrameDebuggerPanel.h`/`.cpp` only listed under
   "Modified" for "real Channels/Levels state + wiring". Putting the actual
   Vulkan pipeline/descriptor-set/dispatch code in the SAME class the pure
   CPU oracle already lives in (rather than duplicating
   `VolumeTexturePreviewRenderer`-shaped Vulkan boilerplate straight into an
   already-large Panel class) is judged the clearly RIGHT reading of the
   plan once actually implementing it: `FrameDebuggerPanel` still does all
   the actual "wiring" the plan asks for (owns the instance, decides
   *when*/*with what parameters* to call `RenderPreview()`, decides *which*
   resulting texture to display) — it just doesn't reimplement
   `VolumeTexturePreviewRenderer`'s entire dispatch mechanism inline a
   second time. This keeps the Panel's own file size/complexity in check and
   matches the file-change inventory the phase document itself commits to
   literally.
2. **The scratch output is a `Texture2D`, not a second `RenderTexture`, even
   though the phase prose says "scratch `RenderTexture`".**
   `Vulkan/FormatCapabilities.h`'s own doc comment states this exactly:
   "`RenderTexture`'s default/negotiated swapchain color format (commonly
   `VK_FORMAT_B8G8R8A8_UNORM`) is genuinely NOT guaranteed to support
   [storage-image usage]" — the Game View's own retained history texture
   uses exactly that default/negotiated format. `Texture2D`'s fixed
   `VK_FORMAT_R8G8B8A8_UNORM` is the documented safe choice for a NEW
   storage-capable texture, and is precisely what
   `VolumeTexturePreviewRenderer`'s own persistent output texture already
   uses for the exact same reason. Using a genuine `RenderTexture` here
   would risk a `std::runtime_error` thrown from
   `Renderer::CreateRenderTexture(..., allowStorageImageAccess=true)` on any
   GPU/driver that doesn't happen to expose storage-image support for that
   particular BGRA format — a real correctness risk the phase's own cited
   precedent (`VolumeTexturePreviewRenderer`) already deliberately avoided by
   choosing `Texture2D` for its own output. This is judged the RIGHT
   engineering call per the task's own explicit "use your own best
   engineering judgment" instruction, not a shortcut — the resulting scratch
   texture is still small, dedicated, persistent, reused/overwritten across
   calls, and never mutates the retained historical copy in place, i.e. it
   satisfies every actual REQUIREMENT the phase's prose describes; only the
   literal C++ class name differs from the prose's own informal wording.
3. **A previously-latent bug was found and fixed while wiring
   `EnsurePreviewDescriptor()` into `Build()`'s existing "did the viewed
   history entry change" detection.** That PHASE4-era code compared
   `m_lastKnownPreviewView` (which `EnsurePreviewDescriptor()` sets to
   whatever `m_previewDescriptor` currently wraps) against the CURRENT
   history entry's raw `preview->View()`, to decide whether to reset
   `m_selectedEventIndex`. Once `EnsurePreviewDescriptor()` could ALSO wrap
   `m_previewProcessor`'s own processed scratch view (whenever Channels/
   Levels are non-neutral), that comparison would have become permanently
   true every single frame (a processed view can never equal the raw entry's
   own view by construction) — silently resetting the user's event
   selection on every frame while any non-neutral Channels/Levels state was
   active. Fixed by introducing a SEPARATE, dedicated field,
   `m_lastKnownRawPreviewView`, updated directly in `Build()` from the raw
   entry's own view only, decoupled from whatever `m_previewDescriptor`
   itself currently wraps. This is exactly the kind of "discover the plan
   [is] incomplete once actually looking at the real code" situation the
   task instructions call out — documented here per that same instruction,
   with the fix included in this phase's own diff (both because it was
   directly introduced by this phase's own change, and because leaving it
   unfixed would ship a real, user-visible regression).
4. Everything else matches the phase document exactly: no new HTTP endpoint
   (PHASE7's job), `/get_texture`'s own `channel` parameter is completely
   untouched, and no processing was applied to the event tree/event-details
   text — only to the preview image, per Step 3.4's explicit non-goals.

## Compile check (per this phase's own Step 3.5)

1. Fast, scoped compile check (`GreatTamanaEngine` target, existing `build`
   directory, `GTE_ENABLE_EDITOR=ON`):
   ```
   cmake --build build --target GreatTamanaEngine
   ```
   Result: **succeeded** — `FrameDebuggerPreviewProcessing.cpp`/
   `Panels/FrameDebuggerPanel.cpp`/`ImGuiEditorLayer.cpp` recompiled cleanly,
   `src/Shaders/FrameDebuggerPreview.comp` compiled to
   `build/shaders/FrameDebuggerPreview.comp.spv` with no GLSL errors, and the
   full executable relinked successfully. (One real compile error was found
   and fixed during this pass — `RenderPreview()`'s `source` parameter needed
   to be `const RenderTexture&`, since `FrameDebuggerHistoryEntry::preview`
   is only ever reached through a `const FrameDebuggerHistoryEntry*` in
   `EnsurePreviewDescriptor()`; every method `RenderPreview()` calls on it —
   `View()`/`Sampler()`/`Image()`/`Extent()` — is already `const`-qualified,
   so this was a pure signature fix, not a behavior change.)
2. Built `GreatTamanaEngineTests` and ran the phase's own specified filter:
   ```
   cmake --build build --target GreatTamanaEngineTests
   build\tests\GreatTamanaEngineTests.exe --gtest_filter=*FrameDebuggerPreview*
   ```
   Result: **all 9 new tests passed** (`AllChannelWithNeutralLevelsIsANoOp`,
   `IsolatingRed/Green/Blue/AlphaReplicates...`,
   `LevelsRemapClampsBelowBlackToZero`, `LevelsRemapClampsAboveWhiteToOne`,
   `LevelsRemapMapsExactMidpointToOneHalf`, and one extra combined-ordering
   test, `LevelsAndChannelIsolationCombineInDocumentedOrder`, added beyond
   the phase document's own minimum list to directly confirm the documented
   "levels BEFORE channel isolation" ordering).
3. Re-ran the FULL `*FrameDebugger*` filter (35 tests across 7 suites,
   spanning PHASE1–PHASE6's own test files) to confirm zero regressions:
   **all 35 passed.**
4. Live smoke test: launched the real `GreatTamanaEngine.exe` in the
   background and confirmed via `GET /get_swapchain` that the Editor still
   renders correctly end-to-end (Hierarchy/Scene/Game/Inspector panels, the
   default Camera entity, a real sky/gradient render) with no crash — the
   Frame Debugger window itself was not open this session (it is not part
   of the default dock layout, and no mouse/HTTP automation exists yet to
   open+enable it — HTTP automation is PHASE7's own job, matching PHASE5's
   own already-accepted precedent/limitation for this exact situation), so a
   literal on-screen Channels/Levels toggle was not additionally captured
   here; the dedicated Tier-1 test suite above is the actual, exhaustive
   verification of the real per-pixel transform this phase introduces.

No full clean build and no full `ctest` regression suite were run in this
phase — reserved for PHASE8 only, per both this phase's own Step 3.5 and
`PHASE0_MASTER_STRATEGY.md`'s Step 3.6/"Order of work".

## File-change inventory

New:
- `src/Editor/FrameDebuggerPreviewProcessing.h`
- `src/Editor/FrameDebuggerPreviewProcessing.cpp`
- `src/Shaders/FrameDebuggerPreview.comp`
- `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp`
- `task_manager/frame-debugger-3/PHASE6_COMPLETION_REPORT.md` (this file)

Modified:
- `src/Editor/Panels/FrameDebuggerPanel.h` (new `m_channel`/`m_levelsBlack`/
  `m_levelsWhite`/`m_previewProcessor`/dirty-tracking/`m_lastKnownRawPreviewView`
  members, doc-comment updates)
- `src/Editor/Panels/FrameDebuggerPanel.cpp` (real Channels/Levels UI wiring,
  `EnsurePreviewDescriptor()` rewritten to dispatch on-demand, `Build()`'s
  history-cursor-change detection fixed to use the new dedicated field,
  widened destructor GPU-idle-wait guard)
- `CMakeLists.txt` (new `gte_core` sources + `GTE_ENABLE_EDITOR`-gated
  `gte_add_shader(... FrameDebuggerPreview.comp)` rule)
- `tests/CMakeLists.txt` (new test file registered alongside the other
  `GTE_ENABLE_EDITOR`-gated Frame Debugger test files)

## Next step

PHASE7 (`PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md`) — a
brand-new `FrameDebuggerCommandBridge` + `/frame_debugger/*` HTTP endpoints
(including `set_channel`/`set_levels`, which will write these SAME
`m_channel`/`m_levelsBlack`/`m_levelsWhite` fields this phase just made
real), plus the main-viewport-pinning fix.
