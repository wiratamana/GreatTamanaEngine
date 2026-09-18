#include "FrameDebuggerData.h"

#include <algorithm>
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

// task_manager/frame-debugger-9 campaign, PHASE1
// (PHASE1_ASPECT_RATIO_CORRECT_PREVIEW.md, Step 3.1) - see this function's
// own doc comment in FrameDebuggerData.h for the full contract. Pure
// geometry: uniform scale-to-fit + centering, with a safe "fill the box at
// (0, 0)" fallback for any non-positive input.
FrameDebuggerAspectFitRect ComputeAspectFitImageRect(
    float availableWidth, float availableHeight, float sourceWidth, float sourceHeight)
{
    if (availableWidth <= 0.0f || availableHeight <= 0.0f || sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
        return FrameDebuggerAspectFitRect{ 0.0f, 0.0f, std::max(0.0f, availableWidth), std::max(0.0f, availableHeight) };
    }

    const float availableAspect = availableWidth / availableHeight;
    const float sourceAspect = sourceWidth / sourceHeight;

    float fittedWidth;
    float fittedHeight;
    if (sourceAspect > availableAspect) {
        // Source is relatively WIDER than the box - width-constrained (letterbox: bars top/bottom).
        fittedWidth = availableWidth;
        fittedHeight = availableWidth / sourceAspect;
    } else {
        // Source is relatively TALLER than (or equal to) the box - height-constrained (pillarbox: bars left/right).
        fittedHeight = availableHeight;
        fittedWidth = availableHeight * sourceAspect;
    }

    FrameDebuggerAspectFitRect rect;
    rect.width = fittedWidth;
    rect.height = fittedHeight;
    rect.offsetX = (availableWidth - fittedWidth) * 0.5f;
    rect.offsetY = (availableHeight - fittedHeight) * 0.5f;
    return rect;
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

// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.1) - finds the real
// execution-order index, among the surviving Post-GameView compute passes,
// of the pass that writes the "GameViewComposited" texture - the SAME
// literal texture name FrameDebuggerCurrentCapture::CaptureFrame()'s own
// `compositedGameViewSource` parameter is always created under
// (Application.cpp's "GameViewComposited" RenderTexture - see
// FrameDebuggerHistory.h's own doc comments, which already reference this
// exact name). This is a STRUCTURAL check against each pass's own real
// `writeNames` (a real render-graph resource-name fact, cross-checked
// against `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`'s
// own `outputTextureName` call-site argument at implementation time), NOT
// a check against the pass's own NAME (e.g.
// "AtmosphereAerialPerspectiveCompositePass") - so a hypothetical future
// rename of that pass would not silently break this split. Returns -1 if
// no surviving Post-GameView pass writes that texture this frame (e.g. a
// captured frame taken before the composite pass has ever run this
// session) - every Post-GameView leaf is then treated as PostComposite
// (Step 2's own "or nothing selected"/default catch-all bucket).
int FindPostGameViewCompositePassExecutionIndex(const rg::RenderGraphSnapshot& graphSnapshot, int gameViewIndex)
{
    for (int i = gameViewIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        for (const std::string& writeName : pass.writeNames) {
            if (writeName == "GameViewComposited") {
                return i;
            }
        }
    }
    return -1;
}

// frame-debugger-5 campaign, PHASE2
// (PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md, Step 3.2) -
// small, exhaustive-switch row-label helpers for a compute-dispatch leaf's
// read/write rows, keyed by PHASE1's new real rg::ResourceKind (never
// guessed) - mirrors RenderGraphTypes.cpp's own IsWriteAccess()/ToString()
// "deliberately NO `default:` case" convention exactly, so a future fourth
// ResourceKind enumerator fails to compile here until updated.
const char* ReadRowLabelForKind(rg::ResourceKind kind)
{
    switch (kind) {
    case rg::ResourceKind::Texture:
        return "Read Texture";
    case rg::ResourceKind::Buffer:
        return "Read Buffer";
    case rg::ResourceKind::VolumeTexture:
        return "Read Volume Texture";
    }
    return "Read Texture";
}

const char* WriteRowLabelForKind(rg::ResourceKind kind)
{
    switch (kind) {
    case rg::ResourceKind::Texture:
        return "Write Texture";
    case rg::ResourceKind::Buffer:
        return "Write Buffer";
    case rg::ResourceKind::VolumeTexture:
        return "Write Volume Texture";
    }
    return "Write Texture";
}

// frame-debugger-5 campaign, PHASE2 - the ONE generic leaf builder for ANY
// real compute dispatch (RenderGraphPassSnapshot::isComputePass == true)
// that survived this frame, whatever its name - GPU Skinning, every
// atmosphere LUT pass, Aerial Perspective Composite, the Aerial
// Perspective Volume Debug-Slice pass, Compute Blur Validation, and any
// future compute pass this engine ever adds. Mirrors the OLD, now-DELETED
// BuildGpuSkinningLeaf()/BuildAerialPerspectiveCompositeLeaf()'s own
// "n/a (compute pass)" blend/Z/stencil convention exactly (a compute
// dispatch never issues a draw call), but is the SINGLE, UNIFIED
// replacement for both of those deleted, name-specific functions - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6.
// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.1) - new LAST parameter
// `stepPreviewKind`, the caller's already-decided
// FrameDebuggerStepPreviewKind for THIS leaf (NotYetDrawn for a
// Pre-GameView pass, PreComposite/PostComposite for a Post-GameView pass
// depending on its own position relative to the atmosphere composite
// pass - see BuildRealFrameDebuggerSnapshot()'s own call sites below).
// `stepPreviewIndex` is left at its default (-1) - never meaningful for a
// compute-dispatch leaf (only PerObjectStep leaves use it).
FrameDebuggerEventNode BuildComputeDispatchLeaf(
    const rg::RenderGraphPassSnapshot& pass, int eventIndex, FrameDebuggerStepPreviewKind stepPreviewKind)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name; // The real, raw render-graph pass name - never fabricated/prettified.
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Dispatch";
    details.stepPreviewKind = stepPreviewKind;
    // No separate "friendly label" - the real, raw pass name IS the
    // passName too, for every compute leaf uniformly (this is what makes
    // this function correct for a pass this file has never heard of
    // before - see PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md's
    // own Step 3.4 for the OPTIONAL cosmetic-label idea this deliberately
    // does NOT implement by default).
    details.passName = pass.name;
    // shaderName - honestly, this generic function has no way to know a
    // real compute pass's exact .comp shader FILE name (that fact used to
    // live only in the deleted, hand-written
    // BuildAerialPerspectiveCompositeLeaf()'s own hardcoded string) - the
    // real, raw pass NAME is used here instead, which is always true and
    // never fabricated.
    details.shaderName = pass.name;

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

    // textures - real read/write resource names, EACH LABELED BY ITS REAL
    // KIND (PHASE1's new readKinds/writeKinds) so a buffer write (e.g. GPU
    // Skinning's own output buffer) is never mislabeled as a texture.
    // task_manager/frame-debugger-9 campaign, PHASE3 - each row also now
    // carries its own real `kind` PLUS `isRenderGraphResource = true` (this
    // IS the one real construction site that has that data - see
    // FrameDebuggerTextureProperty::isRenderGraphResource's own doc comment,
    // FrameDebuggerData.h), which is what lets
    // Panels/FrameDebuggerPanel.cpp's ShaderProperties tab decide which rows
    // get a "View" button.
    for (std::size_t i = 0; i < pass.readNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        const rg::ResourceKind readKind = i < pass.readKinds.size() ? pass.readKinds[i] : rg::ResourceKind::Texture;
        texture.name = ReadRowLabelForKind(readKind);
        texture.valueLabel = pass.readNames[i];
        texture.kind = readKind;
        texture.isRenderGraphResource = true;
        details.textures.push_back(std::move(texture));
    }
    for (std::size_t i = 0; i < pass.writeNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        const rg::ResourceKind writeKind = i < pass.writeKinds.size() ? pass.writeKinds[i] : rg::ResourceKind::Texture;
        texture.name = WriteRowLabelForKind(writeKind);
        texture.valueLabel = pass.writeNames[i];
        texture.kind = writeKind;
        texture.isRenderGraphResource = true;
        details.textures.push_back(std::move(texture));
    }

    // vectors - real GPU timing (0.0ms whenever GPU timing is
    // Absent/Unsupported, exactly like every other GPU-timing consumer in
    // this engine today - see AGENTS.md's "Profiling" section). A compute
    // dispatch never issues a draw call, so PassGpuStats::drawStats is
    // deliberately never reported here (see RenderPasses.cpp's
    // AddGpuSkinningPasses()/DrawStats.h's own header comment).
    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    // matrices deliberately left empty - a compute dispatch never uses a
    // rasterizer-consumed camera matrix (mirrors the deleted
    // BuildGpuSkinningLeaf()/BuildAerialPerspectiveCompositeLeaf()'s own
    // identical convention).

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
    // task_manager/frame-debugger-7 campaign, PHASE4 - the real "GameView"
    // leaf always shows the pre-atmosphere-composite `preview` image
    // (Locked Design Decision #5) - PreComposite carries exactly that
    // meaning in the new FrameDebuggerStepPreviewKind scheme.
    details.stepPreviewKind = FrameDebuggerStepPreviewKind::PreComposite;

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

// frame-debugger-6 campaign, PHASE4 - one real, individually selectable leaf
// per real per-entity draw call this frame's "GameView" pass actually issued
// (FrameDebuggerCaptureContext::DrawRecords(), PHASE3). This is an EXPLICIT,
// user-approved breaking change to the old "one leaf per PASS, never one
// leaf per mesh" rule (see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #1) - scoped ONLY to real children of the "GameView" leaf itself,
// nothing else about pass-level granularity elsewhere in this tree changes.
// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.1) - new LAST parameter
// `stepPreviewIndex` - this record's own 0-based position among
// FrameDebuggerCaptureContext::DrawRecords() (the SAME index
// FrameDebuggerHistoryEntry::perObjectStepPreviews uses, PHASE3/PHASE4) -
// so this leaf's own `stepPreviewKind`/`stepPreviewIndex` let
// ChooseFrameDebuggerPreviewSource() show the real, accumulated "Game View
// as of THIS object" image (fixes Bug 2).
//
// frame-debugger-8 campaign, PHASE3
// (PHASE3_SNAPSHOT_TREE_LEAF_AND_TESTS.md) - this function now also builds a
// SECOND, differently-shaped leaf when `record.isSkyBackgroundDraw == true`
// (the one, real Sky Background full-screen-triangle draw - see
// FrameDebuggerCapture.h's own FrameDebuggerDrawRecord doc comment) - see
// this function's own body for the two-branch split. The per-entity `else`
// branch this comment block already describes above is completely
// unchanged.
FrameDebuggerEventNode BuildGameViewDrawRecordLeaf(
    const FrameDebuggerDrawRecord& record, const Mat4& sharedViewProjection, int eventIndex, int stepPreviewIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.stepPreviewKind = FrameDebuggerStepPreviewKind::PerObjectStep;
    details.stepPreviewIndex = stepPreviewIndex;

    // frame-debugger-8 campaign, PHASE3 - the Sky Background draw is not a
    // real ECS entity (record.entityIndex/entityGeneration are meaningless
    // for it - see FrameDebuggerCapture.h's own FrameDebuggerDrawRecord doc
    // comment) - it needs its own, differently-shaped leaf. This branch is
    // the ONLY change to this function versus its pre-campaign shape; the
    // `else` arm below is BYTE-FOR-BYTE the same code this function already
    // had for every real entity, unchanged.
    if (record.isSkyBackgroundDraw) {
        // Locked Design Decision 2 (PHASE0_MASTER_STRATEGY.md) - the tree
        // row's own name AND its Inspector "Shader" row are BOTH the real,
        // hand-verified shader file pair
        // (AtmosphereSkyBackgroundRenderer::ShaderDebugName(), threaded
        // through here via record.pipelineDebugName) - never an invented
        // cosmetic label like "Sky Background".
        leaf.name = record.pipelineDebugName;
        details.eventLabel = "Draw Fullscreen Triangle";
        // "GameView (Sky Draw)" - a real, structural fact (which real pass
        // this draw happened inside), DISTINCT from both the literal
        // "GameView" pass leaf's own passName AND from
        // "GameView (Entity Draw)" (the per-entity leaves' own passName) -
        // mirrors that exact, pre-existing distinctness precedent (see this
        // function's own `else` arm below, and its historical doc comment
        // above this function for the original "why must this differ from
        // the literal 'GameView' string" reasoning, which applies here too).
        details.passName = "GameView (Sky Draw)";
        details.shaderName = record.pipelineDebugName;

        // vectors - just the real triangle count (always 1 - a single
        // full-screen triangle, see FrameDebuggerCapture.cpp's own
        // RecordSkyBackgroundDraw()). Deliberately NO "Entity (Index,
        // Generation)" row here (unlike the `else` arm below) - there is no
        // real ECS entity behind this record at all, and fabricating one
        // would violate this whole tree's own "never invent a fact" rule.
        {
            FrameDebuggerVectorProperty triangleCount;
            triangleCount.name = "Triangle Count";
            triangleCount.x = static_cast<float>(record.triangleCount);
            details.vectors.push_back(triangleCount);
        }

        // blend/Z/stencil - THIS pass's own real, distinct pipeline state
        // (Locked Design Decision 4, PHASE0_MASTER_STRATEGY.md) - never the
        // generic per-mesh DescribeStandardPipelineState() every entity
        // leaf reuses (see the `else` arm below).
        {
            const FrameDebuggerStandardPipelineState pipelineState = DescribeSkyBackgroundPipelineState();
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
    } else {
        // UNCHANGED from before this campaign - every real per-entity leaf
        // keeps behaving exactly as it always has.
        leaf.name = record.displayName + " (Entity " + std::to_string(record.entityIndex) + ")";
        details.eventLabel = "Draw Mesh";
        details.passName = "GameView (Entity Draw)";
        details.shaderName = record.pipelineDebugName;

        if (!record.materialTextureDebugName.empty()) {
            FrameDebuggerTextureProperty texture;
            texture.name = "Material Texture";
            texture.valueLabel = record.materialTextureDebugName;
            details.textures.push_back(std::move(texture));
        }

        {
            FrameDebuggerVectorProperty triangleCount;
            triangleCount.name = "Triangle Count";
            triangleCount.x = static_cast<float>(record.triangleCount);
            details.vectors.push_back(triangleCount);

            FrameDebuggerVectorProperty entityIdentity;
            entityIdentity.name = "Entity (Index, Generation)";
            entityIdentity.x = static_cast<float>(record.entityIndex);
            entityIdentity.y = static_cast<float>(record.entityGeneration);
            details.vectors.push_back(entityIdentity);
        }

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
    }

    // matrices - the SAME shared view-projection every draw in this one
    // real Game-View pass this frame used, for BOTH branches above (the
    // sky's own fragment shader really does invert this exact matrix - see
    // AtmosphereSkyBackgroundRenderer::Draw()'s own `viewProjection`
    // parameter) - UNCHANGED code, simply now shared by both branches
    // instead of only ever running for the entity case.
    {
        FrameDebuggerMatrixProperty viewProjection;
        viewProjection.name = "ViewProjection";
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                viewProjection.values[static_cast<std::size_t>(row * 4 + col)] = sharedViewProjection(row, col);
            }
        }
        details.matrices.push_back(viewProjection);
    }

    leaf.details = std::move(details);
    return leaf;
}

} // namespace

FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture, const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo)
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

    // frame-debugger-5 campaign, PHASE2 - THE generic replacement for the
    // former "GPU Skinning" group AND the former hardcoded
    // "AtmosphereAerialPerspectiveCompositePass" special case (see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6). Every real,
    // surviving compute pass this frame becomes one leaf here, in real
    // execution order, whatever its name - "GameView" itself is
    // structurally excluded (it is never isComputePass==true, since it's
    // declared via plain AddPass()/WriteColorAttachment(), never
    // AddComputePass()). SPLIT into a pre/post pair (Locked Design
    // Decision #8, v2 review finding) so a pass that genuinely ran BEFORE
    // "GameView" (e.g. GPU Skinning, every atmosphere LUT pass) is never
    // shown as if it ran after it.
    //
    // frame-debugger-6 campaign, PHASE2
    // (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md) - BOTH loops
    // below also now exclude any surviving compute pass whose PHASE1-stamped
    // `rg::ViewScope` is `SceneView` - this engine genuinely runs a SEPARATE
    // copy of several Atmosphere compute passes for the Editor's own
    // Scene-View camera (writing to "..._SceneView"-suffixed resources) in
    // addition to the Game-View ones, but both copies used to share the exact
    // same literal pass NAME (e.g. "AtmosphereSkyViewLutPass"), so this tree
    // used to show duplicate, indistinguishable leaves and even leak a
    // genuinely Scene-View-only debug tool ("ComputeBlurValidation") into a
    // tree that this feature's own permanent rule says is Game-View ONLY
    // (see this campaign's own PHASE0_MASTER_STRATEGY.md Section 0 and Locked
    // Design Decision #2). `Shared` (e.g.
    // the Transmittance/Multi-Scattering LUT passes, genuinely computed once
    // per frame, not once per view) and `GameView` both still pass through
    // unchanged - this is a structural, one-line boolean filter, never a
    // pass-name/resource-suffix string comparison.
    const int gameViewIndex = static_cast<int>(gameViewPass - graphSnapshot.passesInExecutionOrder.data());

    FrameDebuggerEventNode preGameViewGroup;
    preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
    preGameViewGroup.isDrawCall = false;

    FrameDebuggerEventNode postGameViewGroup;
    postGameViewGroup.name = "Compute Dispatches (Post-GameView)";
    postGameViewGroup.isDrawCall = false;

    // frame-debugger-5 campaign, PHASE2 - IMPORTANT ("nextEventIndex
    // ordering" fix vs. the phase document's own inline code sample): the
    // phase document's Step 3.3 literally showed ONE single loop over the
    // WHOLE passesInExecutionOrder range that routes each surviving compute
    // pass into whichever group it belongs to. Re-verified against this
    // function's own explicitly-documented invariant (and the phase
    // document's own immediately-following "IMPORTANT (nextEventIndex
    // ordering caveat)" prose, which says the pre-GameView loop must assign
    // its leaves' eventIndex values BEFORE "GameView" gets its own, and the
    // post-GameView loop must assign its leaves' eventIndex values AFTER):
    // a SINGLE loop over the whole array would assign eventIndex values to
    // POST-GameView passes BEFORE "GameView" itself ever gets one (since the
    // single loop finishes completely before BuildGameViewLeaf() is ever
    // called) - silently breaking the documented "pre < GameView < post"
    // monotonic ordering for any frame with a POST-GameView compute pass.
    // Caught by this phase's own new
    // EventIndexIsMonotonicAcrossPreGameViewGameViewAndPostGameView test
    // (and 2 others) actually failing against the doc's literal sample.
    // Fixed here by using TWO range-restricted loops instead - one over
    // [0, gameViewIndex) BEFORE "GameView"'s own leaf is built, one over
    // (gameViewIndex, size) AFTER - so eventIndex is genuinely monotonic
    // increasing in true chronological order, exactly as documented.
    for (int i = 0; i < gameViewIndex; ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // frame-debugger-6 campaign, PHASE2
        // (PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md, Step 3.1) -
        // a pass whose PHASE1-stamped rg::ViewScope is SceneView is NEVER
        // genuinely part of the Game View's own render/compute chain, even if
        // it happens to survive this frame and sit before "GameView"'s own
        // index - e.g. a Scene-View-only copy of an Atmosphere LUT pass that
        // shares its literal pass NAME with the real Game-View instance (the
        // exact real-world collision that motivated this whole campaign - see
        // PHASE0_MASTER_STRATEGY.md Section 0). `Shared` and `GameView` both
        // still pass through unchanged.
        if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        // task_manager/frame-debugger-7 campaign, PHASE4 - a Pre-GameView
        // compute-dispatch leaf's own "as of this step" image is honestly
        // "nothing drawn to the screen yet" (Locked Design Decision #6).
        preGameViewGroup.children.push_back(
            BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn));
    }

    // Only add either group at all if something real actually survived
    // this frame in THAT half - mirrors the deleted "GPU Skinning" group's
    // own "only add if at least one real pass actually matched" discipline,
    // applied independently to each half (it is entirely normal/expected
    // for only one half to have children on a given captured frame).
    if (!preGameViewGroup.children.empty()) {
        root.children.push_back(std::move(preGameViewGroup));
    }
    FrameDebuggerEventNode gameViewLeaf = BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++);
    // frame-debugger-6 campaign, PHASE4 - one real child leaf per real per-draw
    // attribution record captured this frame (PHASE3), indexed strictly AFTER
    // "GameView"'s own eventIndex and strictly BEFORE anything in the
    // Post-GameView group below - preserves the exact same "pre < GameView <
    // post" monotonic-eventIndex invariant this function's own PHASE2 (of
    // frame-debugger-5) comment already documents, simply extended one level
    // task_manager/frame-debugger-7 campaign, PHASE4 - each per-object draw
    // child leaf's own `stepPreviewIndex` is its 0-based position among
    // capture.DrawRecords() - the SAME index
    // FrameDebuggerHistoryEntry::perObjectStepPreviews will use (Phase 3's
    // ReplayStepPreviews(), moved there by TriggerCapture()/CaptureFrame()).
    int perObjectStepIndex = 0;
    for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
        gameViewLeaf.children.push_back(BuildGameViewDrawRecordLeaf(
            record, capture.LastViewProjection(), nextEventIndex++, perObjectStepIndex));
        ++perObjectStepIndex;
    }
    root.children.push_back(std::move(gameViewLeaf));

    // task_manager/frame-debugger-7 campaign, PHASE4 - a Post-GameView
    // leaf's own "as of this step" image depends on whether it runs
    // before or at/after the real atmosphere composite pass (see
    // FindPostGameViewCompositePassExecutionIndex()'s own doc comment
    // above) - found STRUCTURALLY, by matching each surviving pass's own
    // real `writeNames` against the "GameViewComposited" texture name,
    // never by comparing against the composite pass's own literal NAME.
    const int compositePassIndex = FindPostGameViewCompositePassExecutionIndex(graphSnapshot, gameViewIndex);
    for (int i = gameViewIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // frame-debugger-6 campaign, PHASE2 - identical rule, symmetrically
        // applied to the post-GameView half (see the pre-GameView loop's own
        // comment above for the full rationale) - e.g. the real
        // "AtmosphereAerialPerspectiveCompositePass" duplicate scenario.
        if (!pass.isComputePass || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            continue;
        }
        // Strictly BEFORE the composite pass's own index -> PreComposite
        // (the accumulated image is still the pre-atmosphere-fog one);
        // AT the composite pass's own index (its own leaf) or AFTER it,
        // or no composite pass found at all this capture -> PostComposite.
        const FrameDebuggerStepPreviewKind stepPreviewKind = (compositePassIndex >= 0 && i < compositePassIndex)
            ? FrameDebuggerStepPreviewKind::PreComposite
            : FrameDebuggerStepPreviewKind::PostComposite;
        postGameViewGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind));
    }
    if (!postGameViewGroup.children.empty()) {
        root.children.push_back(std::move(postGameViewGroup));
    }

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(std::move(root));
    snapshot.totalEventCount = nextEventIndex;
    snapshot.renderTarget = gameViewRenderTargetInfo;
    snapshot.renderTarget.name = "GameView";
    return snapshot;
}

// frame-debugger-4 campaign, PHASE3 - see FrameDebuggerData.h's own doc
// comment for the full contract. A plain, exhaustive switch over
// FrameDebuggerStepPreviewKind - deliberately no live FrameDebuggerHistoryEntry/
// RenderTexture dependency at all.
//
// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.2/3.3) - REWRITTEN. The
// old `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`
// functions (frame-debugger-5 campaign) and the old `hasSelectedComputePassPreview`/
// `isViewingGameViewLeaf` boolean-soup parameters are GONE (an explicit,
// user-approved breaking change - PHASE0's Locked Design Decision #3); see
// FrameDebuggerData.h's own doc comment for the full new contract.
FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry,
    FrameDebuggerStepPreviewKind stepPreviewKind, bool hasPreview, bool hasCompositedPreview,
    bool hasPerObjectStepPreviewAtIndex)
{
    if (!hasEntry) {
        return FrameDebuggerPreviewSourceChoice::None;
    }
    // Deliberately NO `default:` case - the same exhaustive-switch
    // convention this file's own ReadRowLabelForKind()/WriteRowLabelForKind()
    // already establish, so a future fifth FrameDebuggerStepPreviewKind
    // enumerator fails to compile here until updated.
    switch (stepPreviewKind) {
    case FrameDebuggerStepPreviewKind::NotYetDrawn:
        // Honest "nothing drawn yet" placeholder - always wins outright,
        // regardless of hasPreview/hasCompositedPreview (Locked Design
        // Decision #6 - never fabricate an image for this bucket).
        return FrameDebuggerPreviewSourceChoice::NotYetDrawn;
    case FrameDebuggerStepPreviewKind::PerObjectStep:
        // Fixes Bug 2 - the real, accumulated "Game View as of THIS
        // object" image, or an honest `None` if it somehow isn't
        // available (never falls back to the fog-inclusive whole-frame
        // image - that would show later objects/atmosphere fog that
        // hadn't actually been applied yet at this exact step).
        return hasPerObjectStepPreviewAtIndex ? FrameDebuggerPreviewSourceChoice::PerObjectStepPreview
                                               : FrameDebuggerPreviewSourceChoice::None;
    case FrameDebuggerStepPreviewKind::PreComposite:
        // The literal "GameView" leaf itself, or a Post-GameView leaf
        // that runs before the composite pass - always the pre-composite
        // `preview` (Locked Design Decision #5), never `compositedPreview`.
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    case FrameDebuggerStepPreviewKind::PostComposite:
        // The composite pass itself, anything after it, or nothing
        // selected at all - prefers the true, final `compositedPreview`,
        // falling back to `preview` only when this particular captured
        // frame has no composited image at all.
        if (hasCompositedPreview) {
            return FrameDebuggerPreviewSourceChoice::CompositedPreview;
        }
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    }
    return FrameDebuggerPreviewSourceChoice::None;
}

} // namespace gte
