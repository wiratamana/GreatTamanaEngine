#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "FrameDebuggerCapture.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshot.h"

// ImGui-free data model of the Frame Debugger window. Every field is display-ready; nothing holds a GPU handle.
namespace gte {

// One texture property row. `valueLabel` is a texture name or "None".
struct FrameDebuggerTextureProperty {
    std::string name;
    std::string valueLabel;

    // Render-graph resource kind named by `valueLabel`. Meaningful only when `isRenderGraphResource` is true.
    rg::ResourceKind kind = rg::ResourceKind::Texture;

    // True for compute Read/Write rows, so `valueLabel` resolves through the graph. False for material slots and buffers.
    bool isRenderGraphResource = false;
};

// One vector property row.
struct FrameDebuggerVectorProperty {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// One 4x4 matrix property row, row-major: values[0..3] is row 0.
struct FrameDebuggerMatrixProperty {
    std::string name;
    std::array<float, 16> values{};
};

// Which retained image shows the Game view as of one event.
enum class FrameDebuggerStepPreviewKind {
    NotYetDrawn,    // Before the view region drew anything: an empty view.
    PerObjectStep,  // One per-entity draw: `stepPreviewIndex` picks the retained image.
    PreComposite,   // The view region itself, or a later pass before the composite pass.
    PostComposite,  // The composite pass, anything after it, or no selection.
};

// Everything the inspector shows for one selected event.
struct FrameDebuggerEventDetails {
    int eventIndex = -1; // Matches FrameDebuggerEventNode::eventIndex.
    std::string shaderName;
    std::string passName;
    std::string blendMode;
    std::string zClip;
    std::string zTest;
    std::string zWrite;
    std::string cull;
    std::string stencilRef;
    std::string stencilComp;
    std::string stencilPass;
    std::string stencilFail;
    std::string stencilZFail;
    std::vector<FrameDebuggerTextureProperty> textures;
    std::vector<FrameDebuggerVectorProperty> vectors;
    std::vector<FrameDebuggerMatrixProperty> matrices;

    // Operation kind shown in the event header, e.g. "Draw Mesh".
    std::string eventLabel;

    // Retained image for this event. `stepPreviewIndex` is a 0-based draw-record index, used by PerObjectStep only.
    FrameDebuggerStepPreviewKind stepPreviewKind = FrameDebuggerStepPreviewKind::PostComposite;
    int stepPreviewIndex = -1;

    // Set for a leaf that writes a pass-private render-graph resource. Empty for the shared view buffer.
    std::optional<std::string> privateWriteTextureName;

    // The pass's own GPU time as text: a measured time, or the status word when it was not measured.
    std::string gpuTimingText;

    // Indirect draws of the pass; never counted as triangles. Zero when the pass has none.
    std::uint32_t indirectDraws = 0;
};

// One row of the event tree. A group has children and no `details`; a draw-call leaf has `details`.
struct FrameDebuggerEventNode {
    std::string name;
    bool isDrawCall = false;
    int eventIndex = -1; // Global 0-based event counter. Meaningful only when isDrawCall is true.
    std::optional<FrameDebuggerEventDetails> details;
    std::vector<FrameDebuggerEventNode> children;
};

// Render target shown above the preview.
struct FrameDebuggerRenderTargetInfo {
    std::string name;
    int width = 0;
    int height = 0;
    std::string format;
};

// Image rectangle relative to the available area's origin.
struct FrameDebuggerAspectFitRect {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Largest centered rect that keeps the source aspect ratio inside the available box.
// Any non-positive input returns a rect that fills the available box at (0, 0).
FrameDebuggerAspectFitRect ComputeAspectFitImageRect(
    float availableWidth, float availableHeight, float sourceWidth, float sourceHeight);

// One captured frame's event tree and render target.
struct FrameDebuggerSnapshot {
    std::vector<FrameDebuggerEventNode> rootNodes;
    int totalEventCount = 0;
    FrameDebuggerRenderTargetInfo renderTarget;
};

// "N of M" label with a 1-based N. Returns "0 of 0" when totalEventCount <= 0.
std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount);

// Clamps `requested` into [0, totalEventCount - 1], or returns -1 when totalEventCount <= 0.
int ClampSelectedEventIndex(int requested, int totalEventCount);

// Finds the draw-call leaf with this eventIndex and returns its details.
// Returns nullopt for a negative index, a missing node, or a node without details.
std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex);

// "(x, y, z, w)" with compact numbers, e.g. "(1, 1, 1, 1)".
std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector);

// Four newline-separated rows of four space-separated compact numbers.
std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix);

// Builds the event tree of one frame from the render-graph snapshot and the per-draw capture.
// Returns an empty snapshot when the graph has no view-region pass. `renderTarget.name` is always "GameView".
// Every pass row owns one child row that names its GPU operation.
FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(
    const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture,
    const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo = FrameDebuggerRenderTargetInfo{});

// Which retained image the preview shows.
enum class FrameDebuggerPreviewSourceChoice {
    None,                 // No captured frame, or the needed image is missing.
    NotYetDrawn,          // Placeholder: nothing drawn yet at this step.
    Preview,              // The view as of before the composite pass.
    CompositedPreview,    // The final composited view.
    PerObjectStepPreview, // The accumulated view as of the selected draw.
};

// Picks the preview image. NotYetDrawn always wins. PerObjectStep never falls back to a whole-frame image.
// PostComposite prefers the composited image and falls back to the plain one.
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry,
    FrameDebuggerStepPreviewKind stepPreviewKind, bool hasPreview, bool hasCompositedPreview,
    bool hasPerObjectStepPreviewAtIndex);

} // namespace gte
