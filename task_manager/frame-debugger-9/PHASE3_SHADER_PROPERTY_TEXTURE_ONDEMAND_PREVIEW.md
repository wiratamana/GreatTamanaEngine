# PHASE3 — Shader-Property Texture On-Demand Preview (Feature 2)

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first in full, especially Locked Design
Decisions #1, #2, #3, #6, #7, #8, #9, #10, every one of which this phase implements.
Depends on Phase 1 (reuses `ComputeAspectFitImageRect()`) and Phase 2 (extends
`SetSelectedEventIndex()`) — implement this phase AFTER both._

**THIS IS THE CAMPAIGN'S HEAVIEST, HIGHEST-RISK PHASE.** It introduces a brand-new,
second, independent GPU resource lifecycle inside a class that already juggles one
(`m_previewDescriptor`). `PHASE0_MASTER_STRATEGY.md`'s own workflow notes call for extra
scrutiny here — the orchestrating process is expected to run a dedicated double-check
pass over THIS document (and, later, this phase's actual implementation) before folding
it into the campaign-wide double-check. Because of that, this document is written to
leave as little ambiguity as possible.

_This document was double-checked against the real, current contents of every file it
references (`FrameDebuggerPanel.h/.cpp`, `FrameDebuggerData.h/.cpp`, `RenderGraph.h`,
`RenderGraphDebugTextureRegistry.h`/`RenderGraphDebugVolumeTextureRegistry.h`,
`VolumeTexturePreviewRenderer.h`, `Renderer.h`/`.cpp`, `Texture2D.h`/`.cpp`,
`AssetPreviewTexture.cpp`, `ImGuiEditorLayer.cpp`, `Application.cpp`'s real `GET
/get_texture` handler) before being finalized. Every real signature/struct field/enum
value quoted below (`Renderer::CaptureImagePixels()`, `Renderer::CreateTexture2D()`,
`VolumeTexturePreviewRenderer::RenderPreview()`, `RenderGraph::DebugTextureSnapshotFor()`/
`DebugVolumeTextureSnapshotFor()`, `rg::ResourceKind`, `Encoding::ConvertBgraToRgbaInPlace()`/
`ConvertHdrRgba16fToRgba8()`) was confirmed to match exactly. Three real correctness gaps
found during that pass are fixed directly in this document's own plan below (see the
"IMPORTANT — found during double-check" callouts in Steps 3.3/3.4/3.5) rather than left
for the implementer to discover the hard way._

## Step 1: The Goal

Every "Read Texture" / "Write Texture" / "Read Volume Texture" / "Write Volume Texture"
row already shown in the "ShaderProperties" tab (e.g. `AtmosphereTransmittanceLut`,
`AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView` from the campaign's own
reference screenshot) gets a small "View" button next to it. Clicking it renders that
EXACT texture's own real current pixels — a 2D LUT texture, or a ray-marched thumbnail
for a 3D volume texture — into the big preview box, replacing the normal step preview
until the user goes back or navigates elsewhere. This is a strictly ON-DEMAND,
ONE-SHOT, non-reactive feature (Locked Design Decision #2) — never a continuously live
view.

## Step 2: The Situation

- `FrameDebuggerTextureProperty` (`FrameDebuggerData.h`) today is just
  `{std::string name; std::string valueLabel;}` — it carries NO information about
  whether a given row is a render-graph-registered 2D texture, a render-graph-
  registered volume texture, a render-graph buffer (never viewable), or a
  non-render-graph "Material Texture" (also never viewable, per Locked Design
  Decision #1). This must be added structurally (never inferred from the row's display
  label string).
- `BuildComputeDispatchLeaf()` (`FrameDebuggerData.cpp`) is the ONLY construction site
  that builds rows from real `rg::ResourceKind`-tagged data
  (`pass.readKinds[i]`/`pass.writeKinds[i]`, already `rg::ResourceKind::Texture` /
  `Buffer` / `VolumeTexture` — confirmed, `RenderGraphTypes.h`) — this is exactly where
  the new fields get populated. `BuildGameViewLeaf()`/`BuildGameViewDrawRecordLeaf()`'s
  "Material Texture" rows must NOT set the new "is a render-graph resource" flag (they
  have no such registry entry to look up at all).
- `RenderGraph::DebugTextureSnapshotFor(name)` / `DebugVolumeTextureSnapshotFor(name)`
  (`src/Renderer/RenderGraph/RenderGraph.h`) are the two live-query primitives — exactly
  what `GET /get_texture`'s handler (`Application.cpp`, ~lines 1081–1236) already calls.
  Confirmed real return shapes: `std::optional<rg::DebugTextureSnapshot>` (fields
  `name`/`regime`/`target` (a `RenderTarget`: `image`/`imageView`/`extent`/`format` +
  optional depth half)/`hasDepth`/`colorState`/`depthState`/`lastUpdatedFrameCounter`)
  and `std::optional<rg::DebugVolumeTextureSnapshot>` (fields `name`/`regime`/`target`
  (a `VolumeTarget`: `image`/`imageView`/`extent` (`VkExtent3D`)/`format`)/`state`/
  `lastUpdatedFrameCounter`) — note the volume struct's single ResourceState field is
  named `state`, NOT `colorState` (that name only exists on the 2D struct). Every field
  used below matches these real names exactly.
  `FrameDebuggerPanel::m_frameRenderGraph` (a raw, non-owning `const rg::RenderGraph*`)
  is already refreshed, unconditionally, at the very top of every single `Build()` call
  (confirmed: `m_frameRenderGraph = &renderGraph;` runs unconditionally, before the
  `if (!ctx.frameDebuggerWindowOpen) return;` early-out and well before `ImGui::Begin()`)
  — so it is always valid and current for the rest of that same `Build()` call,
  including from inside a button's click handler further down the same call stack.
  `m_frameRenderer` (a raw, non-owning `Renderer*`) is set the exact same way, same
  guarantee.
- `Renderer::CaptureImagePixels(image, aspect, format, extent, previousState,
  bytesPerPixel)` (`Renderer.h`, confirmed exact parameter order/types against the real
  header and `Renderer.cpp`'s own implementation) is the generic, synchronous 2D
  readback primitive. `Application.cpp`'s own `GET /get_texture` handler (read in full
  during this campaign's research, and re-confirmed line-for-line during this
  document's own double-check) shows the EXACT recipe to follow for a 2D color texture:
  1. `bytesPerPixel = (format == VK_FORMAT_R16G16B16A16_SFLOAT) ? 8 : 4;`
  2. `Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(snapshot->target.image,
     VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format, snapshot->target.extent,
     snapshot->colorState, bytesPerPixel);` — **declared NON-`const`, deliberately** (see
     Step 3.4's callout below for why this matters here, unlike most other local
     readback variables in this codebase).
  3. If `format == VK_FORMAT_R16G16B16A16_SFLOAT`: convert via
     `Encoding::ConvertHdrRgba16fToRgba8()` (`src/Encoding/HdrColorVisualization.h`,
     confirmed signature `bool ConvertHdrRgba16fToRgba8(const std::uint8_t* rawRgba16f,
     VkFormat colorFormat, int width, int height, std::uint8_t* outRgba8)`) into a
     fresh, separate RGBA8 buffer.
  4. Else if the format is BGRA-ordered (`VK_FORMAT_B8G8R8A8_UNORM` /
     `VK_FORMAT_B8G8R8A8_SRGB`): swizzle in place via
     `Encoding::ConvertBgraToRgbaInPlace()` (`src/Encoding/PixelConversion.h`, confirmed
     signature `void ConvertBgraToRgbaInPlace(std::uint8_t* pixels, int width, int
     height)` — takes a genuinely non-const pointer, not a `const`-then-`const_cast`ed
     one; see Step 3.4).
  5. Otherwise: use `raw.pixels` as-is (already RGBA8-ordered, 4 bytes/pixel).
  `Application.cpp`'s own `IsBgraFormat()` helper is a small, ANONYMOUS-NAMESPACE,
  NOT-exported function (confirmed body: `return format == VK_FORMAT_B8G8R8A8_UNORM ||
  format == VK_FORMAT_B8G8R8A8_SRGB;`) — `src/Editor/` may never `#include`
  `src/Application/` headers (Clean Architecture, `AGENTS.md`) — so this phase adds its
  OWN tiny, byte-for-byte-identical local copy inside `FrameDebuggerPanel.cpp`'s own
  anonymous namespace, with a comment cross-referencing `Application.cpp`'s original
  (this exact "duplicate a tiny helper + comment which original it must be kept in sync
  with" pattern is already an accepted, precedented convention in this exact file family
  — see `FrameDebuggerData.cpp`'s own `kFrameDebuggerGameClearColor` constant).
- `Renderer::CreateTexture2D(pixelsRgba8, width, height, debugName, allowStorageImageAccess
  = false)` (`Renderer.h`, confirmed exact signature) uploads already-RGBA8 CPU pixels
  into a brand-new, owned `Texture2D` — exactly what `src/Editor/AssetPreviewTexture.cpp`'s
  `Resolve()` already does for a decoded PNG/KTX2 asset. This phase reuses the exact same
  call for BOTH the 2D-copy case (step 2 above, after conversion) and the volume-raymarch
  case (below). **IMPORTANT (confirmed by reading `Texture2D.cpp`'s real constructor):**
  this call is NOT exception-free — `vmaCreateImage`/`vkCreateImageView`/`vkCreateSampler`
  failure each throws `std::runtime_error`, a real (if rare) reachable failure path having
  nothing to do with the `allowStorageImageAccess` opt-in this phase never uses. THIS is
  exactly why `AssetPreviewTexture::Resolve()` wraps its own, otherwise-identical
  `CreateTexture2D()` call in a `try`/`catch (const std::exception&)` — this phase's own
  new call site must copy that same discipline (see Step 3.4's callout below); an
  uncaught exception here would unwind straight out of an ImGui button click handler,
  mid-`ImGui::Begin()`/`ImGui::End()` pair, almost certainly crashing the Editor process.
- `VolumeTexturePreviewRenderer::RenderPreview(renderer, volumeTarget, previousState,
  interpretation)` (`src/Renderer/VolumeTexturePreviewRenderer.h`, confirmed exact
  parameter order/types) already exists, already does its own full synchronous raymarch +
  CPU readback, and already returns tightly-packed RGBA8 pixels
  (`VolumeTexturePreviewRenderer::CapturedRawPixels{pixels, width, height}`) — no
  conversion step needed for this branch at all.
  `SelectVolumeTexturePreviewInterpretation(name)` (same header) auto-picks the right
  raymarch interpretation by name, exactly like `GET /get_texture`'s own volume branch
  already does — call it the exact same way.
- `AssetPreviewTexture::Reset()`'s own precedent (`src/Editor/AssetPreviewTexture.cpp`,
  confirmed by reading its real body) is the model for safely releasing a previously-
  uploaded, possibly-still-in-flight `Texture2D`/ImGui descriptor pair on a rare,
  human-driven event: call `vkDeviceWaitIdle()` first (ONLY when there is actually
  something to release), THEN `ImGui_ImplVulkan_RemoveTexture()`, THEN reset the owning
  `Texture2D`. This phase copies that exact discipline for its own new descriptor/texture
  pair.
- **IMPORTANT (confirmed by reading `ImGuiEditorLayer.cpp`'s real destructor and
  `FrameDebuggerPanel.h`'s own doc comment on `ReleasePreviewDescriptor()`):** releasing a
  GPU-descriptor-owning member from `FrameDebuggerPanel`'s OWN destructor is, by itself,
  ALREADY KNOWN TO BE TOO LATE in this exact class. `ImGuiEditorLayer::~ImGuiEditorLayer()`
  explicitly calls `m_frameDebuggerPanel.ReleasePreviewDescriptor();` (a PUBLIC method)
  BEFORE `ImGui_ImplVulkan_Shutdown()` — its own comment there explains why: `m_context`'s
  real ImGui teardown happens inside that destructor's explicit BODY, but
  `m_frameDebuggerPanel` is a member declared (and therefore destroyed, in reverse order)
  AFTER `m_context`, so relying on `~FrameDebuggerPanel()`'s own member-destruction-order
  cleanup alone would call `ImGui_ImplVulkan_RemoveTexture()` on an already-shut-down
  Vulkan/ImGui backend. This EXACT same requirement applies, unchanged, to this phase's
  new `m_shaderPropertyPreviewDescriptor` — see Step 3.3/3.5 below for the fix (this
  phase's original draft plan only added the release call to `~FrameDebuggerPanel()`
  itself, which would have silently reintroduced this exact already-solved bug class for
  a second GPU descriptor in the same file).

## Step 3: The Plan

### 3.1 — `FrameDebuggerTextureProperty` gains two new fields (`FrameDebuggerData.h`)

```cpp
struct FrameDebuggerTextureProperty {
    std::string name;
    std::string valueLabel;

    // task_manager/frame-debugger-9 campaign, PHASE3 - which real kind of
    // render-graph resource `valueLabel` names, IF this row is a render-graph
    // resource at all - see `isRenderGraphResource` below. Meaningless
    // (left at its default) when `isRenderGraphResource` is false. Populated
    // directly from the pass's own real rg::ResourceKind
    // (RenderGraphPassSnapshot::readKinds/writeKinds) at the ONE real
    // construction site that has that data, BuildComputeDispatchLeaf() -
    // never guessed from `name`'s own display-label text (e.g. "Read
    // Texture" vs. "Read Volume Texture"), which would be a fragile,
    // stringly-typed shortcut this codebase's own ReadRowLabelForKind()/
    // WriteRowLabelForKind() precedent deliberately avoids elsewhere too.
    rg::ResourceKind kind = rg::ResourceKind::Texture;

    // task_manager/frame-debugger-9 campaign, PHASE3 - true ONLY for a row
    // built from a real, name-addressable RenderGraphBuilder resource this
    // frame's render graph actually declared (a compute pass's own Read/Write
    // Texture or Read/Write Volume Texture row) - i.e. a row whose
    // `valueLabel` is guaranteed resolvable via
    // RenderGraph::DebugTextureSnapshotFor()/DebugVolumeTextureSnapshotFor().
    // FALSE for a "Material Texture" row (BuildGameViewLeaf()/
    // BuildGameViewDrawRecordLeaf() - an asset-based mesh texture with no
    // render-graph registry entry at all - Locked Design Decision #1,
    // PHASE0_MASTER_STRATEGY.md: explicitly out of scope for the new
    // Panels/FrameDebuggerPanel.cpp "View" button this campaign adds) and for
    // a "Read Buffer"/"Write Buffer" row (kind == rg::ResourceKind::Buffer -
    // never an image at all, Locked Design Decision #8). Defaults to false so
    // every EXISTING construction site this phase does not touch (Material
    // Texture rows) is correct with zero code changes there.
    bool isRenderGraphResource = false;
};
```

`rg::ResourceKind` is already visible in this header transitively (via
`RenderGraphSnapshot.h`, already `#include`d — `RenderGraphPassSnapshot::readKinds` is
already `std::vector<rg::ResourceKind>` used elsewhere in this exact file) — no new
`#include` needed. (Confirmed: `rg::ResourceKind` has exactly three enumerators —
`Texture`, `Buffer`, `VolumeTexture`, `RenderGraphTypes.h` — matching every branch this
phase writes.)

### 3.2 — Populate the new fields (`FrameDebuggerData.cpp`, `BuildComputeDispatchLeaf()`)

```cpp
for (std::size_t i = 0; i < pass.readNames.size(); ++i) {
    FrameDebuggerTextureProperty texture;
    const rg::ResourceKind readKind = i < pass.readKinds.size() ? pass.readKinds[i] : rg::ResourceKind::Texture;
    texture.name = ReadRowLabelForKind(readKind);
    texture.valueLabel = pass.readNames[i];
    texture.kind = readKind;                 // NEW - PHASE3.
    texture.isRenderGraphResource = true;    // NEW - PHASE3.
    details.textures.push_back(std::move(texture));
}
for (std::size_t i = 0; i < pass.writeNames.size(); ++i) {
    FrameDebuggerTextureProperty texture;
    const rg::ResourceKind writeKind = i < pass.writeKinds.size() ? pass.writeKinds[i] : rg::ResourceKind::Texture;
    texture.name = WriteRowLabelForKind(writeKind);
    texture.valueLabel = pass.writeNames[i];
    texture.kind = writeKind;                // NEW - PHASE3.
    texture.isRenderGraphResource = true;     // NEW - PHASE3.
    details.textures.push_back(std::move(texture));
}
```

Do NOT touch `BuildGameViewLeaf()`'s "Material Texture" loop or
`BuildGameViewDrawRecordLeaf()`'s "Material Texture" row — both already correctly
default `isRenderGraphResource` to `false` by simply never setting it.

### 3.3 — New `FrameDebuggerPanel` state (`FrameDebuggerPanel.h`)

New includes at the top: `#include "../../Renderer/VolumeTexturePreviewRenderer.h"`
(for `VolumeTexturePreviewRenderer`, `SelectVolumeTexturePreviewInterpretation` — a full
include is required, not a forward declare, since `m_shaderPropertyVolumeRenderer` below
is a full member, not a pointer) and `#include "../../Renderer/Texture2D.h"` (for the
owned `std::optional<Texture2D>` member — likewise a full include, `std::optional<T>`
needs a complete `T`). Both paths confirmed correct relative to this header's real
location (`src/Editor/Panels/FrameDebuggerPanel.h` → `../../Renderer/...` reaches
`src/Renderer/...`, matching this same file's existing `.cpp`'s own include depth).

New private members (place near the existing `m_previewDescriptor`/
`m_previewProcessor` block, with equally detailed doc comments mirroring this class's
existing density):

```cpp
// task_manager/frame-debugger-9 campaign, PHASE3 - non-empty exactly while the
// Inspector's big preview box is showing a one-shot shader-property texture
// preview INSTEAD of the ordinary step preview (m_previewDescriptor above) -
// the real render-graph resource name currently being shown (matches some
// FrameDebuggerTextureProperty::valueLabel the user clicked "View" on).
// Cleared (and the GPU resources below released) by
// ReleaseShaderPropertyTexturePreview() - see that method's own doc comment
// (PUBLIC section, below) for every site that calls it.
std::string m_shaderPropertyPreviewName;

// PHASE3 - which real resource kind m_shaderPropertyPreviewName was resolved
// as (only meaningful while m_shaderPropertyPreviewName is non-empty) -
// purely informational/for a future UI label; the actual branch already
// happened by the time this is set (see RequestShaderPropertyTexturePreview()).
rg::ResourceKind m_shaderPropertyPreviewKind = rg::ResourceKind::Texture;

// PHASE3 - true if `m_shaderPropertyPreviewName`'s currently-displayed image
// is a resolved snapshot lookup that FAILED (the name no longer resolves to
// anything in either registry - e.g. a stale name from a much older capture -
// OR the GPU upload itself failed, see RequestShaderPropertyTexturePreview()'s
// own try/catch below) - distinguishes "showing nothing because the user
// hasn't clicked View yet" (m_shaderPropertyPreviewName empty) from "showing
// nothing because the lookup/upload genuinely failed" (m_shaderPropertyPreviewName
// non-empty, this true) so BuildInspectorPane() can display an honest, distinct
// placeholder message for the latter rather than silently falling back to the
// step preview.
bool m_shaderPropertyPreviewLookupFailed = false;

// PHASE3 - the ONE owned GPU texture behind m_shaderPropertyPreviewDescriptor
// below, for EITHER the 2D-copy case OR the volume-raymarch case (both
// converge on "own a freshly-uploaded Texture2D" - Locked Design Decisions #9
// and #10, PHASE0_MASTER_STRATEGY.md) - never the render graph's own live,
// pooled/aliased VkImageView directly (a real dangling-reference risk this
// design deliberately avoids - see PHASE0's own Locked Design Decision #9).
std::optional<Texture2D> m_shaderPropertyPreviewTexture;

// PHASE3 - this class's own ImGui descriptor wrapping
// m_shaderPropertyPreviewTexture's view/sampler, exactly mirroring
// m_previewDescriptor's own ownership shape but COMPLETELY INDEPENDENT of it -
// the two are never the same slot, since the user must be able to look at
// either kind of preview without the other's state interfering.
VkDescriptorSet m_shaderPropertyPreviewDescriptor = VK_NULL_HANDLE;

// PHASE3 - the real width/height of whichever texture
// m_shaderPropertyPreviewDescriptor currently wraps - fed into PHASE1's
// ComputeAspectFitImageRect() exactly like snapshot.renderTarget.width/height
// already is for the ordinary step preview.
VkExtent2D m_shaderPropertyPreviewExtent{};

// PHASE3 - the dedicated, on-demand volume raymarch renderer this panel now
// owns (mirrors m_previewProcessor's own "small, dedicated GPU dispatcher this
// panel owns" precedent) - reused, UNCHANGED, from
// src/Renderer/VolumeTexturePreviewRenderer.h (the exact same class GET
// /get_texture's own volume branch already uses). This member's own internal
// GPU resources (pipeline/descriptor set/scratch Texture2D) own no ImGui
// descriptor of their own, so - unlike m_shaderPropertyPreviewDescriptor below -
// its destruction order relative to ImGui_ImplVulkan_Shutdown() does not
// matter; it only needs the GPU to be idle first, already guaranteed by this
// class's own destructor's existing unconditional vkDeviceWaitIdle() call.
VolumeTexturePreviewRenderer m_shaderPropertyVolumeRenderer;
```

New PRIVATE method (declaration in `FrameDebuggerPanel.h`'s private section, near
`EnsurePreviewDescriptor()`):

```cpp
// task_manager/frame-debugger-9 campaign, PHASE3 - the "View" button's own
// click handler (called directly from BuildEventDetailsSection()'s
// ShaderProperties tab loop, THIS SAME Build() call - m_frameRenderer/
// m_frameRenderGraph are already valid non-null pointers by the time any
// button in this window could possibly be clicked, since Build() sets them
// unconditionally at its own top before ever reaching ImGui::Begin()). Always
// releases whatever was previously shown first (Locked Design Decision #2,
// PHASE0_MASTER_STRATEGY.md - "user click it draw preview once... user go
// again draw again" - there is deliberately NO staleness/dirty-check here,
// clicking the SAME row's button again always redoes the whole capture from
// scratch), then performs EXACTLY ONE fresh capture/raymarch + GPU upload for
// `textureName`/`kind`, wiring the result into m_shaderPropertyPreview* above -
// or, on a lookup miss OR a GPU upload failure (see this method's own .cpp
// body), leaves m_shaderPropertyPreviewLookupFailed = true with no descriptor.
// See this method's own .cpp body for the full two-branch (Texture vs.
// VolumeTexture) recipe - never called for `kind == rg::ResourceKind::Buffer`
// (the caller never draws a "View" button for a Buffer row at all - Locked
// Design Decision #8).
void RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind);
```

**IMPORTANT — found during this document's own double-check (do NOT declare this next
method `private`):** `ReleaseShaderPropertyTexturePreview()` must be a **PUBLIC** method,
declared in `FrameDebuggerPanel.h`'s public section directly next to the existing
`ReleasePreviewDescriptor()` (not in the private section alongside
`RequestShaderPropertyTexturePreview()` above). This is not a style preference — it is
required so `ImGuiEditorLayer::~ImGuiEditorLayer()` can call it directly, for the exact
same reason it already calls `m_frameDebuggerPanel.ReleasePreviewDescriptor();` directly
(see Step 2's own callout above, and Step 3.5's call-site list, item 7, below). Add it
right after `ReleasePreviewDescriptor()`'s own declaration:

```cpp
// task_manager/frame-debugger-9 campaign, PHASE3 - releases
// m_shaderPropertyPreviewDescriptor/m_shaderPropertyPreviewTexture (if either
// currently holds anything) and clears m_shaderPropertyPreviewName/
// m_shaderPropertyPreviewLookupFailed - mirrors ReleasePreviewDescriptor()'s
// own shape closely, including AssetPreviewTexture::Reset()'s own "wait for
// the GPU to actually be idle first, but only when there is something to
// wait for" discipline (a rare, explicit, human-driven event - an occasional
// full stall here is an accepted, already-precedented cost, exactly like
// AssetPreviewTexture::Reset()'s own identical tradeoff). PUBLIC (not
// private), for the exact same reason ReleasePreviewDescriptor() immediately
// above is public - see that method's own doc comment: MUST be called
// explicitly by ImGuiEditorLayer's own destructor BEFORE
// ImGui_ImplVulkan_Shutdown() runs, since FrameDebuggerPanel is declared (and
// therefore destroyed, in reverse order) BEFORE ImGuiEditorLayer's own
// explicit destructor BODY (which calls ImGui_ImplVulkan_Shutdown()) even
// starts - relying on THIS class's own destructor alone would run too late.
// Safe to call repeatedly / on an already-empty instance.
//
// Called from: RequestShaderPropertyTexturePreview() itself (always, first,
// before doing new work); SetSelectedEventIndex() (PHASE2) whenever the
// selected event actually changes - "user go elsewhere" (tree click, slider
// drag, arrow-key nudge, or a fresh HTTP select_event); TriggerCapture()
// (indirectly, via its own SetSelectedEventIndex(-1) call - a fresh capture
// always resets selection, which already routes through the same
// chokepoint); ApplyEnabledEdge()'s true->false branch and Build()'s own
// "resume while Enabled" branch (both already call m_currentCapture.Clear() -
// add an explicit direct call here too, since those two sites do NOT go
// through SetSelectedEventIndex()); a new "Back to Step Preview" button in
// BuildInspectorPane() (Step 3.6, below); ~FrameDebuggerPanel() (via
// ReleasePreviewDescriptor()'s own existing sibling call site - add this call
// directly alongside it, after the destructor's own pre-existing
// vkDeviceWaitIdle() call); AND ImGuiEditorLayer::~ImGuiEditorLayer()
// (src/Editor/ImGuiEditorLayer.cpp) - directly alongside its existing
// `m_frameDebuggerPanel.ReleasePreviewDescriptor();` call, BEFORE
// ImGui_ImplVulkan_Shutdown() - see this method's own "why public" note
// above; the ~FrameDebuggerPanel() call site is still ALSO kept (a safe,
// idempotent no-op by the time it runs if ImGuiEditorLayer's destructor
// already released it first) purely for defense-in-depth, mirroring
// ReleasePreviewDescriptor() being called from both of those exact same two
// places today.
void ReleaseShaderPropertyTexturePreview();
```

### 3.4 — `RequestShaderPropertyTexturePreview()` body (`FrameDebuggerPanel.cpp`)

Add a small anonymous-namespace helper at the top of the file, mirroring
`Application.cpp`'s own `IsBgraFormat()` byte-for-byte (confirmed identical to the real
`Application.cpp` body during this document's own double-check):

```cpp
namespace {
// task_manager/frame-debugger-9 campaign, PHASE3 - byte-for-byte duplicate of
// Application.cpp's own anonymous-namespace IsBgraFormat() (src/Editor/ may
// never #include src/Application/ headers - Clean Architecture, AGENTS.md).
// Keep in sync with that copy if it ever changes.
bool IsBgraFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}
} // namespace
```

(`#include <volk.h>` is already present via existing headers; `VkFormat` is already
usable.)

```cpp
void FrameDebuggerPanel::RequestShaderPropertyTexturePreview(const std::string& textureName, rg::ResourceKind kind)
{
    ReleaseShaderPropertyTexturePreview(); // Locked Design Decision #2 - always start fresh.

    if (m_frameRenderer == nullptr || m_frameRenderGraph == nullptr) {
        return; // Defensive only - unreachable in practice, see this method's own header doc comment.
    }

    m_shaderPropertyPreviewKind = kind;

    // task_manager/frame-debugger-9 campaign, PHASE3 - the actual CreateTexture2D()
    // upload (common to both branches below) is wrapped in try/catch: Texture2D's
    // real constructor (Texture2D.cpp) can throw std::runtime_error on a genuine
    // vmaCreateImage/vkCreateImageView/vkCreateSampler failure - a rare but real
    // GPU-resource-exhaustion edge case that has NOTHING to do with the
    // allowStorageImageAccess opt-in this call never uses. This mirrors
    // AssetPreviewTexture::Resolve()'s own identical try/catch around its own,
    // otherwise-identical CreateTexture2D() call - an uncaught exception here
    // would unwind straight out of an ImGui button click handler mid-Begin()/
    // End() pair, almost certainly crashing the Editor process instead of
    // showing the honest "not currently available" placeholder this method
    // already has ready for every OTHER failure mode.
    auto uploadOrFail = [this, &textureName](const void* pixelsRgba8, int width, int height, const char* debugName) -> bool {
        try {
            m_shaderPropertyPreviewTexture.emplace(m_frameRenderer->CreateTexture2D(pixelsRgba8, width, height, debugName));
            return true;
        } catch (const std::exception&) {
            m_shaderPropertyPreviewTexture.reset();
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return false;
        }
    };

    if (kind == rg::ResourceKind::Texture) {
        const std::optional<rg::DebugTextureSnapshot> snapshot = m_frameRenderGraph->DebugTextureSnapshotFor(textureName);
        if (!snapshot.has_value()) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        m_frameRenderer->WaitForGpuIdle(); // Rare, explicit, human-driven - same accepted cost GET /get_texture already pays.

        const bool isHdrColor = (snapshot->target.format == VK_FORMAT_R16G16B16A16_SFLOAT);
        const int bytesPerPixel = isHdrColor ? 8 : 4;
        // NON-const, deliberately (found during this document's own double-check):
        // the BGRA branch below mutates raw.pixels IN PLACE. A `const`-qualified
        // local here would make that mutation only reachable via a const_cast on a
        // truly-const object - technically undefined behavior, and NOT how
        // Application.cpp's own GET /get_texture handler declares its own,
        // otherwise-identical local (`Renderer::CapturedRawPixels raw = ...;`, no
        // `const`) - copy that exactly, not a `const`-qualified variant.
        Renderer::CapturedRawPixels raw = m_frameRenderer->CaptureImagePixels(
            snapshot->target.image, VK_IMAGE_ASPECT_COLOR_BIT, snapshot->target.format,
            snapshot->target.extent, snapshot->colorState, bytesPerPixel);

        std::vector<std::uint8_t> hdrConverted;
        const std::uint8_t* rgba8Pixels = raw.pixels.data();
        bool ok = true;
        if (isHdrColor) {
            hdrConverted.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
            ok = Encoding::ConvertHdrRgba16fToRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, hdrConverted.data());
            rgba8Pixels = hdrConverted.data();
        } else if (IsBgraFormat(raw.format)) {
            // Safe in-place swizzle - CaptureImagePixels() already handed us a
            // buffer we own exclusively, and `raw` is non-const (see above), so
            // no const_cast is needed at all.
            Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
        }

        if (!ok) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        if (!uploadOrFail(rgba8Pixels, raw.width, raw.height, "FrameDebuggerShaderPropertyPreview")) {
            return; // uploadOrFail() already set the lookup-failed state.
        }
        m_shaderPropertyPreviewExtent = snapshot->target.extent;
    } else if (kind == rg::ResourceKind::VolumeTexture) {
        const std::optional<rg::DebugVolumeTextureSnapshot> snapshot =
            m_frameRenderGraph->DebugVolumeTextureSnapshotFor(textureName);
        if (!snapshot.has_value()) {
            m_shaderPropertyPreviewName = textureName;
            m_shaderPropertyPreviewLookupFailed = true;
            return;
        }

        m_frameRenderer->WaitForGpuIdle();

        const VolumeTexturePreviewInterpretation interpretation = SelectVolumeTexturePreviewInterpretation(textureName);
        const VolumeTexturePreviewRenderer::CapturedRawPixels raw =
            m_shaderPropertyVolumeRenderer.RenderPreview(*m_frameRenderer, snapshot->target, snapshot->state, interpretation);

        if (!uploadOrFail(raw.pixels.data(), raw.width, raw.height, "FrameDebuggerShaderPropertyVolumePreview")) {
            return;
        }
        m_shaderPropertyPreviewExtent = VkExtent2D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height) };
    } else {
        return; // rg::ResourceKind::Buffer - never reachable, no "View" button is ever drawn for it (Locked Design Decision #8).
    }

    m_shaderPropertyPreviewDescriptor = ImGui_ImplVulkan_AddTexture(
        m_shaderPropertyPreviewTexture->Sampler(), m_shaderPropertyPreviewTexture->View(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // Correct here (unlike a live registry wrap) - this is a freshly-uploaded, fully-owned Texture2D, always left in this exact layout post-upload.
    m_shaderPropertyPreviewName = textureName;
    m_shaderPropertyPreviewLookupFailed = false;
}
```

(Worth noting for whoever implements this: the `kind` three-way branch above is written
as `if`/`else if`/`else` rather than an exhaustive `switch` with no `default:` - unlike
this same file's `ReadRowLabelForKind()`/`WriteRowLabelForKind()` neighbors, which
deliberately use that stricter form specifically so a future 4th `rg::ResourceKind`
enumerator fails to compile until every call site is revisited. A real `switch` here
would be a small, free consistency improvement - not required for correctness this
phase, since exactly the same three cases are already handled either way, but worth
doing if convenient.)

Required new includes in `FrameDebuggerPanel.cpp`: `#include "../../Encoding/PixelConversion.h"`,
`#include "../../Encoding/HdrColorVisualization.h"`. (`<vector>`/`<optional>` are already
available transitively via `FrameDebuggerData.h`; no new include needed for those.)

### 3.5 — `ReleaseShaderPropertyTexturePreview()` body

```cpp
void FrameDebuggerPanel::ReleaseShaderPropertyTexturePreview()
{
    if ((m_shaderPropertyPreviewTexture.has_value() || m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE)
        && m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device); // Mirrors AssetPreviewTexture::Reset()'s own identical, already-accepted tradeoff.
    }
    if (m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_shaderPropertyPreviewDescriptor);
        m_shaderPropertyPreviewDescriptor = VK_NULL_HANDLE;
    }
    m_shaderPropertyPreviewTexture.reset();
    m_shaderPropertyPreviewName.clear();
    m_shaderPropertyPreviewLookupFailed = false;
    m_shaderPropertyPreviewExtent = VkExtent2D{};
}
```

Call sites to add/verify (be exhaustive — this is the single biggest correctness risk
in this phase per `PHASE0_MASTER_STRATEGY.md`'s own callout):

1. `RequestShaderPropertyTexturePreview()` — already covered above (first line).
2. `SetSelectedEventIndex()` (Phase 2's chokepoint) — Phase 2's real body is an
   early-return guard, `if (newIndex == m_selectedEventIndex) { return; }`, followed
   unconditionally by `m_selectedEventIndex = newIndex;` — add the new call
   immediately after that assignment (i.e. anywhere after the early-return has already
   filtered out the no-change case, which is exactly where Phase 2's own doc comment
   reserves a spot for it — the two are equivalent in effect, just phrase this against
   Phase 2's REAL early-return shape rather than an `if (changed) { ... }` positive
   guard, which is not the shape Phase 2 actually uses).
3. `ApplyEnabledEdge()`'s `else if (!m_enabled && wasEnabled)` branch (Disable) — add the
   call directly alongside the existing `m_currentCapture.Clear();`.
4. `Build()`'s "resume while Enabled" block (`if (m_enabled && m_wasPlaybackPaused &&
   !ctx.playbackPaused)`) — add the call directly alongside the existing
   `m_currentCapture.Clear();`.
5. A new small "Back to Step Preview" button, drawn only while
   `!m_shaderPropertyPreviewName.empty()` (see 3.6 below) — its click handler is exactly
   one call to this method.
6. `~FrameDebuggerPanel()` — add the call directly alongside the existing
   `ReleasePreviewDescriptor()` call, after the destructor's own pre-existing
   `vkDeviceWaitIdle()`. (Kept as defense-in-depth even after item 7 below — a safe,
   idempotent no-op by the time this runs.)
7. **`ImGuiEditorLayer::~ImGuiEditorLayer()` (`src/Editor/ImGuiEditorLayer.cpp`) —
   REQUIRED, not optional.** Add `m_frameDebuggerPanel.ReleaseShaderPropertyTexturePreview();`
   directly alongside the existing `m_frameDebuggerPanel.ReleasePreviewDescriptor();`
   call (confirmed real line: inside `~ImGuiEditorLayer()`, directly after
   `ReleaseGameViewDescriptor()`/`ReleaseSceneViewDescriptor()`/`ReleaseBlurredSceneOutputDescriptor()`
   (the existing `ReleasePreviewDescriptor()` call itself already sits right after those three),
   and before `ImGui_ImplVulkan_Shutdown()`). This is REQUIRED for the
   exact same documented reason that existing call is there at all — see Step 2's own
   callout above and this method's own updated doc comment (Step 3.3): `FrameDebuggerPanel`
   is a member declared, and therefore destroyed, AFTER `m_context`'s real ImGui teardown
   already ran inside `~ImGuiEditorLayer()`'s own explicit body, so item 6 above
   (`~FrameDebuggerPanel()`'s own destructor call) running alone, without this one, would
   call `ImGui_ImplVulkan_RemoveTexture()` on an already-shut-down Vulkan/ImGui backend —
   the exact bug class `ReleasePreviewDescriptor()`'s own existing call site already
   exists specifically to avoid. Also requires making `ReleaseShaderPropertyTexturePreview()`
   PUBLIC (see Step 3.3) so `ImGuiEditorLayer.cpp` can call it at all.

### 3.6 — UI wiring

**(a) The "View" button — `BuildEventDetailsSection()`'s ShaderProperties tab loop:**

**IMPORTANT — found during this document's own re-double-check (button-ID collision
risk):** `d.textures` is one FLAT vector holding BOTH read rows and write rows for the
same pass (see Step 3.2 above — both loops push into the same `details.textures`
vector). A compute pass that reads AND writes a resource under the exact same
registered name (an in-place/accumulation-style dispatch) would therefore produce TWO
rows with an identical `valueLabel` — a `"View##" + texture.valueLabel` button ID would
collide between them (Dear ImGui IDs must be unique within the same window/ID-stack
scope), which at best makes the two buttons alias the same interaction state and at
worst trips an ImGui ID-collision assertion in a debug build. No such pass exists in
this engine TODAY (confirmed by reading every real `AddComputePass()` call site during
this pass), but nothing structurally prevents one being added later, and the fix is
free — index the loop instead of relying on `valueLabel` alone:

```cpp
if (!d.textures.empty()) {
    ImGui::SeparatorText("Textures");
    for (std::size_t i = 0; i < d.textures.size(); ++i) {
        const FrameDebuggerTextureProperty& texture = d.textures[i];
        BuildPropertyRow(texture.name.c_str(), texture.valueLabel);
        // task_manager/frame-debugger-9 campaign, PHASE3 - Locked Design
        // Decisions #1/#3/#8 (PHASE0_MASTER_STRATEGY.md): only a real
        // render-graph 2D/volume texture gets a "View" button - never a
        // Buffer row, never a Material Texture row (isRenderGraphResource is
        // already false for those).
        if (texture.isRenderGraphResource && texture.kind != rg::ResourceKind::Buffer) {
            ImGui::SameLine();
            const bool isCurrentlyViewing = (m_shaderPropertyPreviewName == texture.valueLabel);
            if (isCurrentlyViewing) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            // Indexed by this row's own position (i), NOT just texture.valueLabel -
            // see this section's own "button-ID collision risk" callout immediately
            // above for why a name-only ID would not always be unique.
            const std::string buttonId = "View##Texture" + std::to_string(i);
            if (ImGui::SmallButton(buttonId.c_str())) {
                RequestShaderPropertyTexturePreview(texture.valueLabel, texture.kind);
            }
            if (isCurrentlyViewing) {
                ImGui::PopStyleColor();
            }
        }
    }
}
```

(`BuildPropertyRow()`'s real, current body — confirmed by reading it — ends with a
plain `ImGui::TextUnformatted(value.c_str());` and no trailing `ImGui::SameLine()` of
its own, so the `ImGui::SameLine()` inserted immediately above is exactly what puts the
button on the SAME row as its property text, not the next line.)

**(b) The preview box branch — `BuildInspectorPane()`:**

```cpp
ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255)); // Phase 1's solid-black background - unchanged.
ImGui::BeginChild("FrameDebuggerTexturePreview", ImVec2(0.0f, previewHeight), true);
{
    if (!m_shaderPropertyPreviewName.empty()) {
        // task_manager/frame-debugger-9 campaign, PHASE3 - showing a one-shot
        // shader-property texture preview INSTEAD of the ordinary step
        // preview (Locked Design Decision #2 - never both at once).
        ImGui::TextDisabled("Viewing: %s", m_shaderPropertyPreviewName.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Back to Step Preview")) {
            ReleaseShaderPropertyTexturePreview();
        }
        if (m_shaderPropertyPreviewDescriptor != VK_NULL_HANDLE) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x >= 1.0f && avail.y >= 1.0f) {
                const FrameDebuggerAspectFitRect fit = ComputeAspectFitImageRect(avail.x, avail.y,
                    static_cast<float>(m_shaderPropertyPreviewExtent.width),
                    static_cast<float>(m_shaderPropertyPreviewExtent.height));
                const ImVec2 cursorBase = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(cursorBase.x + fit.offsetX, cursorBase.y + fit.offsetY));
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_shaderPropertyPreviewDescriptor)),
                    ImVec2(fit.width, fit.height));
            }
        } else {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* placeholderText = m_shaderPropertyPreviewLookupFailed
                ? "Texture not currently available for preview."
                : "No Texture";
            const ImVec2 textSize = ImGui::CalcTextSize(placeholderText);
            ImGui::SetCursorPos(ImVec2(
                std::max(0.0f, (avail.x - textSize.x) * 0.5f), std::max(0.0f, (avail.y - textSize.y) * 0.5f)));
            ImGui::TextDisabled("%s", placeholderText);
        }
    } else if (showPreviewTexture) {
        // ... Phase 1's existing step-preview branch, UNCHANGED ...
    } else {
        // ... Phase 1's existing placeholder-text branch, UNCHANGED ...
    }
}
ImGui::EndChild();
ImGui::PopStyleColor();
```

### 3.7 — Exhaustive risk checklist (re-verify every item before declaring this phase done)

- [ ] Switching directly from viewing one shader-property texture to a DIFFERENT one
  (clicking a second "View" button while the first is still showing) correctly releases
  the first's `Texture2D`/descriptor before creating the second's — guaranteed by
  `RequestShaderPropertyTexturePreview()`'s own first line calling
  `ReleaseShaderPropertyTexturePreview()` unconditionally.
- [ ] Re-clicking the SAME "View" button while already viewing that exact texture
  redoes the whole capture from scratch (no staleness caching at all) — matches the
  user's own explicit answer; do not "optimize" this away.
- [ ] Selecting a different tree/slider/arrow-key event while a shader-property preview
  is showing releases it (via `SetSelectedEventIndex()`) and falls back to that NEWLY
  selected event's own ordinary step preview — never leaves a stale texture name
  pointing at data belonging to the previous selection.
- [ ] A fresh Capture, or Disable, or Resume-while-Enabled, all correctly release it too
  (three separate call sites — verify each one individually, not just by inspection of
  one).
- [ ] **The Editor process can be closed cleanly while a shader-property preview is
  showing, with no crash and no validation-layer error.** This is the one gap this
  document's own double-check pass found: releasing `m_shaderPropertyPreviewDescriptor`
  from `~FrameDebuggerPanel()` ALONE (call site 6) is too late —
  `ImGuiEditorLayer::~ImGuiEditorLayer()` must ALSO call the new (now PUBLIC)
  `ReleaseShaderPropertyTexturePreview()` directly, alongside its existing
  `ReleasePreviewDescriptor()` call, BEFORE `ImGui_ImplVulkan_Shutdown()` runs (call site
  7, Step 3.5). Test this explicitly: open the Frame Debugger, Enable, Capture, select a
  compute leaf with a texture row, click "View", then close the whole application window
  — must not crash.
- [ ] **A genuine GPU upload failure (`Texture2D`'s constructor throwing
  `std::runtime_error`) during `RequestShaderPropertyTexturePreview()` is caught, not left
  to unwind out of an ImGui button click handler.** Verify the `try`/`catch` wrapping
  BOTH `CreateTexture2D()` call sites (2D and volume branches, Step 3.4) is actually
  present, mirroring `AssetPreviewTexture::Resolve()`'s own identical precedent.
- [ ] The destructor releases it before `ImGui_ImplVulkan_Shutdown()` runs (same
  ordering requirement `ReleasePreviewDescriptor()` already documents at length) — this
  is why the call is placed directly alongside the existing one, after the destructor's
  own pre-existing `vkDeviceWaitIdle()` — AND (see the item above) is duplicated at the
  `ImGuiEditorLayer::~ImGuiEditorLayer()` level, which is the one that actually runs in
  time.
- [ ] The 2D branch's image layout argument to `ImGui_ImplVulkan_AddTexture()` is
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` — correct BECAUSE this wraps a freshly
  CreateTexture2D()-uploaded, fully independent texture (never the render graph's own
  live view) — re-confirm this reasoning is still true if this recipe is ever
  refactored later; do not silently start wrapping a live `RenderTarget::imageView`
  directly without re-deriving the correct layout from `colorState.layout` instead (see
  `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #9's own reasoning for why this
  matters).
- [ ] No "View" button is ever drawn for a `rg::ResourceKind::Buffer` row.
- [ ] `RequestShaderPropertyTexturePreview()`'s failure paths (`snapshot` lookup miss,
  HDR/format conversion failure, GPU upload exception) all leave
  `m_shaderPropertyPreviewName` non-empty (so "Back to Step Preview" is still reachable)
  with `m_shaderPropertyPreviewLookupFailed = true` and no descriptor — never a silent,
  unexplained blank box.
- [ ] A Game View resize while a shader-property preview is showing is a NON-issue by
  design, not something that needs extra handling: `m_shaderPropertyPreviewTexture` is a
  fully independent, fixed-size, already-uploaded copy (or ray-marched thumbnail) that has
  no relationship whatsoever to the live Game View's own `RenderTexture`/extent — a
  resize neither invalidates it nor needs to refresh it (the whole point of Locked Design
  Decision #2's "frozen until dismissed" one-shot lifecycle). Confirm this reasoning still
  holds if this recipe is ever changed to hold a live reference instead of an owned copy
  (it must not be).

### 3.8 — Tests

- Extend `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`: a compute-dispatch leaf's
  Read/Write Texture and Read/Write Volume Texture rows must have
  `isRenderGraphResource == true` with the correct `kind`; a Read/Write Buffer row must
  have `isRenderGraphResource == true` too (it IS a real render-graph resource, just
  never image-previewable — `kind == rg::ResourceKind::Buffer` is what actually gates
  the UI button, not this flag alone) with `kind == rg::ResourceKind::Buffer`; a
  `GameView`/per-entity "Material Texture" row must have `isRenderGraphResource ==
  false`.
- `RequestShaderPropertyTexturePreview()`/`ReleaseShaderPropertyTexturePreview()`
  themselves stay Tier-2/untested-by-this-codebase's-own-convention (live
  Vulkan/`Renderer`/`RenderGraph` dependency, exactly like `TriggerCapture()` and
  `EnsurePreviewDescriptor()` already are) — proven correct via the manual/live
  verification pass in Phase 4 instead, matching this exact feature's own established
  precedent for GPU-touching methods.

### 3.9 — Verification for this phase

1. Incremental compile check only.
2. Run the extended `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`.
3. STRONGLY encouraged given this phase's risk level: `run_app_background` the Editor,
   drive it via `GET /frame_debugger/open` → `/enable` → `/capture` →
   `/select_event?index=<a Sky-View-LUT compute leaf's index>`, then — since HTTP
   automation cannot click an ImGui button directly — at minimum confirm via a manual
   screenshot (`gte_send_request` against `/get_swapchain`) that the "View" buttons
   render correctly next to the right rows, and, if feasible from this environment, a
   manual interactive click-through to confirm an actual LUT texture (e.g.
   `AtmosphereTransmittanceLut`) renders as a real, non-garbage image, and that clicking
   "Back to Step Preview" correctly restores the previous view. Also perform the clean-
   shutdown-while-viewing check from Step 3.7's own checklist item (close the app while a
   preview is showing; confirm no crash). Stop the app afterward.

### 3.10 — Deliverables

- `src/Editor/FrameDebuggerData.h` / `.cpp` — new `FrameDebuggerTextureProperty` fields
  + their two construction-site updates.
- `src/Editor/Panels/FrameDebuggerPanel.h` — new members/methods (`RequestShaderPropertyTexturePreview()`
  private; `ReleaseShaderPropertyTexturePreview()` PUBLIC, declared next to
  `ReleasePreviewDescriptor()`).
- `src/Editor/Panels/FrameDebuggerPanel.cpp` — new methods, new includes, UI wiring, all
  release call-sites.
- `src/Editor/ImGuiEditorLayer.cpp` — ONE new line in `~ImGuiEditorLayer()`,
  `m_frameDebuggerPanel.ReleaseShaderPropertyTexturePreview();`, directly alongside the
  existing `m_frameDebuggerPanel.ReleasePreviewDescriptor();` call (see Step 3.5, call
  site 7 — REQUIRED, not optional).
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — new assertions.
- `task_manager/frame-debugger-9/PHASE3_COMPLETION_REPORT.md` — written once the above
  compiles and the extended tests pass, explicitly re-confirming every box in the
  Step 3.7 checklist; commit together via `git_add`/`git_commit` on
  `feature/frame-debugger-impl`.
