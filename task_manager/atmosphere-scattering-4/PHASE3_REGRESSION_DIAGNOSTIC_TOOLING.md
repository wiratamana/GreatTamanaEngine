# PHASE3 — Permanent Regression Diagnostic: "Validate Aerial Perspective Sky Purity"

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first)
**Depends on:** `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md` (this phase
reuses its oracle function) and
`PHASE2_SHADER_PASSTHROUGH_FIX.md` (the fix this phase's tool is meant to
catch a future regression of)
**Touches shaders?** No. Editor-only C++.

---

## Step 1 — The Goal

Turn the bug report's own manual §5 verification step 3 ("toggle
`aerialPerspectiveStrength` between 0.0 and 1.0 — the sky must look 100%
identical at both settings") into a **permanent, automated, code-based Editor
diagnostic** — so this exact bug class (something double-compositing an
atmosphere effect onto a background/no-geometry pixel) is caught by a single
button click from now on, never again only by a human eyeballing a
screenshot.

Ship a new file pair,
`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`, following
the exact same proven shape as `AtmosphereTransmittanceLutValidation.h/.cpp`
and `AtmosphereAerialPerspectiveLutInspection.h/.cpp` (Editor-only, Tier-2,
`Renderer::CaptureImagePixels()`-based, no `gte::rg::RenderGraph` pass added,
graceful `succeeded=false` failure — never a crash/assert), wired into the
Editor's "Atmosphere" panel as a new "Validate Aerial Perspective Sky Purity"
button.

**What it proves, precisely:** for every pixel where the pre-composite
render target's own depth buffer reports "no opaque geometry was drawn here"
(exactly Phase 1's `ShouldBypassAerialPerspectiveComposite()` predicate), the
POST-composite output texture's color must be identical (within an 8-bit
quantization tolerance) to the PRE-composite color at that same pixel. A
non-zero mismatch count means the composite pass is, once again, touching
pixels it must never touch — the literal symptom this whole campaign fixes.

---

## Step 2 — The Situation

### 2.1 The two textures this tool compares already exist and are already
generically capturable — no new pass, no new render-graph wiring

`Application::Run()` (`src/Application/Application.cpp`) already declares,
every frame, via `RenderGraphBuilder::ImportTexture()`/the Aerial Perspective
Composite pass:

- `"GameView"` / `"SceneView"` — the PRE-composite color+depth render target
  (real opaque geometry + the Sky Background pass's own output — see line
  ~454-457/506-507 for the exact `ImportTexture()` call sites).
- `"GameViewComposited"` / `"SceneViewComposited"` — the POST-composite
  output (`AddAtmosphereCompositePass()`'s own return value — see line
  ~469-475/525-531).

Both are automatically registered in `RenderGraphDebugTextureRegistry` with
**zero opt-in** (per `AGENTS.md`'s "Named Texture Capture" section — every
texture any pass declares becomes capturable by name automatically), and are
already exactly what `GET /get_texture`/`GET /list_textures` expose over
HTTP. This phase's tool reuses the exact same
`rg::RenderGraph::DebugTextureSnapshotFor(name)` primitive
`Application::Run()`'s own `GET /get_texture` handler uses (see
`Application.cpp` around line 842 — `if (const std::optional<rg::DebugTextureSnapshot>
snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName))`), just
called directly from Editor code instead of through the HTTP layer.

### 2.2 `DebugTextureSnapshot`'s shape (already read — `RenderGraphDebugTextureRegistry.h`)

```cpp
struct DebugTextureSnapshot {
    std::string name;
    ExecuteTimingMode regime{};
    RenderTarget target;   // target.image/imageView/extent/format = COLOR; target.depthImage/depthImageView/depthFormat = optional DEPTH
    bool hasDepth = false;
    ResourceState colorState;
    ResourceState depthState;
    std::uint64_t lastUpdatedFrameCounter = 0;
};
```

`"GameView"`/`"SceneView"` are registered with `hasDepth == true` (their own
`RenderTarget` carries a real depth buffer — the same one the Aerial
Perspective composite shader's `sourceDepth` binding reads).
`"GameViewComposited"`/`"SceneViewComposited"` have `hasDepth == false` (the
composite pass's `destinationImage` output has no depth attachment at all) —
this tool only ever needs the PRE-composite snapshot's depth half.

### 2.3 `Renderer::CaptureImagePixels()` is the one, already-proven, generic
readback primitive to reuse (see `Renderer.h` around line 300-332)

```cpp
CapturedRawPixels CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format,
    VkExtent2D extent, const rg::ResourceState& previousState, int bytesPerPixel = 4) const;
```

Exactly the same primitive `GET /get_texture`'s handler and
`AtmosphereTransmittanceLutValidation.cpp`'s `ValidateAtmosphereTransmittanceLut()`
already use. **Do not call `Renderer::WaitForGpuIdle()` anywhere in this new
tool.** `Renderer.h`'s own doc comment on that method, and `AGENTS.md`'s
"Named Texture Capture" section, are explicit: *"No other endpoint, and no
per-frame engine code anywhere else, may ever call `Renderer::WaitForGpuIdle()`"*
— it is a deliberate, narrow exception reserved for `GET /get_texture` alone.
This new tool instead follows `AtmosphereTransmittanceLutValidation.cpp`'s own
precedent exactly (no `WaitForGpuIdle()` call at all — it relies on the same
documented assumption that an Editor UI button click is always processed
BEFORE this iteration's own new GPU submission, so the texture is still in
whatever `ResourceState` the PREVIOUS, already-GPU-completed frame's graph
execution left it in — see that file's own header comment for the full
reasoning, which applies identically here since `"GameView"`/
`"GameViewComposited"` are both produced by the same
`SynchronousImmediateReadback` offscreen regime that file's own target
already used).

### 2.4 Depth decoding has no shared helper to call directly — write a small,
local, non-shared decode function (mirroring an existing precedent)

`src/Encoding/DepthVisualization.h`'s `ConvertDepthToGrayscaleRgba8()` decodes
a captured depth buffer into an 8-bit-quantized grayscale visualization —
useful for a PNG, but too lossy right at the `>= 0.999999` threshold this
tool needs exact precision around. Instead, write a small, **local** (not
shared/exported), per-texel float decoder in this phase's own new `.cpp`,
mirroring `ConvertDepthToGrayscaleRgba8()`'s own per-format branches exactly
(same three supported formats: `VK_FORMAT_D32_SFLOAT`,
`VK_FORMAT_D32_SFLOAT_S8_UINT`, `VK_FORMAT_D24_UNORM_S8_UINT`) but returning
the raw `[0,1]` float instead of a clamped byte — the same "a local copy of
decode logic that must match a real shader/format exactly, deliberately NOT
folded into a shared, more general-purpose utility" precedent
`AtmosphereTransmittanceLutValidation.cpp`'s own
`RayIntersectsSphereNearestForValidation()` already establishes.

### 2.5 Reuse Phase 1's oracle for the actual "is this a sky pixel" decision

Once a pixel's depth is decoded to a `float` in `[0,1]`, call Phase 1's own
`gte::ShouldBypassAerialPerspectiveComposite(depthFloat)` — **do not
reimplement the `>= 0.999999` comparison a second time in this new file.**
This is the whole point of Phase 1 existing first: one single, tested
definition of "this is a sky pixel," reused by both the shader (via Phase
2's mirrored GLSL) and this diagnostic.

---

## Step 3 — The Plan

**Verified against real source on 2026-09-11:** every field name, function signature, and namespace claim in this Step 3 was re-read directly against the CURRENT `src/Renderer/RenderTarget.h`, `src/Renderer/Renderer.h`, `src/Renderer/RenderGraph/RenderGraph.h`, `src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h`, `src/Editor/Panels/RenderGraphPanel.h`, `src/Editor/ImGuiEditorLayer.cpp`, `src/Editor/Panels/AtmospherePanel.h`/`.cpp`, and `src/Encoding/DepthVisualization.h`/`.cpp`. TWO real mistakes were found and are corrected inline below (each marked **CORRECTED 2026-09-11**):
1. Every `rg::RenderGraph& renderGraph` in this phase's own sketches was WRONG — the real, already-in-scope parameter (`ImGuiEditorLayer.cpp`'s `BuildUI()`, confirmed at its base declaration in `src/Editor/EditorLayer.h`'s `IEditorLayer::BuildUI()` too) is `const rg::RenderGraph& renderGraph`. `RenderGraph::DebugTextureSnapshotFor()` is itself a `const` method, so this needs no logic change — only every `rg::RenderGraph&` occurrence below (the new header's function signature, the new `.cpp`'s function signature, and `AtmospherePanel`'s proposed signature extension) must read `const rg::RenderGraph&` instead.
2. There is NO `src/Editor/ImGuiEditorLayer.h` file at all — the entire `ImGuiEditorLayer` class (every member, including the existing `m_lastAerialPerspectiveLutInspection` this phase mirrors) is defined entirely inside `ImGuiEditorLayer.cpp` alone, in an anonymous namespace. Step 3.4's instruction below is corrected to say `ImGuiEditorLayer.cpp` only.

Everything else this phase assumed — `RenderTarget`'s exact field names, `CapturedRawPixels`'s exact shape, `CaptureImagePixels()`'s exact signature and its documented 4-bytes/texel guarantee for a DEPTH-aspect-only capture, `DebugTextureSnapshot`'s exact fields, the `RenderGraphPanel.h` forward-declaration idiom for `rg::RenderGraph`, `AtmospherePanel`'s current parameter list/order, and `DepthVisualization`'s three supported depth formats plus their exact bit manipulation — was confirmed to match the real source EXACTLY as written, with no changes needed. (Separately, `PHASE1_COMPOSITE_DECISION_CPU_ORACLE_AND_TESTS.md`'s own `Vec3::operator*(const Vec3&, float)`/`operator+` assumptions were also re-checked against `src/Math/Vec3.h` and are correct as written — no change needed there either.)

### 3.1 Create `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h`

```cpp
#pragma once

// ============================================================================
// atmosphere-scattering-4 campaign, Phase 3 - Aerial Perspective Sky Purity
// Validation Tool.
// ============================================================================
// See task_manager/atmosphere-scattering-4/PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md
// for this file's own design reasoning - directly modeled on
// src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp's shape: Editor-only
// (GTE_ENABLE_EDITOR), self-contained (Renderer::CaptureImagePixels()-based),
// NO gte::rg::RenderGraph PASS added (this tool only ever READS the graph's
// own DebugTextureSnapshot registry, via RenderGraph::DebugTextureSnapshotFor()
// - the exact same primitive GET /get_texture's own handler uses).
//
// WHAT THIS PROVES: for every pixel where the PRE-composite render target's
// own depth says "no opaque geometry was drawn here this frame" (per
// AtmosphereAerialPerspectiveCompositeMath.h's own
// ShouldBypassAerialPerspectiveComposite() - the SAME predicate the composite
// shader itself now uses, see PHASE1/PHASE2), the POST-composite output
// texture's color must be identical (within an 8-bit quantization tolerance)
// to the PRE-composite color at that exact pixel. This is the permanent,
// automated regression guard for
// AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md: a non-zero mismatch
// count means the composite pass is, once again, altering a pixel it must
// never touch.

#include <cstddef>
#include <string>

namespace gte {

class Renderer;

namespace rg {
class RenderGraph;
}

struct AtmosphereAerialPerspectiveSkyPurityResult {
    bool succeeded = false;
    std::string failureReason;

    int width = 0;
    int height = 0;

    // How many pixels this frame's PRE-composite depth buffer reported as
    // "no opaque geometry" (ShouldBypassAerialPerspectiveComposite() ==
    // true) - i.e. how many pixels this check actually covers. A scene that
    // is 100% covered by opaque geometry (no sky visible at all) will
    // legitimately report 0 here - see ToDiagnosticString()'s own handling
    // of that case.
    std::size_t skyPixelCount = 0;

    // Of skyPixelCount above, how many differ between pre- and
    // post-composite by more than `toleranceUnorm8` on at least one RGB
    // channel. MUST be 0 for a passing result.
    std::size_t mismatchingSkyPixelCount = 0;

    double maxSkyPixelChannelDelta = 0.0;  // in [0,1] units, largest single-channel |pre - post| seen across every sky pixel.
    double meanSkyPixelChannelDelta = 0.0; // averaged across every sky pixel (0.0 if skyPixelCount == 0).

    // One 8-bit UNORM quantization step by default (1/255) - both textures
    // are ordinary 8-bit-per-channel color targets, so a byte-identical
    // pass-through can still legitimately differ by up to this much due to
    // two independent UNORM encode/decode round-trips (this render target's
    // own write, then the compute shader's own texture()/imageStore()
    // round-trip) - mirrors AtmosphereTransmittanceLutValidationResult's own
    // "epsilon" reasoning (see that file's own header comment) applied to a
    // pass-through check instead of a physical-formula parity check.
    double toleranceUnorm8 = 1.0 / 255.0;

    // succeeded && mismatchingSkyPixelCount == 0 - the actual pass/fail a
    // caller should branch on (mirrors
    // AtmosphereTransmittanceLutValidationResult's own "succeeded && ...
    // == 0" pattern already used by AtmospherePanel.cpp's "Validate
    // Transmittance LUT" button).
    bool Passed() const noexcept { return succeeded && mismatchingSkyPixelCount == 0; }
};

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveSkyPurityResult& result);

// Compares `preCompositeColorTextureName`'s (e.g. "GameView") own real,
// currently-registered color+depth against
// `compositedColorTextureName`'s (e.g. "GameViewComposited") own real,
// currently-registered color, both read back via
// RenderGraph::DebugTextureSnapshotFor() + Renderer::CaptureImagePixels() -
// see this file's own header comment. Graceful `succeeded=false` (never a
// crash/assert) if either name is not currently registered, the pre-
// composite name has no depth half, or the two textures' extents disagree.
AtmosphereAerialPerspectiveSkyPurityResult ValidateAerialPerspectiveSkyPurity(Renderer& renderer,
    const rg::RenderGraph& renderGraph, const char* preCompositeColorTextureName,  // CORRECTED 2026-09-11: was `rg::RenderGraph&` (non-const) - the real, in-scope caller-side value is `const rg::RenderGraph&` (see ImGuiEditorLayer.cpp's BuildUI() / EditorLayer.h's IEditorLayer::BuildUI()).
    const char* compositedColorTextureName, double toleranceUnorm8 = 1.0 / 255.0);

} // namespace gte
```

**CONFIRMED against real source**: `src/Editor/Panels/RenderGraphPanel.h` uses exactly

```cpp
namespace gte {
struct EditorContext;
namespace rg {
class RenderGraph;
} // namespace rg
...
} // namespace gte
```

— i.e. `namespace gte { ... namespace rg { class RenderGraph; } ... }` (two nested `namespace` blocks, NOT the stray-extra-brace `} } }` shorthand this file's own header comment loosely gestured at) - this matches the sketch above's forward declaration exactly, so the header sketch above needs no structural change here. (`RenderGraphPanel::Build()` itself takes `const rg::RenderGraph& renderGraph` too, by the way - the same const-reference convention this phase's own signature is now corrected to use above.)

### 3.2 Create `src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp`

Structure (follow `AtmosphereTransmittanceLutValidation.cpp`'s own file
layout/style exactly):

```cpp
#include "AtmosphereAerialPerspectiveSkyPurityValidation.h"

#include "../Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gte {

namespace {

// LOCAL copy, deliberately NOT shared with src/Encoding/DepthVisualization.h
// (see PHASE3_REGRESSION_DIAGNOSTIC_TOOLING.md, Step 2.4, for why) - decodes
// one already-captured depth texel's raw 4 bytes into its real [0,1] float
// value, for the exact three depth formats VulkanDevice::PickDepthFormat()
// can ever return.
bool DecodeDepthTexelToUnitFloat(std::uint32_t word, VkFormat depthFormat, float& outDepth) noexcept
{
    if (depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        const std::uint32_t depth24 = word & 0x00FFFFFFu;
        outDepth = static_cast<float>(depth24) / static_cast<float>(0x00FFFFFFu);
        return true;
    }
    if (depthFormat == VK_FORMAT_D32_SFLOAT || depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        std::memcpy(&outDepth, &word, 4);
        return true;
    }
    return false; // Unrecognized format - caller reports a clean failure, never a crash.
}

} // namespace

std::string ToDiagnosticString(const AtmosphereAerialPerspectiveSkyPurityResult& result)
{
    if (!result.succeeded) {
        return "Aerial Perspective Sky Purity Validation: FAILED - " + result.failureReason;
    }
    if (result.skyPixelCount == 0) {
        return "Aerial Perspective Sky Purity Validation: no sky pixels found this frame "
               "(scene is fully covered by opaque geometry) - nothing to check.";
    }

    char buffer[640];
    std::snprintf(buffer, sizeof(buffer),
        "Aerial Perspective Sky Purity Validation: %dx%d, %zu sky pixel(s) checked\n"
        "  mismatching sky pixels: %zu / %zu\n"
        "  max channel delta:      %.6f\n"
        "  mean channel delta:     %.6f\n"
        "  tolerance:              %.6f",
        result.width, result.height, result.skyPixelCount, result.mismatchingSkyPixelCount, result.skyPixelCount,
        result.maxSkyPixelChannelDelta, result.meanSkyPixelChannelDelta, result.toleranceUnorm8);
    return std::string(buffer);
}

AtmosphereAerialPerspectiveSkyPurityResult ValidateAerialPerspectiveSkyPurity(Renderer& renderer,
    const rg::RenderGraph& renderGraph, const char* preCompositeColorTextureName,  // CORRECTED 2026-09-11: was `rg::RenderGraph&` (non-const) - see the matching correction/reasoning in Step 3.1 above. DebugTextureSnapshotFor() is a `const` method, so nothing else in this function's body needs to change.
    const char* compositedColorTextureName, double toleranceUnorm8)
{
    AtmosphereAerialPerspectiveSkyPurityResult result;
    result.toleranceUnorm8 = toleranceUnorm8;

    const std::optional<rg::DebugTextureSnapshot> preSnapshot =
        renderGraph.DebugTextureSnapshotFor(preCompositeColorTextureName);
    if (!preSnapshot.has_value()) {
        result.failureReason =
            std::string("\"") + preCompositeColorTextureName + "\" is not currently registered - render at least one frame first.";
        return result;
    }
    if (!preSnapshot->hasDepth) {
        result.failureReason = std::string("\"") + preCompositeColorTextureName + "\" has no depth half registered.";
        return result;
    }

    const std::optional<rg::DebugTextureSnapshot> postSnapshot =
        renderGraph.DebugTextureSnapshotFor(compositedColorTextureName);
    if (!postSnapshot.has_value()) {
        result.failureReason =
            std::string("\"") + compositedColorTextureName + "\" is not currently registered - render at least one frame first.";
        return result;
    }

    if (preSnapshot->target.extent.width != postSnapshot->target.extent.width
        || preSnapshot->target.extent.height != postSnapshot->target.extent.height) {
        result.failureReason = "Pre-composite and post-composite textures have different extents this frame.";
        return result;
    }

    result.width = static_cast<int>(preSnapshot->target.extent.width);
    result.height = static_cast<int>(preSnapshot->target.extent.height);
    if (result.width <= 0 || result.height <= 0) {
        result.failureReason = "Textures have a zero-sized dimension - nothing to validate.";
        return result;
    }

    const Renderer::CapturedRawPixels depthRaw = renderer.CaptureImagePixels(preSnapshot->target.depthImage,
        VK_IMAGE_ASPECT_DEPTH_BIT, preSnapshot->target.depthFormat, preSnapshot->target.extent,
        preSnapshot->depthState, 4);
    const Renderer::CapturedRawPixels preColorRaw = renderer.CaptureImagePixels(preSnapshot->target.image,
        VK_IMAGE_ASPECT_COLOR_BIT, preSnapshot->target.format, preSnapshot->target.extent, preSnapshot->colorState,
        4);
    const Renderer::CapturedRawPixels postColorRaw = renderer.CaptureImagePixels(postSnapshot->target.image,
        VK_IMAGE_ASPECT_COLOR_BIT, postSnapshot->target.format, postSnapshot->target.extent,
        postSnapshot->colorState, 4);

    std::size_t skyPixelCount = 0;
    std::size_t mismatching = 0;
    double deltaSum = 0.0;
    double maxDelta = 0.0;

    const std::size_t pixelCount = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint32_t depthWord = 0;
        std::memcpy(&depthWord, depthRaw.pixels.data() + i * 4, 4);
        float depthValue = 1.0f;
        if (!DecodeDepthTexelToUnitFloat(depthWord, preSnapshot->target.depthFormat, depthValue)) {
            result.failureReason = "Unrecognized depth format - cannot decode.";
            return result;
        }

        if (!ShouldBypassAerialPerspectiveComposite(depthValue)) {
            continue; // Real opaque geometry - not this tool's concern (Phase 4's own regression tests cover that path).
        }
        ++skyPixelCount;

        const std::uint8_t* preTexel = preColorRaw.pixels.data() + i * 4;
        const std::uint8_t* postTexel = postColorRaw.pixels.data() + i * 4;

        double maxChannelDelta = 0.0;
        for (int channel = 0; channel < 3; ++channel) { // RGB only - alpha is not meaningful for either target.
            const double deltaUnit =
                std::abs(static_cast<double>(preTexel[channel]) - static_cast<double>(postTexel[channel])) / 255.0;
            maxChannelDelta = std::max(maxChannelDelta, deltaUnit);
        }

        deltaSum += maxChannelDelta;
        maxDelta = std::max(maxDelta, maxChannelDelta);
        if (maxChannelDelta > toleranceUnorm8) {
            ++mismatching;
        }
    }

    result.skyPixelCount = skyPixelCount;
    result.mismatchingSkyPixelCount = mismatching;
    result.maxSkyPixelChannelDelta = maxDelta;
    result.meanSkyPixelChannelDelta = (skyPixelCount > 0) ? (deltaSum / static_cast<double>(skyPixelCount)) : 0.0;
    result.succeeded = true;
    return result;
}

} // namespace gte
```

**CONFIRMED against real source (2026-09-11)**: `Renderer::CapturedRawPixels` is exactly `{ std::vector<std::uint8_t> pixels; int width = 0; int height = 0; VkFormat format = VK_FORMAT_UNDEFINED; }` (`Renderer.h` lines 271-276), and `Renderer::CaptureImagePixels()`'s real signature matches this document's sketch exactly: `CapturedRawPixels CaptureImagePixels(VkImage image, VkImageAspectFlags aspect, VkFormat format, VkExtent2D extent, const rg::ResourceState& previousState, int bytesPerPixel = 4) const;` (`Renderer.h` line 331). `RenderTarget`'s real field names also match this document's sketch exactly: `image`/`imageView`/`extent`/`format`/`depthImage`/`depthImageView`/`depthFormat` (plus one extra field this sketch doesn't use, `bool depthHasStencil`, per `src/Renderer/RenderTarget.h`). No changes needed to this sketch.

### 3.3 Register the new files in `CMakeLists.txt`

Add immediately after the existing
`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp` lines (around
line 579-580, inside the `if(GTE_ENABLE_EDITOR)` block):

```cmake
        src/Editor/AtmosphereAerialPerspectiveLutInspection.h
        src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp
        src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h
        src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp
```

(Re-read the file first to confirm the exact current line numbers, since
Phase 1 already added lines above this block.)

### 3.4 Wire the new tool into `AtmospherePanel`

`src/Editor/Panels/AtmospherePanel.h` — extend `BuildAtmospherePanel()`'s
signature with two new parameters: a `const rg::RenderGraph&` (forward-declared,
matching the CONFIRMED `RenderGraphPanel.h` idiom — see 3.1; CORRECTED
2026-09-11 from a non-const `rg::RenderGraph&` — the real caller-side value,
`ImGuiEditorLayer.cpp`'s `BuildUI()` parameter, is itself `const`) and a
`std::optional<AtmosphereAerialPerspectiveSkyPurityResult>&` result-storage
reference, mirroring the existing `lastValidationResult`/
`lastAerialInspectionResult` parameter pattern exactly. CONFIRMED: the current
real signature (`AtmospherePanel.h`) is `BuildAtmospherePanel(EditorContext&
ctx, AtmosphereSettings& settings, Renderer& renderer, AtmosphereLutRenderer&
atmosphereLutRenderer, std::optional<AtmosphereTransmittanceLutValidationResult>&
lastValidationResult, std::optional<AtmosphereAerialPerspectiveLutInspectionResult>&
lastAerialInspectionResult)` — append the two new parameters at the END of this
list, in the same order they are listed above, so this remains additive.

`src/Editor/Panels/AtmospherePanel.cpp` — add, right after the existing
"Inspect Aerial Perspective LUT" block (its own `ImGui::Separator()` +
button + result printout):

```cpp
    // atmosphere-scattering-4 campaign, Phase 3 - the permanent, automated
    // regression guard for AERIAL_PERSPECTIVE_NO_GEOMETRY_BUG_REPORT_20260911.md.
    ImGui::Separator();
    if (ImGui::Button("Validate Aerial Perspective Sky Purity")) {
        lastSkyPurityResult = ValidateAerialPerspectiveSkyPurity(renderer, renderGraph, "GameView", "GameViewComposited");
    }
    if (lastSkyPurityResult.has_value()) {
        const AtmosphereAerialPerspectiveSkyPurityResult& r = *lastSkyPurityResult;
        const bool passed = r.Passed();
        ImGui::TextColored(passed ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f) : ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
            !r.succeeded ? "ERROR" : (passed ? "PASS" : "FAIL"));
        ImGui::TextWrapped("%s", ToDiagnosticString(r).c_str());
    }
```

Add `#include "../AtmosphereAerialPerspectiveSkyPurityValidation.h"` to both
`AtmospherePanel.h`/`.cpp` as needed (mirroring the existing include for
`AtmosphereAerialPerspectiveLutInspection.h`).

`src/Editor/ImGuiEditorLayer.cpp` (CORRECTED 2026-09-11: there is NO
`src/Editor/ImGuiEditorLayer.h` — the entire `ImGuiEditorLayer` class,
including all of its private members, is defined inline inside this one
`.cpp` file, inside an anonymous namespace; edit `ImGuiEditorLayer.cpp` only)
— add a new member, `std::optional<AtmosphereAerialPerspectiveSkyPurityResult>
m_lastAerialPerspectiveSkyPurityResult;`, right next to the existing
`m_lastAerialPerspectiveLutInspection` member (CONFIRMED at line 810), and
update the `BuildAtmospherePanel(...)` call site (CONFIRMED at line 548) to
pass both `renderGraph` (CONFIRMED: this is exactly the real name of the
`const rg::RenderGraph&` parameter already in scope — see `BuildUI(Game& game,
Renderer& renderer, const rg::RenderGraph& renderGraph, ...)` at line 459 —
already passed by that same name to `m_renderGraphPanel.Build(m_ctx,
renderGraph)` at line 540, two lines before the `BuildAtmospherePanel(...)`
call at line 548; note it is `const`, matching the `const rg::RenderGraph&`
correction made to `BuildAtmospherePanel()`'s own signature above) and the new
result-storage member. These exact line numbers (810/548/459/540) were
confirmed correct via a real source read on 2026-09-11 as part of this
phase document's own accuracy pass, but — per this whole campaign's
"re-read before editing" discipline (see `PHASE0_MASTER_STRATEGY.md`'s
own workflow rule 3) — re-read `ImGuiEditorLayer.cpp` fresh immediately
before making this edit anyway, in case Phase 1/2's own commits (or
anything else) shifted them in the meantime; if a line number has drifted,
the real source wins and the deviation should be noted in this phase's own
completion report, not silently assumed away.

### 3.5 Verification (fast compile check only)

1. Fast targeted build of the Editor-enabled configuration (`GTE_ENABLE_EDITOR=ON`,
   the project's default) — confirm zero compile errors.
2. Launch the engine (`run_app_background`), confirm the Editor's
   "Atmosphere" panel now shows a third button, "Validate Aerial Perspective
   Sky Purity".
3. Click it once with the Phase 2 fix in place — confirm the result prints
   `PASS` with `mismatching sky pixels: 0 / <N>` for some `N > 0` (a scene
   with at least some visible sky).
4. As an extra, optional confidence check (not required to pass this phase,
   but strongly recommended): temporarily revert Phase 2's shader fix
   locally, rebuild, click the button again, confirm it now reports `FAIL`
   with a non-zero mismatch count — then re-apply Phase 2's fix and rebuild
   before committing. This is the closest thing to an automated end-to-end
   regression test this Tier-2, GPU-dependent tool can get, and is worth
   doing once to build confidence in the tool itself, even though it will
   not be repeated automatically by any CI in this repository (no headless
   GPU test infrastructure exists yet — see `TESTING.md`).
5. `stop_app_background` the running instance.
6. Write `PHASE3_COMPLETION_REPORT.md` describing the final file contents,
   the compile check, and the button's actual observed output (paste the
   diagnostic string), then `git_add` + `git_commit`.
