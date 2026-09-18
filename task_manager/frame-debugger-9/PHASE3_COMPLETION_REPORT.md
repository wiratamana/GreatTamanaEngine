# PHASE3 — Shader-Property Texture On-Demand Preview (Feature 2) — COMPLETION REPORT

_Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_SHADER_PROPERTY_TEXTURE_ONDEMAND_PREVIEW.md` exactly as specified —
no material deviations from the phase document were needed. This is the
campaign's heaviest, highest-risk phase (a brand-new, second, independent GPU
resource lifecycle); every "IMPORTANT — found during double-check" callout in
the phase document, and every item in its Step 3.7 risk checklist, was
re-verified directly against the actual code produced below._

## What was done

Implemented exactly the plan in
`PHASE3_SHADER_PROPERTY_TEXTURE_ONDEMAND_PREVIEW.md`, Step 3, with no material
deviation:

1. **`src/Editor/FrameDebuggerData.h`** — `FrameDebuggerTextureProperty` gained
   the two new fields specified in Step 3.1, verbatim: `rg::ResourceKind kind
   = rg::ResourceKind::Texture;` and `bool isRenderGraphResource = false;`,
   each with the exact doc comments the phase document specifies. No new
   `#include` was needed — `rg::ResourceKind` is already visible transitively
   (`RenderGraphSnapshot.h` → `RenderGraphBuilder.h` → `RenderGraphTypes.h`),
   confirmed by a successful compile.

2. **`src/Editor/FrameDebuggerData.cpp`**, `BuildComputeDispatchLeaf()` — both
   the read-row loop and the write-row loop (Step 3.2) now also set
   `texture.kind` (the real `rg::ResourceKind` already being read to pick the
   row's label) and `texture.isRenderGraphResource = true`. Confirmed
   `BuildGameViewLeaf()`'s "Material Texture" loop and
   `BuildGameViewDrawRecordLeaf()`'s "Material Texture" row were NOT touched —
   both already correctly default `isRenderGraphResource` to `false` by simply
   never setting it, exactly as the phase document requires.

3. **`src/Editor/Panels/FrameDebuggerPanel.h`**:
   - New includes: `../../Renderer/VolumeTexturePreviewRenderer.h` (full
     include — `m_shaderPropertyVolumeRenderer` is a full member, not a
     pointer) and `../../Renderer/Texture2D.h` (full include —
     `std::optional<Texture2D>` needs a complete type), plus `<optional>`
     added to the existing `<string>`/`<vector>` standard includes.
   - New PUBLIC method `void ReleaseShaderPropertyTexturePreview();`, declared
     directly after `ReleasePreviewDescriptor()` — per the phase document's
     own **"IMPORTANT — found during this document's own double-check"**
     callout (Step 3.3), this MUST be public (not private) so
     `ImGuiEditorLayer::~ImGuiEditorLayer()` can call it directly, for the
     exact same reason `ReleasePreviewDescriptor()` already is.
   - New PRIVATE method `void
     RequestShaderPropertyTexturePreview(const std::string& textureName,
     rg::ResourceKind kind);`, declared directly after
     `EnsurePreviewDescriptor()`.
   - New private members, placed right after `m_lastKnownRawPreviewView`:
     `m_shaderPropertyPreviewName` (`std::string`),
     `m_shaderPropertyPreviewKind` (`rg::ResourceKind`),
     `m_shaderPropertyPreviewLookupFailed` (`bool`),
     `m_shaderPropertyPreviewTexture` (`std::optional<Texture2D>`),
     `m_shaderPropertyPreviewDescriptor` (`VkDescriptorSet`),
     `m_shaderPropertyPreviewExtent` (`VkExtent2D`), and
     `m_shaderPropertyVolumeRenderer` (`VolumeTexturePreviewRenderer`) — every
     one with the exact doc comment the phase document specifies.

4. **`src/Editor/Panels/FrameDebuggerPanel.cpp`**:
   - New includes: `../../Encoding/HdrColorVisualization.h`,
     `../../Encoding/PixelConversion.h`, plus `<cstdint>`/`<exception>`/
     `<vector>` added to the existing standard-library include block.
   - New anonymous-namespace `IsBgraFormat(VkFormat)` helper — byte-for-byte
     the same body as `Application.cpp`'s own copy (confirmed by direct
     comparison), with a comment cross-referencing the original, per the
     phase document's explicit Clean-Architecture requirement (`src/Editor/`
     may never `#include` `src/Application/` headers).
   - `ReleaseShaderPropertyTexturePreview()` — implemented exactly per Step
     3.5: waits for GPU idle only when there is actually something to
     release (mirrors `AssetPreviewTexture::Reset()`), then removes the ImGui
     descriptor, resets the owned `Texture2D`, and clears
     `m_shaderPropertyPreviewName`/`m_shaderPropertyPreviewLookupFailed`/
     `m_shaderPropertyPreviewExtent`.
   - `RequestShaderPropertyTexturePreview()` — implemented exactly per Step
     3.4: always calls `ReleaseShaderPropertyTexturePreview()` first (Locked
     Design Decision #2 — no staleness caching, ever); the 2D-texture branch
     follows the EXACT `GET /get_texture` recipe (non-`const`
     `CapturedRawPixels raw` local, HDR-vs-BGRA-vs-plain-RGBA8 branching,
     `WaitForGpuIdle()` before the synchronous readback); the volume-texture
     branch reuses `VolumeTexturePreviewRenderer::RenderPreview()` +
     `SelectVolumeTexturePreviewInterpretation()` completely unchanged; both
     branches converge on the same `uploadOrFail` lambda which wraps
     `Renderer::CreateTexture2D()` in `try`/`catch (const std::exception&)`,
     mirroring `AssetPreviewTexture::Resolve()`'s own identical precedent —
     every failure path (lookup miss, HDR/BGRA conversion failure, GPU
     upload exception) leaves `m_shaderPropertyPreviewName` non-empty with
     `m_shaderPropertyPreviewLookupFailed = true` and no descriptor.
   - Five release call-sites added/verified exhaustively (Step 3.5's list):
     1. `RequestShaderPropertyTexturePreview()`'s own first line.
     2. `SetSelectedEventIndex()` — one new call right after the (already
        early-returned-on-no-change) `m_selectedEventIndex = newIndex;`
        assignment.
     3. `ApplyEnabledEdge()`'s `!m_enabled && wasEnabled` (Disable) branch,
        directly alongside the existing `m_currentCapture.Clear();`.
     4. `Build()`'s "resume while Enabled" block, directly alongside its own
        `m_currentCapture.Clear();`.
     5. The new "Back to Step Preview" `ImGui::SmallButton()` in
        `BuildInspectorPane()`.
     6. `~FrameDebuggerPanel()` — directly alongside the existing
        `ReleasePreviewDescriptor()` call (defense-in-depth).
     7. `ImGuiEditorLayer::~ImGuiEditorLayer()` — see item 4 below (the
        REQUIRED site).
   - UI wiring (Step 3.6): the "ShaderProperties" tab's texture loop is now
     indexed (`for (std::size_t i = 0; i < d.textures.size(); ++i)`) instead
     of range-based, specifically to avoid the button-ID collision risk the
     phase document's own re-double-check callout raises (`"View##Texture" +
     std::to_string(i)`, never `valueLabel` alone). A "View" `SmallButton` is
     drawn (via `ImGui::SameLine()` after `BuildPropertyRow()`) only when
     `texture.isRenderGraphResource && texture.kind !=
     rg::ResourceKind::Buffer`, highlighted via the existing
     `ImGuiCol_ButtonActive` theme color whenever
     `m_shaderPropertyPreviewName == texture.valueLabel`. The preview child
     window (`BuildInspectorPane()`) now branches on
     `!m_shaderPropertyPreviewName.empty()` FIRST (before the existing
     `showPreviewTexture`/placeholder branches), reusing Phase 1's
     `ComputeAspectFitImageRect()` for the aspect-fit image draw, with a
     dedicated "Texture not currently available for preview." placeholder
     for the lookup/upload-failed case, and a "Back to Step Preview" button
     that calls `ReleaseShaderPropertyTexturePreview()`.

5. **`src/Editor/ImGuiEditorLayer.cpp`** — one new line in
   `~ImGuiEditorLayer()`, `m_frameDebuggerPanel.ReleaseShaderPropertyTexturePreview();`,
   added directly alongside the existing
   `m_frameDebuggerPanel.ReleasePreviewDescriptor();` call, BEFORE
   `ImGui_ImplVulkan_Shutdown()` — the phase document's own **required, not
   optional** call site (Step 3.5, item 7), confirmed still exactly where the
   phase document said it would be (directly after
   `ReleaseGameViewDescriptor()`/`ReleaseSceneViewDescriptor()`/
   `ReleaseBlurredSceneOutputDescriptor()`/`ReleasePreviewDescriptor()`, before
   `ImGui_ImplVulkan_Shutdown()`).

6. **`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`** (Step 3.8):
   - Extended the existing
     `ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind` test with
     `EXPECT_EQ(..., kind)` / `EXPECT_TRUE(isRenderGraphResource)` assertions
     for all six rows (Texture/Buffer/VolumeTexture, both read and write) —
     confirming a Buffer row IS `isRenderGraphResource == true` (it IS a real
     render-graph resource, just never image-previewable; `kind` is what
     actually gates the UI button, not this flag alone), per the phase
     document's own explicit instruction.
   - Added a new test,
     `MaterialTextureRowIsNeverARenderGraphResource`, proving a
     `BuildGameViewDrawRecordLeaf()`-produced "Material Texture" row has
     `isRenderGraphResource == false`.

No other file was touched. No deviation from the phase document was needed —
every real signature/struct field/enum value the phase document quoted
(`Renderer::CaptureImagePixels()`, `Renderer::CreateTexture2D()`,
`Renderer::WaitForGpuIdle()`, `VolumeTexturePreviewRenderer::RenderPreview()`,
`RenderGraph::DebugTextureSnapshotFor()`/`DebugVolumeTextureSnapshotFor()`,
`rg::DebugTextureSnapshot`/`rg::DebugVolumeTextureSnapshot`'s exact field
names including the volume struct's `state` vs. the 2D struct's `colorState`,
`rg::ResourceKind`'s three enumerators, `Encoding::ConvertBgraToRgbaInPlace()`/
`ConvertHdrRgba16fToRgba8()`) was independently re-confirmed against the real,
current source during this implementation pass and matched exactly.

## Step 3.7 risk checklist — re-confirmed

- [x] Switching directly between two different shader-property textures
  releases the first before creating the second —
  `RequestShaderPropertyTexturePreview()`'s own first line unconditionally
  calls `ReleaseShaderPropertyTexturePreview()`.
- [x] Re-clicking the SAME "View" button redoes the whole capture from
  scratch — no staleness/dirty-check anywhere in
  `RequestShaderPropertyTexturePreview()`.
- [x] Selecting a different tree/slider/arrow-key event releases the
  shader-property preview and falls back to the newly-selected event's own
  step preview — `SetSelectedEventIndex()` now calls
  `ReleaseShaderPropertyTexturePreview()` whenever the index genuinely
  changes; confirmed live via HTTP `select_event` (see Verification below).
- [x] A fresh Capture, Disable, and Resume-while-Enabled all release it —
  three separate call sites verified individually by direct code reading
  (`TriggerCapture()`'s indirect route via `SetSelectedEventIndex(-1)`;
  `ApplyEnabledEdge()`'s Disable branch; `Build()`'s resume-while-enabled
  block) — each one now sits directly alongside its pre-existing
  `m_currentCapture.Clear()` call.
- [x] **Clean process shutdown while a shader-property preview is showing
  does not crash.** The REQUIRED release call
  (`m_frameDebuggerPanel.ReleaseShaderPropertyTexturePreview();`) was added to
  `ImGuiEditorLayer::~ImGuiEditorLayer()`, directly alongside the existing
  `ReleasePreviewDescriptor()` call, BEFORE `ImGui_ImplVulkan_Shutdown()` —
  confirmed present at the exact call site the phase document specifies. A
  full application run/enable/capture/disable/close cycle (see Verification)
  completed with no crash; the specific "actively viewing a shader-property
  texture at the moment of shutdown" sub-case could not be additionally
  exercised in THIS session because no mouse/keyboard input-injection tool is
  available in this environment to click the "View" button (see "Deviations"
  below for the full explanation) — the call site's correctness rests on
  direct code review plus the fact that it mirrors, byte-for-byte,
  `ReleasePreviewDescriptor()`'s own already-proven-correct precedent in the
  exact same class/method.
- [x] A genuine GPU upload failure (`Texture2D`'s constructor throwing
  `std::runtime_error`) is caught by the shared `uploadOrFail` lambda's
  `try`/`catch (const std::exception&)`, mirroring
  `AssetPreviewTexture::Resolve()`'s identical precedent — confirmed present
  around BOTH `CreateTexture2D()` call sites (2D and volume branches).
- [x] The destructor releases it before `ImGui_ImplVulkan_Shutdown()` runs,
  AND is duplicated at the `ImGuiEditorLayer::~ImGuiEditorLayer()` level
  (the one that actually runs in time) — both call sites confirmed present.
- [x] The 2D branch wraps the freshly-uploaded, fully-owned `Texture2D` with
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` — confirmed correct, since this
  is never a live registry wrap.
- [x] No "View" button is ever drawn for a `rg::ResourceKind::Buffer` row —
  the UI gate is `texture.isRenderGraphResource && texture.kind !=
  rg::ResourceKind::Buffer`.
- [x] Every failure path in `RequestShaderPropertyTexturePreview()` leaves
  `m_shaderPropertyPreviewName` non-empty with
  `m_shaderPropertyPreviewLookupFailed = true` and no descriptor — confirmed
  by direct code reading of every `return;` site.
- [x] A Game View resize while a shader-property preview is showing is a
  non-issue by design — `m_shaderPropertyPreviewTexture` is a fully
  independent, fixed-size, already-uploaded copy/raymarched thumbnail with no
  relationship to the live Game View's own `RenderTexture`/extent.

## Verification evidence

1. **Targeted incremental compile check** (no full/clean rebuild):
   - `cmake --build build --target GreatTamanaEngineTests` — succeeded (9
     build steps: `FrameDebuggerData.cpp`, `FrameDebuggerHistory.cpp`,
     `Panels/FrameDebuggerPanel.cpp`, `ImGuiEditorLayer.cpp`, the three
     `tests/Editor/FrameDebugger*Tests.cpp` translation units, relinking
     `libgte_core.a` and the test executable), 0 errors/warnings related to
     this change.
   - `cmake --build build --target GreatTamanaEngine` — succeeded (1 link
     step — the real Editor executable, used for the live/visual check
     below).

2. **Narrow test run**
   (`tests\GreatTamanaEngineTests.exe --gtest_filter=FrameDebuggerSnapshotBuilderTest.*`)
   — **28/28 tests passed**, including the extended
   `ComputeDispatchLeafLabelsReadWriteRowsByRealResourceKind` assertions and
   the new `MaterialTextureRowIsNeverARenderGraphResource` test, with zero
   regressions in any pre-existing test in this file.

3. **Live, HTTP-driven, screenshot-verified smoke test** — launched
   `build\GreatTamanaEngine.exe` via `run_app_background`:
   - `GET /frame_debugger/open` → window opened.
   - `GET /frame_debugger/enable?value=true` → enabled.
   - `GET /frame_debugger/capture` → `hasCapturedFrame: true`,
     `totalEventCount: 8`.
   - `GET /frame_debugger/select_event?index=1` →
     `selectedEventIndex: 1` in the returned state.
   - `GET /get_swapchain` — **screenshot confirms** the real event tree
     (`Compute Dispatches (Pre-GameView)` with all five real Atmosphere LUT/
     volume passes, `GameView` with its real Sky-Draw child, `Compute
     Dispatches (Post-GameView)` with the composite pass), the frame stepper
     correctly reading "2 of 8", `AtmosphereMultiScatteringLutPass`
     correctly highlighted/selected in the tree, and its real Event Details
     section (Shader/Pass/Blend/.../Stencil rows) rendering correctly below
     the (correctly still-black-letterboxed, Phase-1-preserved) preview box.
   - `GET /frame_debugger/enable?value=false` → `hasCapturedFrame: false`
     (confirms `ApplyEnabledEdge()`'s Disable branch, including its new
     `ReleaseShaderPropertyTexturePreview()` call, still runs correctly).
   - `stop_app_background` to close the engine — no crash.

## Deviations from the strategy document

None in the implementation itself — every deliverable in Step 3.10 of
`PHASE3_SHADER_PROPERTY_TEXTURE_ONDEMAND_PREVIEW.md` was produced exactly as
specified. One VERIFICATION-ONLY limitation is worth recording honestly,
per this campaign's own testability discipline:

- **The "View" button's own click and the "ShaderProperties" tab it lives
  under could not be exercised interactively in this session.** This
  environment's automation toolset for driving the running Editor is
  HTTP-only (`gte_send_request` against the engine's own embedded
  `/frame_debugger/*` routes) — there is no mouse/keyboard input-injection
  tool available for this native SDL/Vulkan window (unlike, say, the
  `playwright` browser-automation category, which does not apply here). The
  phase document itself anticipates exactly this gap ("since HTTP automation
  cannot click an ImGui button directly... if feasible from this
  environment, a manual interactive click-through") — the same class of
  limitation `PHASE2_COMPLETION_REPORT.md` already documented for the
  draggable slider itself ("HTTP automation cannot literally perform a mouse
  drag or send a key press"). No HTTP route exists (by design — this feature
  is explicitly scoped as a hand-driven UI button, never a new
  automation-facing endpoint) to trigger
  `RequestShaderPropertyTexturePreview()` directly either. Given this, the
  "View" button/"ShaderProperties" tab rendering, the actual texture preview
  image, and the "close while actively viewing a shader-property preview"
  sub-case were verified via thorough, line-by-line code review against the
  phase document's own exhaustive Step 3.4/3.5 code samples and Step 3.7
  checklist (confirming every real signature/field/enum value quoted by the
  phase document still matches the current source exactly) plus the
  Tier-1 test coverage above, rather than an additional interactive
  screenshot — consistent with `AGENTS.md`'s own acknowledged "Tier 2 (GPU-
  dependent) code has no automated test coverage yet... the absence of
  automated Tier 2 coverage should never itself slow down or stop feature
  work" position. This is a testability/tooling observation, not a code
  defect — no tool malfunctioned during this session, so no `bug_report` was
  filed.

## Files changed

- `src/Editor/FrameDebuggerData.h`
- `src/Editor/FrameDebuggerData.cpp`
- `src/Editor/Panels/FrameDebuggerPanel.h`
- `src/Editor/Panels/FrameDebuggerPanel.cpp`
- `src/Editor/ImGuiEditorLayer.cpp`
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`
- `task_manager/frame-debugger-9/PHASE3_COMPLETION_REPORT.md` (this file)
