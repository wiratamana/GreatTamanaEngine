#include "FrameDebuggerData.h"

#include <cstddef>
#include <cstdio>

namespace gte {

FrameDebuggerSnapshot BuildPlaceholderFrameDebuggerSnapshot()
{
    // Deliberately empty - see FrameDebuggerData.h's own top-of-file
    // comment and PHASE0's Locked Design Decision #2. Returning a
    // default-constructed FrameDebuggerSnapshot{} is intentional, not a
    // stub left unfinished - this IS the finished behavior for this
    // campaign.
    return FrameDebuggerSnapshot{};
}

std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return "0 of 0";
    }
    // 1-based display, matching the reference screenshot's own
    // "2117 of 2117" convention - currentEventIndex is a 0-based index
    // internally (see ClampSelectedEventIndex()'s own contract), so +1
    // here, once, is the single place that conversion happens.
    const int displayIndex = currentEventIndex < 0 ? 0 : (currentEventIndex + 1);
    return std::to_string(displayIndex) + " of " + std::to_string(totalEventCount);
}

int ClampSelectedEventIndex(int requested, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return -1;
    }
    if (requested < 0) {
        return 0;
    }
    if (requested >= totalEventCount) {
        return totalEventCount - 1;
    }
    return requested;
}

std::string FormatFrameHistoryLabel(int cursorIndex, int count)
{
    if (count <= 0) {
        return "Frame 0 of 0";
    }
    // 1-based display, matching FormatFrameStepperLabel()'s own convention -
    // a negative cursorIndex (defensive only; FrameDebuggerHistory never
    // actually produces one once count > 0 - see
    // ClampFrameDebuggerHistoryCursor()) displays as "Frame 1", not "Frame
    // 0", for the same "never below the first real display index" reason.
    const int displayIndex = cursorIndex < 0 ? 1 : (cursorIndex + 1);
    return "Frame " + std::to_string(displayIndex) + " of " + std::to_string(count);
}

namespace {

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndexRecursive(
    const std::vector<FrameDebuggerEventNode>& nodes, int eventIndex)
{
    for (const FrameDebuggerEventNode& node : nodes) {
        if (node.isDrawCall && node.eventIndex == eventIndex) {
            return node.details;
        }
        std::optional<FrameDebuggerEventDetails> found
            = FindEventDetailsByIndexRecursive(node.children, eventIndex);
        if (found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<FrameDebuggerEventDetails> FindEventDetailsByIndex(
    const FrameDebuggerSnapshot& snapshot, int eventIndex)
{
    if (eventIndex < 0) {
        return std::nullopt;
    }
    return FindEventDetailsByIndexRecursive(snapshot.rootNodes, eventIndex);
}

namespace {

// Trims a fixed-precision formatted float down to the shortest
// "does not lose the value" representation - e.g. "1.000000" -> "1",
// "0.500000" -> "0.5" - matching the reference screenshot's own compact
// "(1, 1, 1, 1)" style rather than a fixed 6-decimal dump.
std::string FormatCompactFloat(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return std::string(buffer);
}

} // namespace

std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector)
{
    return "(" + FormatCompactFloat(vector.x) + ", " + FormatCompactFloat(vector.y) + ", "
        + FormatCompactFloat(vector.z) + ", " + FormatCompactFloat(vector.w) + ")";
}

std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix)
{
    std::string result;
    for (int row = 0; row < 4; ++row) {
        if (row > 0) {
            result += "\n";
        }
        for (int col = 0; col < 4; ++col) {
            if (col > 0) {
                result += " ";
            }
            result += FormatCompactFloat(matrix.values[static_cast<std::size_t>(row * 4 + col)]);
        }
    }
    return result;
}

namespace {

// PHASE2 - duplicated, real, hardcoded clear-color constant. Mirrors
// src/Application/RenderPasses.cpp's own kGameClearColor VALUE exactly
// (20/255, 20/255, 30/255, 1.0f - see that file's own top-of-file comment,
// which already establishes "duplicate a hardcoded engine constant with a
// comment documenting the value it must be kept in sync with" as an
// accepted pattern in this codebase, rather than a workaround). NOT
// re-exported/re-used directly from RenderPasses.cpp/.h here on purpose:
// RenderPasses.cpp's own copy is an anonymous-namespace, internal-linkage
// local, and src/Editor/ must never #include src/Application/ headers (see
// AGENTS.md's "Clean Architecture" - Application is the composition root
// that depends on Editor, never the reverse; exposing it via a NEW shared
// header was considered and rejected as unnecessary churn for one constant
// this campaign's own PHASE0 already established a "just duplicate it with
// a comment" precedent for). If RenderPasses.cpp's own kGameClearColor ever
// changes, update this constant to match.
constexpr float kFrameDebuggerGameClearColor[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };

// Finds the first pass in `passes` whose name exactly matches `name`, or
// nullptr if none does - a plain linear scan, exactly as cheap/simple as
// this once-per-captured-frame lookup needs to be.
const rg::RenderGraphPassSnapshot* FindPassByName(
    const std::vector<rg::RenderGraphPassSnapshot>& passes, const std::string& name)
{
    for (const rg::RenderGraphPassSnapshot& pass : passes) {
        if (pass.name == name) {
            return &pass;
        }
    }
    return nullptr;
}

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    for (const std::string& candidate : names) {
        if (candidate == name) {
            return true;
        }
    }
    return false;
}

// Builds one GPU-skinning compute-dispatch LEAF node for `pass` - see
// BuildRealFrameDebuggerSnapshot()'s own header-comment tree-shape
// description and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6.
FrameDebuggerEventNode BuildGpuSkinningLeaf(const rg::RenderGraphPassSnapshot& pass, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name;
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Dispatch";
    details.shaderName = pass.name;
    details.passName = "GPU Skinning";
    details.blendMode = "n/a (compute pass)";
    details.zClip = "n/a (compute pass)";
    details.zTest = "n/a (compute pass)";
    details.zWrite = "n/a (compute pass)";
    details.cull = "n/a (compute pass)";
    details.stencilRef = "n/a (compute pass)";
    details.stencilComp = "n/a (compute pass)";
    details.stencilPass = "n/a (compute pass)";
    details.stencilFail = "n/a (compute pass)";
    details.stencilZFail = "n/a (compute pass)";

    // Renderer::Dispatch() never touches PassGpuStats::drawStats (see
    // RenderPasses.cpp's AddGpuSkinningPasses() and DrawStats.h's own
    // header comment) - a compute pass genuinely never issues a draw call,
    // so reporting an all-zero "Draw Stats" row here would misleadingly
    // imply one happened. The one real, DrawStats-adjacent number
    // PassGpuStats DOES carry for a compute pass is its GPU timing sample -
    // report that instead (0.0ms whenever GPU timing is Absent/Unsupported,
    // exactly like every other GPU-timing consumer in this engine today -
    // see AGENTS.md's "Profiling" section).
    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    // textures/matrices deliberately left empty - real: a compute skinning
    // pass never samples a material texture or uses a camera matrix (see
    // PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md's own Step 3.1).

    leaf.details = std::move(details);
    return leaf;
}

// Builds the real "GameView" pass LEAF node - see
// BuildRealFrameDebuggerSnapshot()'s own header-comment tree-shape
// description and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6.
FrameDebuggerEventNode BuildGameViewLeaf(
    const rg::RenderGraphPassSnapshot& gameViewPass, const FrameDebuggerCaptureContext& capture, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = "GameView";
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Draw Mesh";
    details.passName = "GameView";

    // shaderName - every DISTINCT real Pipeline debug name recorded this
    // frame, comma-joined (Locked Design Decision #6). Empty (never a
    // fabricated placeholder name) if capture recorded no draws at all this
    // frame - a genuinely empty Game View is a real, honest outcome.
    {
        std::string joined;
        const std::vector<std::string>& names = capture.PipelineDebugNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += names[i];
        }
        details.shaderName = joined;
    }

    // textures - one row per DISTINCT real bound MaterialTexture debug name
    // - there is no per-slot "_MainTex"-style naming in this engine yet, so
    // `name` is a stable, generic label rather than an invented one (see
    // this phase's own Step 3.1).
    for (const std::string& textureName : capture.MaterialTextureDebugNames()) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Material Texture";
        texture.valueLabel = textureName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - the real clear color, plus the real aggregate DrawStats
    // (draw-call count / triangle count) taken directly from
    // graphSnapshot's own "GameView" pass entry, exactly as this phase's
    // own Step 3.1 specifies (NOT re-derived from `capture.DrawCallCount()`
    // - the graph snapshot's own stats are the authoritative source other
    // panels, e.g. the "Render Graph" panel, already trust).
    {
        FrameDebuggerVectorProperty clearColor;
        clearColor.name = "Clear Color";
        clearColor.x = kFrameDebuggerGameClearColor[0];
        clearColor.y = kFrameDebuggerGameClearColor[1];
        clearColor.z = kFrameDebuggerGameClearColor[2];
        clearColor.w = kFrameDebuggerGameClearColor[3];
        details.vectors.push_back(clearColor);

        FrameDebuggerVectorProperty drawStats;
        drawStats.name = "Draw Stats (Calls, Tris)";
        drawStats.x = static_cast<float>(gameViewPass.stats.drawStats.drawCallCount);
        drawStats.y = static_cast<float>(gameViewPass.stats.drawStats.triangleCount);
        details.vectors.push_back(drawStats);
    }

    // matrices - the real view-projection matrix this pass actually
    // rendered with this frame. Mat4 is COLUMN-MAJOR storage
    // (columns[c][r] via operator()(row, col) - see Math/Mat4.h's own class
    // comment), but FrameDebuggerMatrixProperty::values is ROW-MAJOR
    // (values[row*4 + col] is row `row`, column `col` - see
    // FrameDebuggerData.h's own struct comment) - so this copies through
    // Mat4::operator()(row, col) element-by-element (which already accounts
    // for the column-major storage internally) rather than a raw memcpy of
    // Mat4::Data() (which is column-major and would silently transpose the
    // displayed matrix - see PHASE2_FRAME_DEBUGGER_SNAPSHOT_BUILDER.md's
    // own explicit warning about exactly this bug class).
    {
        FrameDebuggerMatrixProperty viewProjection;
        viewProjection.name = "ViewProjection";
        const Mat4& matrix = capture.LastViewProjection();
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                viewProjection.values[static_cast<std::size_t>(row * 4 + col)] = matrix(row, col);
            }
        }
        details.matrices.push_back(viewProjection);
    }

    // blend/Z/stencil rows - this engine's real, single, constant Pipeline
    // configuration (Locked Design Decision #6).
    {
        const FrameDebuggerStandardPipelineState pipelineState = DescribeStandardPipelineState();
        details.blendMode = pipelineState.blendMode;
        details.zClip = pipelineState.zClip;
        details.zTest = pipelineState.zTest;
        details.zWrite = pipelineState.zWrite;
        details.cull = pipelineState.cull;
        details.stencilRef = pipelineState.stencilRef;
        details.stencilComp = pipelineState.stencilComp;
        details.stencilPass = pipelineState.stencilPass;
        details.stencilFail = pipelineState.stencilFail;
        details.stencilZFail = pipelineState.stencilZFail;
    }

    leaf.details = std::move(details);
    return leaf;
}

// Builds the real "Aerial Perspective Composite" pass LEAF node - see
// BuildRealFrameDebuggerSnapshot()'s own header-comment tree-shape
// description. frame-debugger-4 campaign, PHASE2. Mirrors
// BuildGpuSkinningLeaf()'s own "n/a (compute pass)" blend/Z/stencil
// convention (a real compute dispatch, never a draw call), but - unlike
// GPU Skinning, which never samples a material texture at all - this pass
// DOES have real, meaningful texture reads/writes worth showing (it is the
// whole reason this leaf exists: to make the atmosphere-compositing step
// visible), sourced directly from `pass.readNames`/`pass.writeNames` -
// never fabricated or re-derived from anywhere else.
FrameDebuggerEventNode BuildAerialPerspectiveCompositeLeaf(const rg::RenderGraphPassSnapshot& pass, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    // Mirrors BuildGpuSkinningLeaf()'s own leaf.name = pass.name convention
    // exactly - the real, raw render-graph pass name, never a prettified
    // invented one (this campaign's own honesty rule - see AGENTS.md,
    // "Testability & Regression Safety" and PHASE0_MASTER_STRATEGY.md's own
    // "never fabricate" precedent throughout this whole feature).
    leaf.name = pass.name; // "AtmosphereAerialPerspectiveCompositePass"
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Composite";
    // A friendlier grouping label, distinct from the raw pass name above -
    // mirrors BuildGpuSkinningLeaf()'s own details.passName = "GPU Skinning"
    // precedent (a real pass's own raw name vs. a short, human-readable
    // category label are allowed to differ).
    details.passName = "Aerial Perspective Composite";

    // shaderName - a real, hardcoded fact (there is exactly ONE compute
    // shader this pass ever dispatches - see AtmosphereLutRenderer::
    // AddAerialPerspectiveCompositePass()'s own renderer.Dispatch() call,
    // Shaders/AtmosphereAerialPerspectiveComposite.comp) - never fabricated,
    // mirrors DescribeStandardPipelineState()'s own "duplicate a real
    // hardcoded engine constant with a comment documenting what it must be
    // kept in sync with" precedent (FrameDebuggerCapture.cpp/.h).
    details.shaderName = "AtmosphereAerialPerspectiveComposite.comp";

    details.blendMode = "n/a (compute pass)";
    details.zClip = "n/a (compute pass)";
    details.zTest = "n/a (compute pass)";
    details.zWrite = "n/a (compute pass)";
    details.cull = "n/a (compute pass)";
    details.stencilRef = "n/a (compute pass)";
    details.stencilComp = "n/a (compute pass)";
    details.stencilPass = "n/a (compute pass)";
    details.stencilFail = "n/a (compute pass)";
    details.stencilZFail = "n/a (compute pass)";

    // textures - real read/write resource names, straight from this exact
    // pass's own already-resolved RenderGraphPassSnapshot fields - never
    // aggregated from FrameDebuggerCaptureContext (this pass is not a
    // per-draw-call thing at all, unlike the "GameView" leaf).
    for (const std::string& readName : pass.readNames) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Read Texture";
        texture.valueLabel = readName;
        details.textures.push_back(std::move(texture));
    }
    for (const std::string& writeName : pass.writeNames) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Write Texture";
        texture.valueLabel = writeName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - real GPU timing, exactly like BuildGpuSkinningLeaf()'s own
    // identical convention (0.0ms whenever GPU timing is Absent/Unsupported
    // - see that function's own comment for the full reasoning, unchanged
    // here).
    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    // matrices deliberately left empty - real: this pass's own real
    // invViewProjection/cameraWorldPosition push constants are genuinely
    // camera-derived, but PHASE0's own Non-Goals explicitly scope this leaf
    // to pass-level read/write/timing facts only, mirroring GPU Skinning's
    // own "no per-draw camera matrix" precedent for a compute pass (neither
    // pass is drawing anything with a rasterizer-consumed matrix).

    leaf.details = std::move(details);
    return leaf;
}

} // namespace

FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture, const std::vector<std::string>& gpuSkinningPassNamesThisFrame,
    const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo)
{
    const rg::RenderGraphPassSnapshot* gameViewPass = FindPassByName(graphSnapshot.passesInExecutionOrder, "GameView");
    if (gameViewPass == nullptr) {
        // Honest "no frame captured yet" empty result - see this function's
        // own header-comment contract and PHASE0's Locked Design Decision #2.
        return FrameDebuggerSnapshot{};
    }

    int nextEventIndex = 0;

    FrameDebuggerEventNode root;
    root.name = "Game View";
    root.isDrawCall = false;

    if (!gpuSkinningPassNamesThisFrame.empty()) {
        FrameDebuggerEventNode gpuSkinningGroup;
        gpuSkinningGroup.name = "GPU Skinning";
        gpuSkinningGroup.isDrawCall = false;

        // Preserve graphSnapshot's own real execution order - never the
        // (unrelated) order `gpuSkinningPassNamesThisFrame` itself happens
        // to list its names in.
        for (const rg::RenderGraphPassSnapshot& pass : graphSnapshot.passesInExecutionOrder) {
            if (Contains(gpuSkinningPassNamesThisFrame, pass.name)) {
                gpuSkinningGroup.children.push_back(BuildGpuSkinningLeaf(pass, nextEventIndex++));
            }
        }

        // Only add the group at all if at least one real pass in this
        // frame's graphSnapshot actually matched by name - see this
        // function's own Step 2 fallback reasoning in the phase document
        // (a caller-supplied name with no matching real pass this frame
        // must never produce an empty, misleading "GPU Skinning" group).
        if (!gpuSkinningGroup.children.empty()) {
            root.children.push_back(std::move(gpuSkinningGroup));
        }
    }

    root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++));

    // frame-debugger-4 campaign, PHASE2 - the real atmosphere-compositing
    // step, sibling to "GameView" above, in real execution order (it always
    // runs strictly AFTER "GameView" in the same frame - see
    // AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()'s own
    // pass.ReadTexture(sourceColorHandle, ...) dependency declaration).
    // Mirrors the "GPU Skinning" group's own "only add if a real matching pass
    // was actually found this frame" discipline - a graphSnapshot from before
    // this feature existed, or a hypothetical future build where atmosphere
    // compositing genuinely did not run, must never produce a fake, empty, or
    // misleading leaf.
    const rg::RenderGraphPassSnapshot* aerialPerspectiveCompositePass =
        FindPassByName(graphSnapshot.passesInExecutionOrder, "AtmosphereAerialPerspectiveCompositePass");
    if (aerialPerspectiveCompositePass != nullptr) {
        root.children.push_back(BuildAerialPerspectiveCompositeLeaf(*aerialPerspectiveCompositePass, nextEventIndex++));
    }

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(std::move(root));
    snapshot.totalEventCount = nextEventIndex;
    snapshot.renderTarget = gameViewRenderTargetInfo;
    snapshot.renderTarget.name = "GameView";
    return snapshot;
}

// frame-debugger-4 campaign, PHASE3 - see FrameDebuggerData.h's own doc
// comment for the full contract. A plain, exhaustive if/else chain over
// already-resolved booleans - deliberately no live FrameDebuggerHistoryEntry/
// RenderTexture dependency at all.
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(
    bool hasEntry, bool hasPreview, bool hasCompositedPreview, bool isViewingGameViewLeaf)
{
    if (!hasEntry) {
        return FrameDebuggerPreviewSourceChoice::None;
    }
    if (isViewingGameViewLeaf) {
        // Explicit leaf selection always wins - even if compositedPreview is
        // ALSO present for this captured frame (Locked Design Decision #5).
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    }
    if (hasCompositedPreview) {
        return FrameDebuggerPreviewSourceChoice::CompositedPreview;
    }
    if (hasPreview) {
        return FrameDebuggerPreviewSourceChoice::Preview;
    }
    return FrameDebuggerPreviewSourceChoice::None;
}

} // namespace gte
