# frame-debugger-9 campaign — PHASE0 MASTER STRATEGY

_Orchestrator document. Every child phase file (`PHASE1_*.md` .. `PHASE4_*.md`) must be
read together with this file. This file is the one source of truth for the campaign's
Goal, Situation, Locked Design Decisions, phase list, and workflow rules — a child phase
document never re-litigates a decision already locked here; it only implements it._

## Step 1: The Goal (Where are we going?)

The Frame Debugger (`docs/conventions/frame-debugger.md`, campaigns `frame-debugger-1`
through `frame-debugger-8`) already works. This campaign is a pure Quality-of-Life
follow-up, adding exactly three user-requested improvements, in the user's own words:

1. **"Make preview aspect ratio respect actual texture aspect ratio instead [of]
   stretching it on preview."** Today `FrameDebuggerPanel::BuildInspectorPane()` draws
   `ImGui::Image(descriptor, avail)` where `avail` is the whole preview child window's
   available size — this STRETCHES a non-square/non-16:9 texture to fill that box,
   distorting it.
2. **"I want to make texture[s used] as shader properties viewable as preview... on
   screenshot, the texture making LUT with compute shader. right now its impossible to
   see the LUT, i want frame debugger [to] have the ability to see the texture that
   act[s] as [a] supporter."** Today the "ShaderProperties" tab already lists every real
   Read/Write Texture name a compute pass touched (e.g. `AtmosphereTransmittanceLut`,
   `AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView` — see the campaign's
   own reference screenshot #2), but that is just TEXT — there is no way to actually SEE
   any of those textures' own pixels anywhere in this window.
3. **"I want the horizontal slider for frame-step [to be] drag[g]able so i can drag an[d]
   see the preview immediately."** Today `FrameDebuggerPanel::BuildFrameStepperRow()`
   wraps its `ImGui::SliderInt()` in `ImGui::BeginDisabled()` — it is a pure, non-
   interactive, cosmetic readout of the current selection; the ONLY way to change the
   selected event is clicking a row in the left-hand tree.

By the end of this campaign, all three items are genuinely implemented, with a
completely dry, incremental-compile-checked-per-phase build, and one final full
build + regression test + live HTTP-driven visual verification pass.

## Step 2: The Situation (Where are we now?)

This section records the exact, already-verified-by-reading-the-real-code facts every
phase below depends on. No phase may re-derive these from scratch or guess at them.

- **The panel**: `src/Editor/Panels/FrameDebuggerPanel.h/.cpp` — a small stateful class,
  `Build()` is the entry point (called once per ImGui frame from
  `ImGuiEditorLayer::BuildUI()`), with `BuildToolbarRow()` / `BuildFrameStepperRow()` /
  `BuildEventTreePane()` / `RenderEventNode()` / `BuildInspectorPane()` /
  `BuildEventDetailsSection()` as its private helpers.
- **The pure data model**: `src/Editor/FrameDebuggerData.h/.cpp` — ImGui-free,
  Tier-1-testable (`tests/Editor/FrameDebuggerDataTests.cpp`,
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`). Owns `FrameDebuggerSnapshot` /
  `FrameDebuggerEventNode` / `FrameDebuggerEventDetails` / `FrameDebuggerTextureProperty`
  / `BuildRealFrameDebuggerSnapshot()` / `ClampSelectedEventIndex()`. This is where every
  new PURE helper this campaign adds must live.
- **The preview mechanism**: `FrameDebuggerCurrentCapture`
  (`src/Editor/FrameDebuggerHistory.h/.cpp`) retains exactly one captured frame's worth
  of GPU textures (`preview` / `compositedPreview` / `perObjectStepPreviews`).
  `FrameDebuggerPanel::EnsurePreviewDescriptor()` picks one of those three via
  `ChooseFrameDebuggerPreviewSource()` and wraps it in `m_previewDescriptor` (an
  `ImGui_ImplVulkan_AddTexture()` descriptor this panel owns and re-wraps only when the
  underlying `VkImageView` actually changes). This whole mechanism (the "step preview")
  is UNTOUCHED in spirit by this campaign — Phase 1 only changes HOW it is drawn
  (aspect-fit instead of stretch); Phase 3 adds a completely separate, second,
  independent preview mechanism alongside it (never replacing it).
- **The render-graph debug texture registries**: `RenderGraph::DebugTextureSnapshotFor()`
  / `ListDebugTextures()` (2D, `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h`)
  and `RenderGraph::DebugVolumeTextureSnapshotFor()` / `ListDebugVolumeTextures()` (3D,
  `RenderGraphDebugVolumeTextureRegistry.h`) are the EXACT same primitives `GET
  /get_texture` already uses (`Application.cpp`, ~line 1085 for 2D, ~line 1169 for
  volume) to serve up ANY named render-graph texture's CURRENT live contents, by name,
  at any point after it was declared via `RenderGraphBuilder::CreateTexture()`/
  `ImportTexture()`/`ImportVolumeTexture()`. Every "Read Texture"/"Write Texture" row a
  compute-dispatch leaf already shows (`BuildComputeDispatchLeaf()`,
  `FrameDebuggerData.cpp`) is one of these exact registered names — this is what makes
  Feature 2 tractable at all: the data is already there, already reachable, just not
  drawn as an image yet.
- **`RenderTarget`** (`src/Renderer/RenderTarget.h`, what `DebugTextureSnapshot::target`
  is) is a bare `{VkImage, VkImageView, VkExtent2D, VkFormat, ...depth half}` — no owned
  `VkSampler`, unlike `RenderTexture`. Any code that wants to sample it directly needs
  its own sampler.
- **`Renderer::CaptureImagePixels()`** (`Renderer.h`) is the generic, synchronous,
  on-demand CPU readback primitive `GET /get_texture`'s 2D branch already uses —
  requires the caller to already know the image's current real `rg::ResourceState`
  (`DebugTextureSnapshot::colorState`), and to already know whether a BGRA swizzle
  (`Encoding::ConvertBgraToRgbaInPlace()`, `src/Encoding/PixelConversion.h`) or an HDR
  conversion (`Encoding::ConvertHdrRgba16fToRgba8()`,
  `src/Encoding/HdrColorVisualization.h`) is needed, based on the real `VkFormat`
  (`Application.cpp`'s own `IsBgraFormat()` — a small, ANONYMOUS-NAMESPACE, NOT exported
  helper, since `src/Editor/` may never `#include` `src/Application/` headers — Clean
  Architecture, see `AGENTS.md`).
- **`VolumeTexturePreviewRenderer`** (`src/Renderer/VolumeTexturePreviewRenderer.h`) is
  the EXISTING, already-shipped (network-impl-6 / frame-debugger-5 campaigns) on-demand
  GPU compute raymarch + CPU readback primitive `GET /get_texture`'s volume branch
  already uses. `SelectVolumeTexturePreviewInterpretation(name)` picks the right
  raymarch interpretation automatically by name (e.g. the Aerial Perspective volume gets
  a different interpretation than a generic density volume) — reused UNCHANGED.
- **`Renderer::CreateTexture2D(pixelsRgba8, width, height, debugName)`** (`Renderer.h`)
  uploads already-decoded RGBA8 CPU pixels into a brand-new, owned GPU `Texture2D` —
  this is the EXACT mechanism `src/Editor/AssetPreviewTexture.cpp`'s `Resolve()` already
  uses to turn a decoded PNG/KTX2 asset into something `ImGui::Image()`-displayable, and
  is the pattern Phase 3 copies for BOTH the 2D-texture-copy case and the
  volume-raymarch case.
- **User-confirmed design answers** (via `ask_questions`, this same session) that lock
  in the exact shape of Feature 2 and refine Features 1/3 — see "Locked Design
  Decisions" below; every one of the five answers is already folded into the decisions
  and into each child phase's own plan. Nothing in this campaign is still an open
  question.

## Step 3: The Plan (how do we get there?)

### Locked Design Decisions (binding on every phase below)

1. **Feature 2 scope**: render-graph-registered textures ONLY — every "Read
   Texture"/"Write Texture"/"Read Volume Texture"/"Write Volume Texture" row a
   compute-dispatch leaf's ShaderProperties tab already shows. "Material Texture" rows
   (`GameView`/per-entity-draw leaves' own asset-based mesh textures — a completely
   different, non-render-graph system, `MaterialTextureGpuCache`) are explicitly OUT OF
   SCOPE this campaign — a clean, documented, deferred future item, per the user's own
   confirmed answer.
2. **Feature 2 lifecycle is manual, ON-DEMAND, ONE-SHOT** — per the user's own words:
   *"texture preview is on demand 1 time draw. user click it draw preview once. user go
   elsewhere it delete. user go again draw again."* Concretely: clicking a "View" button
   next to an eligible texture row triggers exactly ONE fresh GPU readback (2D) or
   raymarch (volume), uploads it into an OWNED `Texture2D`, and displays it — frozen —
   until the user either explicitly dismisses it (a "Back to Step Preview" affordance),
   selects a different tree event/leaf, drags/nudges the frame-step slider to a
   different event, a fresh Capture lands, or Disable/Resume clears the whole capture —
   any of those immediately releases the one-shot preview's GPU resources. There is NO
   continuous/reactive re-rendering while it's displayed, and NO caching/reuse across
   clicks — clicking the SAME button again always redoes the whole capture from
   scratch (deliberately simple, matches the user's own literal description exactly).
3. **Feature 2 UI location**: a small button (e.g. `ImGui::SmallButton("View##<name>")`)
   directly next to each eligible texture row in the existing "ShaderProperties" tab —
   NOT a dropdown/combo — per the user's own confirmed answer.
4. **Feature 1 letterbox/pillarbox color**: solid black bars (classic
   letterbox/video-player look), applied as the WHOLE preview child window's background
   (`ImGuiCol_ChildBg` pushed to opaque black for that one child), uniformly for BOTH
   the step preview (Phase 1) and the new shader-property one-shot preview (Phase 3) —
   per the user's own confirmed answer.
5. **Feature 3 interaction**: both mouse-drag AND Left/Right arrow-key nudge (one event
   at a time, while the slider is focused) — per the user's own confirmed answer
   ("plain mouse-drag and left rig[h]t arrow in-between the slider"). `ImGuiKey_*` +
   `ImGui::IsKeyPressed()` are ALREADY used elsewhere in this codebase
   (`src/Editor/DockLayout.cpp`), confirming this is a safe, available API on the
   Dear ImGui version this project vendors.
6. **One chokepoint for changing the selected event.** A new private method,
   `void FrameDebuggerPanel::SetSelectedEventIndex(int newIndex)`, becomes the ONLY
   place `m_selectedEventIndex` is ever assigned from now on (tree-row click, the new
   slider drag, the new arrow-key nudge, `TriggerCapture()`'s existing reset-to‑`-1`,
   and `SelectEventFromCommand()`'s HTTP path all route through it). It (a) writes the
   new, already-clamped value, and (b) whenever the value actually CHANGES, releases
   any currently-shown Feature-2 one-shot preview (Locked Design Decision 2's "user go
   elsewhere it delete" rule) via a new `ReleaseShaderPropertyTexturePreview()` method
   introduced by Phase 3. Phase 2 introduces this chokepoint (needed for the slider);
   Phase 3 extends it with the one extra release call.
7. **One pure aspect-fit helper, reused twice.** A new pure function,
   `FrameDebuggerAspectFitRect ComputeAspectFitImageRect(float availableWidth, float
   availableHeight, float sourceWidth, float sourceHeight)`, lives in
   `FrameDebuggerData.h/.cpp` (the existing pure/ImGui-free layer), Tier-1-tested. It is
   the ONE place aspect-ratio-fit math is computed — Phase 1 uses it for the step
   preview; Phase 3 reuses the SAME function, unchanged, for the shader-property
   preview. Never duplicated.
8. **Buffer-kind rows never get a "View" button.** A compute pass's "Read Buffer"/"Write
   Buffer" row (e.g. GPU Skinning's output buffer) is not an image and is never
   previewable — enforced structurally via a new `FrameDebuggerTextureProperty::kind`
   field (`rg::ResourceKind`), not by string-matching the row's display label.
9. **The 2D one-shot capture path copies pixels once — it never wraps the registry's
   live `VkImageView` directly.** `RenderGraphResourcePool` may alias/recreate a
   transient texture's physical backing image across frames; holding a raw wrapped
   descriptor to it indefinitely (as LDD2's "stays displayed until dismissed" lifecycle
   requires) would risk a dangling reference. Instead: `Renderer::CaptureImagePixels()`
   → the same BGRA/HDR conversion `GET /get_texture` already performs → a fresh
   `Renderer::CreateTexture2D()` upload → wrap THAT owned texture. Depth-channel
   viewing is out of scope (every targeted row is a color resource).
10. **The volume-texture one-shot path reuses `VolumeTexturePreviewRenderer` /
    `SelectVolumeTexturePreviewInterpretation()` completely unchanged**, uploading its
    returned CPU RGBA8 pixels via the same `Renderer::CreateTexture2D()` call as the 2D
    path — one unified "own an uploaded `Texture2D`, wrap it, done" shape for both
    texture kinds.

### Phase list

| Phase | File | Depends on | Summary |
|---|---|---|---|
| 1 | `PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md` | none | Feature 1 — stop stretching the step-preview image; add the reusable pure aspect-fit helper + solid-black letterbox background. |
| 2 | `PHASE2_DRAGGABLE_FRAME_STEP_SLIDER.md` | none (independent of Phase 1) | Feature 3 — make the frame-step slider a real, draggable + arrow-key-nudgeable control; introduces the `SetSelectedEventIndex()` chokepoint. |
| 3 | `PHASE3_SHADER_PROPERTY_TEXTURE_ONDEMAND_PREVIEW.md` | Phase 1 (reuses `ComputeAspectFitImageRect()`), Phase 2 (extends `SetSelectedEventIndex()`) | Feature 2 — the "View" button + one-shot GPU capture/raymarch/upload/display/release pipeline. This is the CAMPAIGN'S HEAVIEST, HIGHEST-RISK phase (two independent GPU resource lifetimes, image-layout correctness, several release call-sites that must all be exhaustively covered). |
| 4 | `PHASE4_DOCS_TESTS_LIVE_VERIFICATION_AND_FULL_BUILD.md` | 1, 2, 3 | Tier-1 tests for every new pure helper, `docs/conventions/frame-debugger.md` update, full build, full regression test (`ctest`), live HTTP-driven + `gte_send_request` visual verification of all three features together, `CAMPAIGN_COMPLETION_REPORT.md`. |

Phases 1 and 2 are independent of each other and could be implemented in either order,
but this table's ordering (1, then 2, then 3, then 4) is the one every phase document
assumes when it says "already done in an earlier phase" — follow it in order.

### Workflow & rules every phase must follow

1. **No full build, no full regression test, until Phase 4.** Phases 1–3 use only a
   fast, targeted incremental compile check (`cmake --build build --target gte_core`
   or equivalent narrow target — never a full clean rebuild) to confirm the code they
   just touched compiles. Phase 4 is where the full build + full `ctest` run happens.
2. **Incremental build/debug is encouraged mid-phase.** A phase MAY use
   `run_app_background` + `gte_send_request` to visually sanity-check its own work in
   progress if useful — this is not the same thing as the forbidden "full build/full
   regression test", and is explicitly allowed (see each phase's own "Verification"
   section).
3. **Every phase writes its own `PHASEn_COMPLETION_REPORT.md`** in this same folder
   (`task_manager/frame-debugger-9/`) once its own compile check passes, and commits its
   code changes + that report together via `git_add`/`git_commit` on the CURRENT branch
   (`feature/frame-debugger-impl`) — never a new branch.
4. **Every new non-trivial member/function gets a doc comment matching this codebase's
   existing density** (see any file under `src/Editor/FrameDebugger*` for the expected
   style/tone) — this is not optional decoration, it is how this specific codebase's
   own conventions work, and a terser comment style will look out of place and reduce
   future maintainability.
5. **`docs/conventions/frame-debugger.md` and, if genuinely warranted,
   `AGENTS.md`'s "Frame Debugger" paragraph** are updated ONLY in Phase 4, once the
   whole campaign's shape is final — do not touch either file in Phases 1–3.
6. **Tests**: any new PURE function (Phase 1's `ComputeAspectFitImageRect()`, Phase 3's
   new `FrameDebuggerTextureProperty` fields feeding existing snapshot-builder tests)
   gets Tier-1 test coverage added in the SAME phase that introduces it (per
   `AGENTS.md`'s "Testability & Regression Safety" rule: "every change to Tier 1 code
   must come with a matching test change") — do not defer test-writing to Phase 4;
   Phase 4 only RUNS the full suite and does a final live/visual pass.
