#include "FrameDebuggerData.h"

#include "EditorViewNames.h"
#include "RenderGraphDisplayText.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <unordered_map>

namespace gte {

std::string FormatFrameStepperLabel(int currentEventIndex, int totalEventCount)
{
    if (totalEventCount <= 0) {
        return "0 of 0";
    }
    // currentEventIndex is 0-based; the label is 1-based.
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

// Shortest "%g" form of a float, e.g. "1.000000" -> "1".
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

// Must match the clear color in Application/RenderPasses.cpp (Editor cannot include Application headers).
constexpr float kFrameDebuggerGameClearColor[4] = { 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };

// The first pass of the Game view region, in execution order, whose event is Opaques or later. Matched by event
// and view, never by pass name; a Scene view pass never becomes the pivot.
const rg::RenderGraphPassSnapshot* FindViewRegionPivot(
    const std::vector<rg::RenderGraphPassSnapshot>& passesInExecutionOrder)
{
    for (const rg::RenderGraphPassSnapshot& pass : passesInExecutionOrder) {
        if (pass.renderPassEvent >= rg::RenderPassEvent::Opaques && pass.viewScope != rg::ViewScope::Secondary) {
            return &pass;
        }
    }
    return nullptr;
}

// Execution-order index, among the surviving Post-GameView compute passes, of the pass that writes the
// "GameViewComposited" texture (matched by written resource name, not pass name). Returns -1 when none writes it
// this frame; every Post-GameView leaf is then treated as PostComposite.
int FindPostGameViewCompositePassExecutionIndex(const rg::RenderGraphSnapshot& graphSnapshot, int gameViewIndex)
{
    for (int i = gameViewIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // Compute-only filter: other pass kinds are skipped.
        if (pass.kind != rg::PassKind::Compute || pass.isCulled || pass.viewScope == rg::ViewScope::Secondary) {
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

// Row labels for compute read/write rows. No default case: a new resource kind fails to compile here.
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

// Makes `passLevelNode` the parent of one child row that names the GPU operation. The child copies the pass
// facts and takes `childEventIndex`, which must be the next event index after the parent's.
FrameDebuggerEventNode WrapPassWithOwnedChildEvent(
    FrameDebuggerEventNode passLevelNode, int childEventIndex, const std::string& childEventLabel)
{
    FrameDebuggerEventNode child = passLevelNode; // Same pass facts, so both rows stay selectable.
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

// Child row label of a graphics pass, taken from its structural draw kind (never from the pass name).
// No default case: a new draw kind fails to compile here until handled.
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

// Leaf for any compute dispatch, whatever its name. A dispatch issues no draw, so blend/depth/stencil read "n/a".
// `stepPreviewKind` is decided by the caller from the pass position.
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
    // The raw pass name is also the shader name: the real shader file is unknown here.
    details.passName = pass.name;
    details.shaderName = pass.name;

    // Pass-private writes use the live debug texture; the shared view buffer uses the step preview.
    if (!pass.writeNames.empty() && pass.writeNames.front() != kGameViewResourceName) {
        details.privateWriteTextureName = pass.writeNames.front();
    }

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

    // Rows are labeled by real resource kind, so a buffer write is never shown as a texture.
    // Each row is a render-graph resource, which gives it a "View" button.
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

    // GPU time only: a compute dispatch reports no draw stats. A pass that was not measured shows no number.
    const GpuPassRecord& gpu = pass.stats.gpu;
    if (gpu.state == GpuReadState::Present) {
        FrameDebuggerVectorProperty timing;
        timing.name = "GPU Time (ms)";
        timing.x = gpu.barrierMs + gpu.workMs;
        details.vectors.push_back(timing);
    }
    details.gpuTimingText = FormatGpuTiming(gpu);

    // No matrices: a compute dispatch uses no camera matrix.

    leaf.details = std::move(details);
    return leaf;
}

// One Frame Debugger row for a material slot: "<slot> Texture" with the image name or "None".
FrameDebuggerTextureProperty MakeMaterialSlotRow(const FrameDebuggerMaterialSlot& slot)
{
    FrameDebuggerTextureProperty texture;
    texture.name = slot.slot + " Texture";
    texture.valueLabel = slot.imageName.empty() ? "None" : slot.imageName;
    return texture;
}

// Builds the "RenderOpaque" pass leaf. Per-entity children are attached by the caller.
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
    // This leaf always shows the pre-composite image.
    details.stepPreviewKind = FrameDebuggerStepPreviewKind::PreComposite;

    // Every distinct pipeline debug name recorded this frame, comma-joined. Empty when nothing was drawn.
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

    // One row per distinct bound material image.
    for (const FrameDebuggerMaterialSlot& slot : capture.MaterialSlotRows()) {
        details.textures.push_back(MakeMaterialSlotRow(slot));
    }

    // Clear color, plus the pass's own draw-call and triangle totals from the graph snapshot.
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

        details.gpuTimingText = FormatGpuTiming(renderOpaquePass.stats.gpu);
        details.indirectDraws = renderOpaquePass.stats.gpu.indirectDraws;
    }

    // The view-projection matrix the pass rendered with.
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

    // Blend, depth and stencil rows: the engine's single constant pipeline state.
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

// Builds one per-entity draw leaf under the "RenderOpaque" pass.
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

    for (const FrameDebuggerMaterialSlot& slot : record.materialSlots) {
        details.textures.push_back(MakeMaterialSlotRow(slot));
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

// Leaf for a graphics pass other than the opaque pass. The raw pass name is also passName and shaderName.
// The sky background pass uses its own pipeline state; any other pass uses the standard one.
FrameDebuggerEventNode BuildGraphicsPassLeaf(
    const rg::RenderGraphPassSnapshot& pass, int eventIndex, FrameDebuggerStepPreviewKind stepPreviewKind)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name; // The real, raw render-graph pass name - never fabricated/prettified.
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    // Same label as the owned child row, so the inspector header reads the same for both rows.
    details.eventLabel = GraphicsChildEventLabelFor(pass.drawKind);
    details.stepPreviewKind = stepPreviewKind;
    details.passName = pass.name;
    details.shaderName = pass.name;

    // Pass-private writes use the live debug texture; the shared view buffer uses the step preview.
    if (!pass.writeNames.empty() && pass.writeNames.front() != kGameViewResourceName) {
        details.privateWriteTextureName = pass.writeNames.front();
    }

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

    // Draw stats come from the pass's own graph entry.
    FrameDebuggerVectorProperty drawStats;
    drawStats.name = "Draw Stats (Calls, Tris)";
    drawStats.x = static_cast<float>(pass.stats.drawStats.drawCallCount);
    drawStats.y = static_cast<float>(pass.stats.drawStats.triangleCount);
    details.vectors.push_back(drawStats);
    details.gpuTimingText = FormatGpuTiming(pass.stats.gpu);
    details.indirectDraws = pass.stats.gpu.indirectDraws;

    leaf.details = std::move(details);
    return leaf;
}

} // namespace

FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture, const FrameDebuggerRenderTargetInfo& gameViewRenderTargetInfo)
{
    // The first pass of the Game view region splits the compute passes into before and after groups.
    const rg::RenderGraphPassSnapshot* renderOpaquePass =
        FindViewRegionPivot(graphSnapshot.passesInExecutionOrder);
    if (renderOpaquePass == nullptr) {
        return FrameDebuggerSnapshot{};
    }

    int nextEventIndex = 0;

    FrameDebuggerEventNode root;
    root.name = "Game View";
    root.isDrawCall = false;

    const int pivotIndex = static_cast<int>(renderOpaquePass - graphSnapshot.passesInExecutionOrder.data());

    // Passes already turned into a leaf or deliberately excluded. The final sweep picks up the rest.
    std::vector<bool> claimed(graphSnapshot.passesInExecutionOrder.size(), false);

    // Compute passes before the pivot, grouped by owning feature in first-seen order.
    // Passes without an owner go to the generic group.
    std::vector<FrameDebuggerEventNode> labeledGroups;
    std::unordered_map<std::string, std::size_t> labeledGroupIndexByOwner;

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
            continue; // Non-compute passes before the pivot reach the final sweep.
        }
        claimed[static_cast<std::size_t>(i)] = true;
        if (pass.isCulled || pass.viewScope == rg::ViewScope::Secondary) {
            continue;
        }
        FrameDebuggerEventNode leaf =
            BuildComputeDispatchLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::NotYetDrawn);
        leaf = WrapPassWithOwnedChildEvent(std::move(leaf), nextEventIndex++, "Compute Dispatch");
        if (!pass.owningFeatureName.empty() && pass.owningFeatureName != "ENGINE_UNOWNED") {
            const auto existing = labeledGroupIndexByOwner.find(pass.owningFeatureName);
            std::size_t groupIndex;
            if (existing == labeledGroupIndexByOwner.end()) {
                FrameDebuggerEventNode group;
                group.name = pass.owningFeatureName;
                group.isDrawCall = false;
                groupIndex = labeledGroups.size();
                labeledGroups.push_back(std::move(group));
                labeledGroupIndexByOwner.emplace(pass.owningFeatureName, groupIndex);
            } else {
                groupIndex = existing->second;
            }
            labeledGroups[groupIndex].children.push_back(std::move(leaf));
        } else {
            preGameViewGroup.children.push_back(std::move(leaf));
        }
    }

    // A group is added only when it has a child. Owner groups come first, then the generic group.
    for (FrameDebuggerEventNode& group : labeledGroups) {
        if (!group.children.empty()) {
            root.children.push_back(std::move(group));
        }
    }
    if (!preGameViewGroup.children.empty()) {
        root.children.push_back(std::move(preGameViewGroup));
    }

    // View region: graphics passes from the pivot until the first surviving compute pass.
    // Replay passes are tagged FrameDebuggerInternal and skipped.
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
                break; // Left unclaimed: the post-pivot compute loop claims it.
            }
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }

        if (pass.isCulled || pass.viewScope == rg::ViewScope::Secondary
            || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            claimed[static_cast<std::size_t>(i)] = true;
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;

        // A blit pass never takes the RenderOpaque slot and never consumes it.
        if (isRenderOpaqueLeaf && pass.kind == rg::PassKind::Graphics) {
            FrameDebuggerEventNode renderOpaqueLeaf = BuildRenderOpaqueLeaf(pass, capture, nextEventIndex++);
            // Per-entity children follow the pass row in event order.
            int perObjectStepIndex = 0;
            for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
                renderOpaqueLeaf.children.push_back(BuildRenderOpaqueDrawRecordLeaf(
                    record, capture.LastViewProjection(), nextEventIndex++, perObjectStepIndex));
                ++perObjectStepIndex;
            }
            root.children.push_back(std::move(renderOpaqueLeaf));
            isRenderOpaqueLeaf = false;
        } else {
            FrameDebuggerEventNode leaf =
                BuildGraphicsPassLeaf(pass, nextEventIndex++, FrameDebuggerStepPreviewKind::PreComposite);
            leaf = WrapPassWithOwnedChildEvent(
                std::move(leaf), nextEventIndex++, GraphicsChildEventLabelFor(pass.drawKind));
            root.children.push_back(std::move(leaf));
        }
    }

    // Post-pivot compute passes. The composite pass is found by the texture it writes, not by name.
    const int compositePassIndex = FindPostGameViewCompositePassExecutionIndex(graphSnapshot, pivotIndex);
    for (int i = pivotIndex + 1; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
        // Non-compute passes are left unclaimed for the final sweep.
        if (pass.kind != rg::PassKind::Compute) {
            continue;
        }
        claimed[static_cast<std::size_t>(i)] = true;
        if (pass.isCulled || pass.viewScope == rg::ViewScope::Secondary) {
            continue;
        }
        // Before the composite pass the image has no fog yet: PreComposite. At or after it: PostComposite.
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

    // Final sweep: any surviving pass no loop above claimed still gets a row.
    FrameDebuggerEventNode otherPassesGroup;
    otherPassesGroup.name = "Other Render Passes";
    otherPassesGroup.isDrawCall = false;
    for (std::size_t i = 0; i < graphSnapshot.passesInExecutionOrder.size(); ++i) {
        if (claimed[i]) {
            continue;
        }
        const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[i];
        if (pass.isCulled || pass.viewScope == rg::ViewScope::Secondary
            || pass.category == rg::RenderPassCategory::FrameDebuggerInternal) {
            continue;
        }
        const int index = static_cast<int>(i);
        const FrameDebuggerStepPreviewKind stepPreviewKind = (index < pivotIndex)
            ? FrameDebuggerStepPreviewKind::NotYetDrawn
            : ((compositePassIndex >= 0 && index < compositePassIndex) ? FrameDebuggerStepPreviewKind::PreComposite
                                                                        : FrameDebuggerStepPreviewKind::PostComposite);
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
    snapshot.renderTarget.name = kGameViewResourceName;
    return snapshot;
}

FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(bool hasEntry,
    FrameDebuggerStepPreviewKind stepPreviewKind, bool hasPreview, bool hasCompositedPreview,
    bool hasPerObjectStepPreviewAtIndex)
{
    if (!hasEntry) {
        return FrameDebuggerPreviewSourceChoice::None;
    }
    // No default case: a new step kind fails to compile here until handled.
    switch (stepPreviewKind) {
    case FrameDebuggerStepPreviewKind::NotYetDrawn:
        return FrameDebuggerPreviewSourceChoice::NotYetDrawn;
    case FrameDebuggerStepPreviewKind::PerObjectStep:
        // Never falls back to a whole-frame image: it would show later draws that had not happened yet.
        return hasPerObjectStepPreviewAtIndex ? FrameDebuggerPreviewSourceChoice::PerObjectStepPreview
                                               : FrameDebuggerPreviewSourceChoice::None;
    case FrameDebuggerStepPreviewKind::PreComposite:
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    case FrameDebuggerStepPreviewKind::PostComposite:
        if (hasCompositedPreview) {
            return FrameDebuggerPreviewSourceChoice::CompositedPreview;
        }
        return hasPreview ? FrameDebuggerPreviewSourceChoice::Preview : FrameDebuggerPreviewSourceChoice::None;
    }
    return FrameDebuggerPreviewSourceChoice::None;
}

} // namespace gte
