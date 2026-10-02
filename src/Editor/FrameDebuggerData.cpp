#include "FrameDebuggerData.h"
#include "../Renderer/RenderGraph/RenderPassGroupRegistry.h"

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

// render-pass-3 campaign, PHASE4 - REPLACES the old literal
// FindPassByName(..., "RenderOpaque") pivot search with a structural,
// name-free lookup: the first pass, in true execution order, whose
// rg::RenderPassEvent sort hint is >= RenderPassEvent::Opaques (see
// RenderGraphTypes.h's own doc comment on that enum - "a principled way
// to answer 'where does the view region start' ... instead of searching
// for a specific pass by name"). Deliberately does NOT filter by
// isCulled/kind/viewScope/category here - this mirrors the OLD
// FindPassByName() call's own behavior exactly (it never filtered on
// those either), preserving this function's existing "return nullptr,
// treated as 'no frame captured yet'" behavior for the one genuinely
// reachable failure case (an empty/degenerate graph snapshot).
const rg::RenderGraphPassSnapshot* FindViewRegionPivot(
    const std::vector<rg::RenderGraphPassSnapshot>& passesInExecutionOrder)
{
    for (const rg::RenderGraphPassSnapshot& pass : passesInExecutionOrder) {
        if (pass.renderPassEvent >= rg::RenderPassEvent::Opaques) {
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
        // editor-core-separation-26 campaign, PHASE4's own companion audit -
        // confirmed safe for `PassKind::Blit` too: this is a one-way
        // Compute-only FILTER (a Blit pass is simply skipped, exactly like a
        // Graphics pass already is), never a disguised "not Compute =
        // Graphics" binary branch; the only field read below (`writeNames`)
        // is valid for any `PassKind`. Not one of the 3 sites the source
        // document names for conversion - see PHASE0_MASTER_STRATEGY.md's
        // Locked Decision 2.
        if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
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
    case rg::ResourceKind::TextureArray:
        return "Read Texture Array";
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
    case rg::ResourceKind::TextureArray:
        return "Write Texture Array";
    }
    return "Write Texture";
}

// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE2 - the ONE shared mechanism that turns a single, flat "pass IS the
// draw event" leaf into a real "v PassName" parent OWNING exactly one real
// child event row - the fix for "DrawSkyBackground seems not owned by any
// render-pass" (and, generalized, every other flat pass leaf this file used
// to build - PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1).
//
// `passLevelNode` is a fully-built leaf EXACTLY as BuildComputeDispatchLeaf()/
// BuildGraphicsPassLeaf() already built it before this phase (real name,
// real eventIndex, real populated `details`) - this function does not
// change any of that data, it only (a) makes a COPY of it for the new child
// (Locked Design Decision #2 - both parent and child stay independently
// selectable, both show the same real pass-level facts), (b) overwrites the
// COPY's own name/eventIndex/details->eventIndex/details->eventLabel to
// describe the actual GPU operation instead of the owning pass, and (c)
// attaches it as `passLevelNode`'s one and only child.
//
// `childEventIndex` must be the NEXT value `nextEventIndex` produces AFTER
// `passLevelNode.eventIndex` was assigned - the caller is responsible for
// this ordering (see this file's own BuildRealFrameDebuggerSnapshot(), which
// calls `nextEventIndex++` twice per pass, back-to-back, to guarantee it).
FrameDebuggerEventNode WrapPassWithOwnedChildEvent(
    FrameDebuggerEventNode passLevelNode, int childEventIndex, const std::string& childEventLabel)
{
    FrameDebuggerEventNode child = passLevelNode; // Copies name/eventIndex/details/children (children is always
                                                   // empty on passLevelNode at this point - defensive-safe either way).
    child.name = childEventLabel;
    child.eventIndex = childEventIndex;
    child.children.clear();
    if (child.details.has_value()) {
        child.details->eventIndex = childEventIndex;
        child.details->eventLabel = childEventLabel;
    }
    passLevelNode.children.push_back(std::move(child));
    return passLevelNode;
}

// Frame Debugger Pass-Ownership campaign, PHASE2 - which child-event label a
// Graphics-kind pass's owned child gets, derived PURELY from its own real,
// structural rg::RenderPassDrawKind (PHASE1) - NEVER from a pass-name string
// comparison (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3).
// Deliberately NO `default:` case - mirrors ReadRowLabelForKind()/
// WriteRowLabelForKind()'s own exhaustive-switch convention immediately
// above in this same file, so a future fourth RenderPassDrawKind enumerator
// fails to compile here until updated.
const char* GraphicsChildEventLabelFor(rg::RenderPassDrawKind drawKind)
{
    switch (drawKind) {
    case rg::RenderPassDrawKind::DrawMesh:
        return "Draw Mesh";
    case rg::RenderPassDrawKind::DrawQuad:
        return "Draw Quad";
    case rg::RenderPassDrawKind::Blit:
        return "Blit";
    }
    return "Draw Mesh";
}

// frame-debugger-5 campaign, PHASE2 - the ONE generic leaf builder for ANY
// real compute dispatch (RenderGraphPassSnapshot::kind == rg::PassKind::Compute)
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

// Render Pass campaign (task_manager/render-pass-1), PHASE4
// (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3) - builds the real
// "RenderOpaque" pass LEAF node (RENAMED from the old "GameView" pass - see
// PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md, which split the old
// monolithic "GameView" pass into "RenderOpaque"/"DrawSkyBackground"/
// "RenderTransparent"). Per-entity children are attached separately by the
// caller (BuildRealFrameDebuggerSnapshot() below) - this function only ever
// builds the parent leaf itself, exactly like BuildGameViewLeaf() (this
// function's own pre-PHASE4 name) always did.
FrameDebuggerEventNode BuildRenderOpaqueLeaf(
    const rg::RenderGraphPassSnapshot& renderOpaquePass, const FrameDebuggerCaptureContext& capture, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = "RenderOpaque";
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Draw Mesh";
    details.passName = "RenderOpaque";
    // task_manager/frame-debugger-7 campaign, PHASE4 - the real "RenderOpaque"
    // leaf always shows the pre-atmosphere-composite `preview` image
    // (Locked Design Decision #5) - PreComposite carries exactly that
    // meaning in the FrameDebuggerStepPreviewKind scheme.
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
    // `name` is a stable, generic label rather than an invented one.
    for (const std::string& textureName : capture.MaterialTextureDebugNames()) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Material Texture";
        texture.valueLabel = textureName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - the real clear color, plus the real aggregate DrawStats
    // (draw-call count / triangle count) taken directly from graphSnapshot's
    // own "RenderOpaque" pass entry, exactly as before this phase (NOT
    // re-derived from `capture.DrawCallCount()`).
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
        drawStats.x = static_cast<float>(renderOpaquePass.stats.drawStats.drawCallCount);
        drawStats.y = static_cast<float>(renderOpaquePass.stats.drawStats.triangleCount);
        details.vectors.push_back(drawStats);
    }

    // matrices - the real view-projection matrix this pass actually
    // rendered with this frame - see FrameDebuggerData.h's own struct
    // comment for the row/column-major conversion rule this copies through.
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
    // configuration.
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
// per real per-entity draw call this frame's "RenderOpaque" pass actually
// issued (FrameDebuggerCaptureContext::DrawRecords(), PHASE3 of that
// campaign).
//
// Render Pass campaign (task_manager/render-pass-1), PHASE4
// (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3/3.4) - the old Sky
// Background branch (`record.isSkyBackgroundDraw`) is REMOVED entirely: the
// Sky Background draw is now its own real, separate "DrawSkyBackground" pass
// leaf (see BuildGraphicsPassLeaf() below), generically discovered by
// BuildRealFrameDebuggerSnapshot()'s own view-region walk - it no longer
// needs a fabricated FrameDebuggerDrawRecord at all. This function now ONLY
// ever builds a per-entity leaf - the single, unbranched shape every
// FrameDebuggerDrawRecord it receives always has today. Also renamed
// `passName` from `"GameView (Entity Draw)"` to `"RenderOpaque (Entity
// Draw)"` - a real, user-visible/HTTP-consumed fact that must stay accurate
// now that the owning pass itself is named "RenderOpaque".
FrameDebuggerEventNode BuildRenderOpaqueDrawRecordLeaf(
    const FrameDebuggerDrawRecord& record, const Mat4& sharedViewProjection, int eventIndex, int stepPreviewIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.stepPreviewKind = FrameDebuggerStepPreviewKind::PerObjectStep;
    details.stepPreviewIndex = stepPreviewIndex;

    leaf.name = record.displayName + " (Entity " + std::to_string(record.entityIndex) + ")";
    details.eventLabel = "Draw Mesh";
    details.passName = "RenderOpaque (Entity Draw)";
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

// Render Pass campaign (task_manager/render-pass-1), PHASE4
// (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3) - the ONE generic
// leaf builder for every OTHER real Graphics-kind pass the view-region walk
// (BuildRealFrameDebuggerSnapshot(), below) discovers besides "RenderOpaque"
// itself - today, always exactly "DrawSkyBackground" (a real, separate pass
// as of PHASE2 of this campaign); once a future transparency campaign makes
// "RenderTransparent" a real, non-empty pass too, it lands here as well, with
// no further Frame Debugger code changes required. Mirrors
// BuildComputeDispatchLeaf()'s own overall shape (the real, raw pass name
// doubles as both `passName` and `shaderName` - honest, never fabricated -
// plus real aggregate draw stats from this pass's own RenderGraphPassSnapshot
// entry) adapted for a Graphics-kind pass's own real blend/Z/stencil facts
// instead of "n/a (compute pass)".
//
// "DrawSkyBackground" specifically reuses DescribeSkyBackgroundPipelineState()
// verbatim - its own real, genuinely different (EQUAL depth test, depth
// write off) pipeline state, hand-verified against
// AtmosphereSkyBackgroundRenderer.cpp (see FrameDebuggerCapture.h/.cpp).
// Every OTHER pass this function is ever called for (i.e. a real future
// "RenderTransparent") falls back to the engine's generic
// DescribeStandardPipelineState() - a deliberate, reasonable default, NOT a
// verified fact (a real "RenderTransparent" pass's own blend/Z/stencil
// source is explicitly OUT OF SCOPE for this phase, per its own "What We
// Will NOT Do" - that pass never actually exists in the graph today).
FrameDebuggerEventNode BuildGraphicsPassLeaf(
    const rg::RenderGraphPassSnapshot& pass, int eventIndex, FrameDebuggerStepPreviewKind stepPreviewKind)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name; // The real, raw render-graph pass name - never fabricated/prettified.
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE2 - reuses
    // the SAME structural label its new owned child event row also gets, so
    // the Inspector's "Event #N: {label}" header is meaningful no matter
    // which of the two independently-selectable rows is selected.
    details.eventLabel = GraphicsChildEventLabelFor(pass.drawKind);
    details.stepPreviewKind = stepPreviewKind;
    details.passName = pass.name;
    details.shaderName = pass.name;

    const FrameDebuggerStandardPipelineState pipelineState =
        (pass.name == "DrawSkyBackground") ? DescribeSkyBackgroundPipelineState() : DescribeStandardPipelineState();
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

    // vectors - real aggregate draw stats from this pass's own real
    // RenderGraphPassSnapshot entry (never re-derived).
    FrameDebuggerVectorProperty drawStats;
    drawStats.name = "Draw Stats (Calls, Tris)";
    drawStats.x = static_cast<float>(pass.stats.drawStats.drawCallCount);
    drawStats.y = static_cast<float>(pass.stats.drawStats.triangleCount);
    details.vectors.push_back(drawStats);

    leaf.details = std::move(details);
    return leaf;
}

} // namespace

FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture, const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo)
{
    // Render Pass campaign (task_manager/render-pass-1), PHASE4
    // (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.1) - the
    // view-region pivot REPLACES the old literal "GameView" pass lookup -
    // "GameView" the PASS no longer exists at all as of PHASE2 (it split into
    // "RenderOpaque"/"DrawSkyBackground"/"RenderTransparent"); "GameView" the
    // RenderTexture/resource name is a completely separate, unaffected
    // concept (see this function's own `renderTarget.name` literal below).
    // render-pass-3 campaign, PHASE4 - REPLACED the literal
    // FindPassByName(..., "RenderOpaque") name-string pivot search with the
    // structural, name-free FindViewRegionPivot() lookup above (the first
    // pass, in true execution order, whose RenderPassEvent is >= Opaques).
    // "RenderOpaque" is still, today, the first pass tagged
    // RenderPassEvent::Opaques in every real snapshot - this variable is kept
    // named `renderOpaquePass` purely for readability in the rest of this
    // function, which already only cares about it as "the pivot pass."
    const rg::RenderGraphPassSnapshot* renderOpaquePass =
        FindViewRegionPivot(graphSnapshot.passesInExecutionOrder);
    if (renderOpaquePass == nullptr) {
        // Honest "no frame captured yet" empty result - see this function's
        // own header-comment contract and PHASE0's Locked Design Decision #2.
        return FrameDebuggerSnapshot{};
    }

    int nextEventIndex = 0;

    FrameDebuggerEventNode root;
    root.name = "Game View";
    root.isDrawCall = false;

    const int pivotIndex = static_cast<int>(renderOpaquePass - graphSnapshot.passesInExecutionOrder.data());

    // editor-core-separation-22 campaign, PHASE4
    // (PHASE4_FRAME_DEBUGGER_TREE_COMPLETE_COVERAGE_AND_CATEGORY_FIX.md,
    // Step 3.2) - tracks, by INDEX into graphSnapshot.passesInExecutionOrder,
    // every pass this function has already either (a) turned into a real
    // leaf somewhere, or (b) deliberately, honestly excluded for an
    // already-documented reason (culled, ViewScope::SceneView,
    // RenderPassCategory::FrameDebuggerInternal). Anything left `false` once
    // every existing bucket/walk below has run is a genuine coverage gap -
    // the final "Other Render Passes" sweep at the bottom of this function
    // picks up exactly those, guaranteeing NO surviving pass can ever be
    // silently dropped again, regardless of its RenderPassEvent tier or
    // position (this is the actual, structural fix for
    // "DemoRenderFeaturePlugin_Clear produces zero visible nodes" -
    // PHASE0_MASTER_STRATEGY.md's Step 2.2).
    std::vector<bool> claimed(graphSnapshot.passesInExecutionOrder.size(), false);

    // render-pass-7 campaign, PHASE4 (Core Campaign 1's own final step) -
    // this loop buckets each surviving pre-GameView compute pass by
    // whichever Frame-Debugger-heading TAG (if any) it carries, per the
    // generic gte::rg::RenderPassGroupRegistry (see RenderPassGroupRegistry.h,
    // PHASE2) - Core has NO knowledge of which specific Layer-2 module
    // registered which heading, or how many headings exist. One
    // FrameDebuggerEventNode bucket is built per CURRENTLY REGISTERED
    // (tag -> heading) pair, in REGISTRATION ORDER, before the loop even
    // starts; a pass whose tags match no registered heading falls into the
    // generic "Compute Dispatches (Pre-GameView)" fallback bucket instead.
    // Today exactly ONE heading ("Compute LUT") is registered in production,
    // by the Atmosphere feature's own AtmosphereLutRenderer constructor
    // (PHASE3) - which is why this loop's real, observed output is still
    // byte-identical to the old hardcoded two-bucket shape. A SINGLE forward
    // loop over [0, pivotIndex) still assigns eventIndex in true
    // chronological execution order (interleaved across every bucket,
    // exactly as before this phase) - only the TREE PRESENTATION order
    // (every registered heading, in registration order, THEN the generic
    // fallback bucket) is fixed, per the Locked Design Decision - never tied
    // to real interleaved execution order.
    //
    // editor-core-separation-22 campaign, PHASE4 - this loop ONLY EVER
    // inspects Compute-kind passes (unchanged) - a Graphics-kind pass sitting
    // in [0, pivotIndex) is NOT this loop's concern and is deliberately never
    // marked `claimed` here; it falls through to the final "Other Render
    // Passes" sweep instead (see that sweep's own comment at the bottom of
    // this function for why this is now correct instead of a silent drop).
    std::vector<FrameDebuggerEventNode> labeledGroups;
    labeledGroups.reserve(rg::PassGroupLabelCount());
    for (std::size_t g = 0; g < rg::PassGroupLabelCount(); ++g) {
        FrameDebuggerEventNode group;
        group.name = rg::PassGroupLabelUiHeadingAt(g);
        group.isDrawCall = false;
        labeledGroups.push_back(std::move(group));
    }

    FrameDebuggerEventNode preGameViewGroup;
    preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
    preGameViewGroup.isDrawCall = false;

    FrameDebuggerEventNode postGameViewGroup;
    postGameViewGroup.name = "Compute Dispatches (Post-GameView)";
    postGameViewGroup.isDrawCall = false;

    for (int i = 0; i < pivotIndex; ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        bool isComputeKind = false;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            isComputeKind = true;
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            break;
        }
        if (!isComputeKind) {
            continue; // Not this loop's concern - see this loop's own header comment above.
        }
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            claimed[static_cast<std::size_t>(i)] = true; // Honest, already-documented exclusion.
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;
        FrameDebuggerEventNode leaf =
            BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn);
        leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");
        // render-pass-7 campaign, PHASE4 - genuinely generic bucket lookup,
        // replacing the old hardcoded RenderPassCategory::AtmosphereLut
        // check (removed by PHASE3's own if(false) placeholder). Core never
        // learns which tag/heading belongs to which Layer-2 feature here.
        const std::optional<std::size_t> groupIndex = rg::FindPassGroupIndexForTags(pass.tags);
        if (groupIndex.has_value()) {
            labeledGroups[*groupIndex].children.push_back(std::move(leaf));
        } else {
            preGameViewGroup.children.push_back(std::move(leaf));
        }
    }

    // Only add a group at all if something real actually survived this frame
    // in THAT bucket - mirrors this tree's own "never an empty, misleading
    // group" rule, applied independently to each bucket. Registered headings
    // are appended first, in registration order, THEN the generic fallback -
    // identical presentation order to the old hardcoded
    // computeLutGroup-then-preGameViewGroup sequence.
    for (FrameDebuggerEventNode& group : labeledGroups) {
        if (!group.children.empty()) {
            root.children.push_back(std::move(group));
        }
    }
    if (!preGameViewGroup.children.empty()) {
        root.children.push_back(std::move(preGameViewGroup));
    }

    // Step 3.3 - the view-region walk: a flat, ordered sibling list of every
    // surviving, non-SceneView-scoped, non-FrameDebuggerInternal-category,
    // Graphics-kind pass starting at the "RenderOpaque" pivot, stopping at
    // the first surviving Compute-kind pass encountered (that pass, and
    // everything from there on, belongs to the "Compute Dispatches
    // (Post-GameView)" discovery below instead - in today's real engine this
    // is always the Aerial Perspective Composite pass, but this rule is
    // deliberately name-free). This is what makes "DrawSkyBackground" a real,
    // separate, individually selectable leaf (no more isSkyBackgroundDraw
    // hack), and is exactly what lets AddFrameDebuggerReplayPasses()'s own N
    // debug-only replay passes (real Graphics-kind, real ViewScope::GameView
    // passes sitting structurally inside this exact index range) be skipped
    // instead of leaking into the tree as spurious extra leaves - they are
    // tagged RenderPassCategory::FrameDebuggerInternal (editor-core-
    // separation-22 campaign, PHASE4 - RENAMED from the old, misused
    // RenderPassCategory::Debug value, which now correctly means "a real,
    // visible, optional/debug-flavored FEATURE pass" instead, and therefore
    // flows through this walk as a normal leaf - see RenderGraphTypes.h's own
    // updated doc comments) specifically so this walk's own
    // `category == FrameDebuggerInternal` guard actually excludes them.
    bool isRenderOpaqueLeaf = true;
    for (int i = pivotIndex; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];

        bool isComputeKind = false;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            isComputeKind = true;
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            break;
        }

        if (isComputeKind) {
            if (!pass.isCulled) {
                // A genuine surviving compute-pass survivor - stop the walk
                // entirely. Deliberately NOT marked `claimed` here - the
                // post-GameView compute loop below (which starts at
                // pivotIndex + 1) re-visits this exact index and claims it.
                break; // OUTSIDE the switch above - this correctly still breaks the for loop.
            }
            // A culled compute pass sitting inside the view region never
            // really ran this frame either way, and can never become a
            // Graphics leaf here - skip it and keep walking, mirroring this
            // whole tree's own "never show a culled pass as if it survived"
            // rule elsewhere.
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }

        // pass.kind is Graphics or Blit from here on - NEVER assume Graphics
        // (editor-core-separation-26 campaign, PHASE4 - the actual
        // correctness fix this whole audit exists for: a PassKind::Blit pass
        // must never be treated as, or silently displace, "RenderOpaque" -
        // see the `isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics`
        // guard below).
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView
            || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;

        // THE FIX: a Blit pass is NEVER treated as RenderOpaque, and -
        // critically - does NOT consume isRenderOpaqueLeaf when it isn't.
        // This is what lets the REAL RenderOpaque pass, whenever it is next
        // encountered, still correctly claim the slot even if one or more
        // Blit passes sat in front of it.
        if (isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics) {
            FrameDebuggerEventNode renderOpaqueLeaf = BuildRenderOpaqueLeaf(pass, capture, nextEventIndex++);
            // frame-debugger-6 campaign, PHASE4 - one real child leaf per
            // real per-draw attribution record captured this frame, indexed
            // strictly AFTER "RenderOpaque"'s own eventIndex and strictly
            // BEFORE "DrawSkyBackground"/anything else in the view region -
            // preserving the same "pre < RenderOpaque < post" monotonic-
            // eventIndex invariant this function has always documented,
            // simply retargeted at the new pass name.
            int perObjectStepIndex = 0;
            for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
                renderOpaqueLeaf.children.push_back(BuildRenderOpaqueDrawRecordLeaf(
                    record, capture.LastViewProjection(), nextEventIndex++, perObjectStepIndex));
                ++perObjectStepIndex;
            }
            root.children.push_back(std::move(renderOpaqueLeaf));
            isRenderOpaqueLeaf = false;
        } else {
            // "DrawSkyBackground" today; a real future "RenderTransparent" once
            // that pass ever actually exists. Frame Debugger Pass-Ownership
            // campaign (render-pass-2), PHASE2 - this pass now also gets a real
            // owned child event row, labeled by its own structural
            // rg::RenderPassDrawKind (never a pass-name string match) - this is
            // the actual fix for "DrawSkyBackground seems not owned by any
            // render-pass". editor-core-separation-26 campaign, PHASE4 - this
            // branch also now correctly covers a real PassKind::Blit pass (e.g.
            // a future "HistoryBufferBlit") - isRenderOpaqueLeaf is
            // DELIBERATELY NOT touched here, so a Blit pass sitting in front of
            // the real RenderOpaque pass can never consume its slot.
            FrameDebuggerEventNode leaf =
                BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite);
            leaf = WrapPassWithOwnedChildEvent(
                std::move(leaf), nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
            root.children.push_back(std::move(leaf));
        }
    }

    // task_manager/frame-debugger-7 campaign, PHASE4 - a Post-GameView leaf's
    // own "as of this step" image depends on whether it runs before or
    // at/after the real atmosphere composite pass - found STRUCTURALLY, by
    // matching each surviving pass's own real `writeNames` against the
    // "GameViewComposited" texture name, never by comparing against the
    // composite pass's own literal NAME.
    const int compositePassIndex = FindPostGameViewCompositePassExecutionIndex(graphSnapshot, pivotIndex);
    for (int i = pivotIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // frame-debugger-6 campaign, PHASE2 - identical rule, symmetrically
        // applied to the post-GameView half - e.g. the real
        // "AtmosphereAerialPerspectiveCompositePass" duplicate scenario.
        // editor-core-separation-22 campaign, PHASE4 - a Graphics-kind pass
        // encountered here (e.g. "DrawSkyBackground", already handled above
        // by the view-region walk) is simply skipped and deliberately NOT
        // claimed here either - a genuinely surviving Graphics-kind pass
        // positioned this far into execution order (e.g.
        // "DemoRenderFeaturePlugin_Clear", tagged
        // RenderPassEvent::AfterEverything) falls through to the final
        // "Other Render Passes" sweep below instead of being silently lost -
        // this is the actual structural fix for PHASE0_MASTER_STRATEGY.md's
        // Step 2.2.
        // editor-core-separation-26 campaign, PHASE4 - RE-VERIFIED, not just
        // cited: confirmed safe for `PassKind::Blit` too - this is a one-way
        // Compute-only FILTER (a Blit pass here is simply skipped, exactly like
        // a Graphics pass already is, and correctly falls through to the final
        // "Other Render Passes" sweep below). Not one of the 3 sites the source
        // document names for conversion - this is structurally identical to
        // FindPostGameViewCompositePassExecutionIndex()'s own already-safe check
        // above (Site 1) - see PHASE0_MASTER_STRATEGY.md's Locked Decision 2.
        if (pass.kind != rg::PassKind::Compute) {
            continue;
        }
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) {
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;
        // Strictly BEFORE the composite pass's own index -> PreComposite
        // (the accumulated image is still the pre-atmosphere-fog one); AT
        // the composite pass's own index (its own leaf) or AFTER it, or no
        // composite pass found at all this capture -> PostComposite.
        const FrameDebuggerStepPreviewKind stepPreviewKind = (compositePassIndex >= 0 && i < compositePassIndex)
            ? FrameDebuggerStepPreviewKind::PreComposite
            : FrameDebuggerStepPreviewKind::PostComposite;
        FrameDebuggerEventNode leaf = BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind);
        leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");
        postGameViewGroup.children.push_back(std::move(leaf));
    }
    if (!postGameViewGroup.children.empty()) {
        root.children.push_back(std::move(postGameViewGroup));
    }

    // editor-core-separation-22 campaign, PHASE4 (Step 3.2) - THE final,
    // generic "nothing survives silently dropped" sweep: any pass, at ANY
    // index, of EITHER PassKind, that none of the three loops above already
    // claimed (built into a leaf OR honestly, deliberately excluded) is a
    // genuine survivor this function used to just lose - most notably a
    // real, user-toggleable Graphics-kind pass positioned structurally AFTER
    // the first surviving Post-GameView compute pass (e.g.
    // "DemoRenderFeaturePlugin_Clear", tagged RenderPassEvent::AfterEverything
    // - PHASE0_MASTER_STRATEGY.md's Step 2.2), but this sweep is deliberately
    // NOT special-cased to only that one shape - it is a genuine, structural,
    // name-free, "any index, any kind" catch-all, so a FUTURE pass positioned
    // anywhere else this two-loop-plus-walk shape does not anticipate is
    // guaranteed a leaf too, by construction, with zero further code changes
    // required (this phase's own Step 1 goal #3). `stepPreviewKind` is
    // resolved the same STRUCTURAL way the post-GameView compute loop above
    // already does (pivotIndex/compositePassIndex comparison), extended one
    // step further back (a swept-up pass positioned BEFORE the pivot itself
    // gets NotYetDrawn, since "RenderOpaque" has not drawn anything yet at
    // that point) - never a fixed constant, since a swept-up pass could
    // genuinely sit anywhere in execution order relative to both boundaries.
    FrameDebuggerEventNode otherPassesGroup;
    otherPassesGroup.name = "Other Render Passes";
    otherPassesGroup.isDrawCall = false;
    for (std::size_t i = 0; i < graphSnapshot.passesInExecutionOrder.size(); ++i) {
        if (claimed[i]) {
            continue;
        }
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[i];
        if (pass.isCulled || pass.viewScope == rg::ViewScope::SceneView
            || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            continue; // Defensive only - every real case is already caught by `claimed` above.
        }
        const int index = static_cast<int>(i);
        const FrameDebuggerStepPreviewKind stepPreviewKind = (index < pivotIndex)
            ? FrameDebuggerStepPreviewKind::NotYetDrawn
            : ((compositePassIndex >= 0 && index < compositePassIndex) ? FrameDebuggerStepPreviewKind::PreComposite
                                                                        : FrameDebuggerStepPreviewKind::PostComposite);
        // editor-core-separation-26 campaign, PHASE4 - converted to a real,
        // exhaustive, compiler-enforced 3-way dispatch (one of the 3 sites the
        // source document names) - defensively future-proofed even though this
        // ternary's own "else" arm was already functionally correct for
        // PassKind::Blit today (BuildGraphicsPassLeaf()/GraphicsChildEventLabelFor()
        // already handle it).
        FrameDebuggerEventNode leaf;
        switch (pass.kind) {
        case rg::PassKind::Compute:
            leaf = WrapPassWithOwnedChildEvent(
                BuildComputeDispatchLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
                "Compute Dispatch");
            break;
        case rg::PassKind::Graphics:
        case rg::PassKind::Blit:
            leaf = WrapPassWithOwnedChildEvent(
                BuildGraphicsPassLeaf(pass, nextEventIndex++, stepPreviewKind), nextEventIndex++,
                GraphicsChildEventLabelFor(pass.drawKind));
            break;
        }
        otherPassesGroup.children.push_back(std::move(leaf));
    }
    if (!otherPassesGroup.children.empty()) {
        root.children.push_back(std::move(otherPassesGroup));
    }

    FrameDebuggerSnapshot snapshot;
    snapshot.rootNodes.push_back(std::move(root));
    snapshot.totalEventCount = nextEventIndex;
    snapshot.renderTarget = gameViewRenderTargetInfo;
    // "GameView" here is the RenderTexture/resource name
    // (Application.cpp's own `b.ImportTexture("GameView", ...)` call) -
    // completely unrelated to, and unaffected by, this campaign's pass-name
    // changes (PHASE0's own Step 2 point 4 draws this exact distinction).
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
