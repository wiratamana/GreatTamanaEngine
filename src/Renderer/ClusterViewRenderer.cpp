#include "ClusterViewRenderer.h"
#include "ClusterViewRendererImpl.h"

#include "ShaderFile.h"
#include "VulkanFormatInfo.h"
#include "RenderGraph/ComputePass.h"
#include "RenderGraph/PassKindDefaults.h"
#include "RenderGraph/RecordedPass.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "../Assets/MeshClusterValidation.h"
#include "../Core/Logging.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace gte {

namespace {

constexpr const char* kLogCategory = "ClusterView";

// Static names of the buffers and passes of each cull set, in Impl::SetId order.
struct SetNames {
    const char* visible;
    const char* commands;
    const char* count;
    const char* countHost;
    const char* commandsHost;
    const char* visibleHost;
    const char* reset;
    const char* cull;
    const char* readCount;
    const char* readCommands;
    const char* readVisible;
};

constexpr SetNames kSetNames[] = {
    { "ClusterViewMainVisible", "ClusterViewMainCommands", "ClusterViewMainCount", "ClusterViewMainCountHost",
        "ClusterViewMainCommandsHost", "ClusterViewMainVisibleHost", "ClusterView.Main.Reset", "ClusterView.Main.Cull",
        "ClusterView.Main.ReadCount", "ClusterView.Main.ReadCommands", "ClusterView.Main.ReadVisible" },
    { "ClusterViewPreviewVisible", "ClusterViewPreviewCommands", "ClusterViewPreviewCount",
        "ClusterViewPreviewCountHost", "ClusterViewPreviewCommandsHost", "ClusterViewPreviewVisibleHost",
        "ClusterView.Preview.Reset", "ClusterView.Preview.Cull", "ClusterView.Preview.ReadCount",
        "ClusterView.Preview.ReadCommands", "ClusterView.Preview.ReadVisible" },
    { "ClusterViewStageAVisible", "ClusterViewStageACommands", "ClusterViewStageACount", "ClusterViewStageACountHost",
        "", "", "ClusterView.StageA.Reset", "ClusterView.StageA.Cull", "ClusterView.StageA.ReadCount", "", "" },
    { "ClusterViewStageBVisible", "ClusterViewStageBCommands", "ClusterViewStageBCount", "ClusterViewStageBCountHost",
        "", "", "ClusterView.StageB.Reset", "ClusterView.StageB.Cull", "ClusterView.StageB.ReadCount", "", "" },
    { "ClusterViewGhostVisible", "ClusterViewGhostCommands", "ClusterViewGhostCount", "", "", "",
        "ClusterView.Ghost.Reset", "ClusterView.Ghost.Cull", "", "", "" },
};

constexpr const char* kDrawNames[2] = { "ClusterView.Main.Draw", "ClusterView.Preview.Draw" };
constexpr const char* kGhostDrawNames[2] = { "ClusterView.Main.DrawGhost", "ClusterView.Preview.DrawGhost" };

constexpr std::uint32_t kCountBytes = sizeof(std::uint32_t);

// Capacity in bytes for `items` records, never below the engine's minimum buffer size.
std::uint64_t BufferBytes(std::uint64_t bytesPerItem, std::uint32_t items) noexcept
{
    return std::max<std::uint64_t>(kClusterMinBufferBytes, bytesPerItem * items);
}

std::array<float, 4> ToClearColor(const std::array<std::uint8_t, 4>& rgba) noexcept
{
    return { rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, rgba[3] / 255.0f };
}

} // namespace

ClusterViewRenderer::Impl::Impl(Renderer& owner)
    : renderer(owner)
    , mainSlot(owner)
    , previewSlot(owner)
    , idSlot(owner)
    , bindings(owner)
    , graph(owner, rg::TimingMode::Disabled)
{
}

void ClusterViewRenderer::Impl::Build()
{
    static_assert(std::size(kSetNames) == kSetCount, "one name block per cull set");
    const IndirectDrawCapsView caps = renderer.IndirectDrawCaps();
    const ClusterLimits limits = ClusterLimitsFor(caps);
    maxClusters = static_cast<std::uint32_t>(
        std::min<std::uint64_t>({ kViewerMaxClusters, limits.maxWorkItems, limits.maxClusters }));

    maxImageDimension = renderer.MaxImageDimension2D();

    vertexSpirv = ReadShaderFileBytes("shaders/MeshCluster.vert.spv");
    fragmentSpirv[static_cast<std::size_t>(ClusterViewFragment::Clay)] = ReadShaderFileBytes("shaders/ClusterViewClay.frag.spv");
    fragmentSpirv[static_cast<std::size_t>(ClusterViewFragment::Tint)] = ReadShaderFileBytes("shaders/ClusterViewTint.frag.spv");
    fragmentSpirv[static_cast<std::size_t>(ClusterViewFragment::Dim)] = ReadShaderFileBytes("shaders/ClusterViewDim.frag.spv");
    fragmentSpirv[static_cast<std::size_t>(ClusterViewFragment::Id)] = ReadShaderFileBytes("shaders/ClusterViewId.frag.spv");
    // The compute shaders are built by the graph on first use; a missing file is caught here.
    (void)ReadShaderFileBytes("shaders/ClusterCull.comp.spv");
    (void)ReadShaderFileBytes("shaders/HizBuildBase.comp.spv");
    (void)ReadShaderFileBytes("shaders/HizBuildTop.comp.spv");

    // Survivor draw and ghost draw of one run each own a set.
    if (!bindings.Prepare(2)) {
        throw std::runtime_error("the descriptor sets could not be allocated");
    }
    hizStandIn.emplace(renderer.CreateRenderTexture(1, 1, PixelFormat::R32_Float, "ClusterViewHizStandIn", nullptr,
        /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/false));
    historyStandIn = rg::GpuBuffer(renderer.Device(), kClusterMinBufferBytes, "ClusterViewHistoryStandIn",
        rg::GpuBufferUse::Storage);
}

void ClusterViewRenderer::Impl::LogOnce(const std::string& message)
{
    if (message != lastLogged) {
        lastLogged = message;
        GTE_LOG_ERROR(kLogCategory, message);
    }
}

bool ClusterViewRenderer::Impl::CheckSize(int width, int height, std::string& note) const
{
    const auto limit = static_cast<int>(std::min<std::uint32_t>(maxImageDimension, std::numeric_limits<int>::max()));
    if (width < 1 || height < 1 || width > limit || height > limit) {
        note = "the image size " + std::to_string(width) + "x" + std::to_string(height) + " is outside 1.."
            + std::to_string(limit);
        return false;
    }
    return true;
}

bool ClusterViewRenderer::Impl::CheckCullCamera(const ClusterViewCull& cull, std::string& note) const
{
    if (!CameraIsFinite(cull.viewProjection, cull.eye)) {
        note = "the cull camera holds a non-finite value";
        return false;
    }
    if (cull.flags != ClusterCullFlags::None && IsOrthographicProjection(cull.viewProjection)) {
        note = "the cull camera must be a perspective camera";
        return false;
    }
    if (!ClusterCullParamsAreFinite(MakeClusterCullParams(cull.viewProjection, cull.eye, false, 1, 1, cull.flags))) {
        note = "the cull camera gives non-finite frustum planes";
        return false;
    }
    return true;
}

bool ClusterViewRenderer::Impl::ReleaseMesh()
{
    if (!DrainGpu("cluster viewer mesh release")) {
        return false; // The GPU may still read them; they stay alive.
    }
    for (CullSet& set : sets) {
        set = CullSet{};
    }
    ReleaseSlot(idSlot);
    hizInfo.reset();
    hizAtlas.reset();
    hizDepth.reset();
    instance.reset();
    table.reset();
    mesh.reset();
    tableClusters = 0;
    realClusters = 0;
    return true;
}

bool ClusterViewRenderer::Impl::EnsureTarget(
    SlotTarget& slot, const char* name, const char* depthName, int width, int height)
{
    if (slot.texture.has_value() && slot.width == width && slot.height == height) {
        return true;
    }
    if (!slot.texture.has_value()) {
        // A fresh target was never used by the GPU, so a failed creation may reset the slot.
        try {
            slot.texture.emplace(renderer.CreateRenderTexture(width, height, PixelFormat::RGBA8_UNorm, name, depthName));
            slot.width = width;
            slot.height = height;
            slot.ui.Set(renderer.GetUiTextureHandle(*slot.texture));
            return true;
        } catch (const std::exception& e) {
            slot.ui.Reset();
            slot.texture.reset();
            slot.width = 0;
            slot.height = 0;
            LogOnce(std::string("the image target could not be made: ") + e.what());
            return false;
        }
    }
    // A used target may still be sampled: resize only on an idle GPU and never destroy it on failure.
    if (!DrainGpu("cluster viewer target resize")) {
        LogOnce("the image target needs an idle GPU to resize");
        return false;
    }
    try {
        renderer.ResizeRenderTexture(*slot.texture, width, height);
        slot.width = width;
        slot.height = height;
        slot.ui.Set(renderer.GetUiTextureHandle(*slot.texture));
        return true;
    } catch (const std::exception& e) {
        LogOnce(std::string("the image target could not be resized: ") + e.what());
        return false;
    }
}

void ClusterViewRenderer::Impl::ReleaseSlot(SlotTarget& slot)
{
    if (!slot.texture.has_value()) {
        return;
    }
    if (!DrainGpu("cluster viewer target release")) {
        return; // The GPU may still sample it; the destructor frees it with the device.
    }
    slot.ui.Reset();
    slot.texture.reset();
    slot.width = 0;
    slot.height = 0;
}

bool ClusterViewRenderer::Impl::EnsureSet(SetId id, bool wantMirrors, bool wantCount)
{
    CullSet& current = Set(id);
    if (current.visible.IsValid() && current.capacity == tableClusters && (current.mirrors || !wantMirrors)
        && (current.countHost.IsValid() || !wantCount)) {
        return true;
    }
    const SetNames& names = kSetNames[static_cast<std::size_t>(id)];
    try {
        const rg::DeviceRef device = renderer.Device();
        constexpr rg::GpuBufferUse kHostRead = rg::GpuBufferUse::HostRead | rg::GpuBufferUse::TransferDst;
        const std::uint64_t visibleBytes = BufferBytes(kClusterVisibleBytes, tableClusters);
        const std::uint64_t commandBytes = BufferBytes(kClusterCommandBytes, tableClusters);

        CullSet fresh;
        fresh.visible = rg::GpuBuffer(device, visibleBytes, names.visible,
            rg::GpuBufferUse::Storage | rg::GpuBufferUse::TransferSrc);
        fresh.commands = rg::GpuBuffer(device, commandBytes, names.commands,
            rg::GpuBufferUse::Storage | rg::GpuBufferUse::Indirect | rg::GpuBufferUse::TransferSrc);
        fresh.count = rg::GpuBuffer(device, kClusterMinBufferBytes, names.count,
            rg::GpuBufferUse::Storage | rg::GpuBufferUse::Indirect | rg::GpuBufferUse::TransferSrc);
        if (wantCount) {
            fresh.countHost = rg::GpuBuffer(device, kClusterMinBufferBytes, names.countHost, kHostRead);
        }
        if (wantMirrors) {
            fresh.commandsHost = rg::GpuBuffer(device, commandBytes, names.commandsHost, kHostRead);
            fresh.visibleHost = rg::GpuBuffer(device, visibleBytes, names.visibleHost, kHostRead);
        }
        fresh.capacity = tableClusters;
        fresh.mirrors = wantMirrors;
        fresh.params = std::move(current.params);
        if (!DrainGpu("cluster viewer cull buffers")) { // The old buffers may still be bound by the last run.
            LogOnce("the cull buffers need an idle GPU");
            return false;
        }
        current = std::move(fresh);
        return true;
    } catch (const std::exception& e) {
        LogOnce(std::string("the cull buffers could not be allocated: ") + e.what());
        return false;
    }
}

ClusterViewRenderer::Impl::GraphSet ClusterViewRenderer::Impl::ImportSet(rg::RenderGraphBuilder& builder, SetId id)
{
    CullSet& set = Set(id);
    GraphSet graphSet;
    graphSet.id = id;
    graphSet.visible = builder.Import(set.visible);
    graphSet.commands = builder.Import(set.commands);
    graphSet.count = builder.Import(set.count);
    if (set.countHost.IsValid()) {
        graphSet.countHost = builder.Import(set.countHost);
    }
    if (set.mirrors) {
        graphSet.commandsHost = builder.Import(set.commandsHost);
        graphSet.visibleHost = builder.Import(set.visibleHost);
    }
    return graphSet;
}

// The one place that decides whether the Hi-Z test runs: a capture must exist and be taken at exactly the cull pose.
ClusterViewRenderer::Impl::AppliedCull ClusterViewRenderer::Impl::ResolveFlags(const ClusterViewCull& cull) const
{
    AppliedCull applied;
    applied.flags = cull.flags;
    if (!HasClusterCullFlag(cull.flags, ClusterCullFlags::Occlusion)) {
        return applied;
    }
    const bool haveCapture = hizInfo.has_value() && hizAtlas.has_value()
        && HizLayoutFor(static_cast<std::uint32_t>(hizInfo->width), static_cast<std::uint32_t>(hizInfo->height)).IsValid();
    const bool samePose = haveCapture
        && CameraPoseEqual(hizInfo->viewProjection, hizInfo->eye, cull.viewProjection, cull.eye);
    applied.occlusionApplied = samePose;
    applied.hizCut = haveCapture && !samePose;
    if (!applied.occlusionApplied) {
        applied.flags = static_cast<ClusterCullFlags>(
            static_cast<std::uint32_t>(cull.flags) & ~static_cast<std::uint32_t>(ClusterCullFlags::Occlusion));
    }
    return applied;
}

ClusterCullParams ClusterViewRenderer::Impl::MakeParams(
    const ClusterViewCull& cull, ClusterCullFlags flags, bool useHiz) const
{
    ClusterCullParams params = MakeClusterCullParams(cull.viewProjection, cull.eye, false, 1, tableClusters, flags);
    if (useHiz && hizInfo.has_value()) {
        SetClusterCullHiz(params,
            HizLayoutFor(static_cast<std::uint32_t>(hizInfo->width), static_cast<std::uint32_t>(hizInfo->height)));
    }
    return params;
}

rg::TextureHandle ClusterViewRenderer::Impl::ImportHizAtlas(rg::RenderGraphBuilder& builder, bool useHiz)
{
    const bool real = useHiz && hizAtlas.has_value();
    RenderTexture& texture = real ? *hizAtlas : *hizStandIn;
    return builder.ImportTexture(real ? "ClusterViewHizAtlas" : "ClusterViewHizStandIn", texture.Target(),
        FromVkImageLayout(texture.CurrentState().layout), texture.SamplerObjectHandle(), SamplerHandle::Invalid,
        &texture);
}

rg::BufferHandle ClusterViewRenderer::Impl::ImportHistoryStandIn(rg::RenderGraphBuilder& builder)
{
    return builder.Import(historyStandIn);
}

bool ClusterViewRenderer::Impl::DeclareFill(rg::RenderGraphBuilder& builder, const char* passName, rg::BufferHandle buffer)
{
    std::optional<rg::PassBody> body =
        rg::MakeTransferBody(builder, passName, rg::TransferRecord::Fill(buffer, 0, 0, kCountBytes));
    if (!body) {
        return false;
    }
    builder.AddRenderPass(passName, rg::DefaultPassKind(rg::DeclarativePassType::Transfer), rg::ViewScope::Shared,
        rg::RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
        rg::DefaultDrawKind(rg::DeclarativePassType::Transfer), rg::RenderPassEvent::Opaques);
    return true;
}

bool ClusterViewRenderer::Impl::DeclareReadback(rg::RenderGraphBuilder& builder, const char* passName,
    rg::BufferHandle device, rg::BufferHandle host, std::uint64_t bytes)
{
    std::optional<rg::PassBody> body =
        rg::MakeTransferBody(builder, passName, rg::TransferRecord::Readback(device, host, bytes));
    if (!body) {
        return false;
    }
    builder.KeepBufferOutput(host);
    builder.AddRenderPass(passName, rg::DefaultPassKind(rg::DeclarativePassType::Transfer), rg::ViewScope::Shared,
        rg::RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
        rg::DefaultDrawKind(rg::DeclarativePassType::Transfer), rg::RenderPassEvent::Opaques);
    return true;
}

bool ClusterViewRenderer::Impl::DeclareCull(rg::RenderGraphBuilder& builder, const GraphSet& set,
    rg::BufferHandle instances, rg::BufferHandle clusters, rg::BufferHandle history, rg::TextureHandle hizAtlasHandle,
    const ClusterCullParams& params)
{
    const SetNames& names = kSetNames[static_cast<std::size_t>(set.id)];
    return rg::ComputePass(builder, cullShader, rg::StaticName::Interned(names.cull), rg::ViewScope::Shared,
        rg::RenderPassEvent::Opaques)
        .Params(Set(set.id).params, params)
        .Bind("instances", instances)
        .Bind("clusters", clusters)
        .Bind("visible", set.visible)
        .Bind("commands", set.commands)
        .Bind("count", set.count)
        .Bind("history", history)
        .Bind("hizAtlas", hizAtlasHandle)
        .Dispatch(Extent2D{ tableClusters, 1 });
}

bool ClusterViewRenderer::Impl::DeclareDraw(rg::RenderGraphBuilder& builder, const char* passName, const GraphSet& set,
    rg::BufferHandle instances, rg::TextureHandle target, bool depthOnly, const Pipeline& pipeline,
    const Mat4& viewProjection, std::size_t bindingIndex, std::optional<std::array<float, 4>> clearColor,
    std::optional<float> clearDepth)
{
    rg::IndirectDrawRecord record;
    record.target = target;
    record.indirect = set.commands;
    record.count = set.count;
    record.instances = instances;
    record.visible = set.visible;
    record.maxDrawCount = tableClusters;
    record.depthOnly = depthOnly;
    record.clearColor = clearColor;
    record.clearDepth = clearDepth;

    const Mesh* drawMesh = &*mesh;
    const Pipeline* drawPipeline = &pipeline;
    rg::ClusterDrawBindings* drawBindings = &bindings;
    auto resolveInputs = [drawMesh, drawPipeline, drawBindings, bindingIndex, viewProjection]() {
        rg::IndirectDrawInputs inputs;
        inputs.mesh = drawMesh;
        inputs.pipeline = drawPipeline;
        inputs.viewProjection = viewProjection;
        inputs.clusterBindings = drawBindings;
        inputs.clusterBindingIndex = bindingIndex;
        return inputs;
    };

    std::optional<rg::PassBody> body = rg::MakeIndirectDrawBody(builder, passName, record, std::move(resolveInputs));
    if (!body) {
        return false;
    }
    builder.AddRenderPass(passName, rg::DefaultPassKind(rg::DeclarativePassType::IndirectDraw), rg::ViewScope::Shared,
        rg::RenderPassCategory::General, std::move(body->setup), std::move(body->execute),
        rg::DefaultDrawKind(rg::DeclarativePassType::IndirectDraw), rg::RenderPassEvent::Opaques);
    return true;
}

const Pipeline* ClusterViewRenderer::Impl::ViewPipeline(ClusterViewFragment fragment, bool cullBackFaces)
{
    const std::size_t index = static_cast<std::size_t>(fragment) * 2 + (cullBackFaces ? 1 : 0);
    if (pipelines[index].has_value()) {
        return &*pipelines[index];
    }
    if (pipelineFailed[index]) {
        return nullptr;
    }
    try {
        // Cluster buffers sit at set 2; sets 0 and 1 are zero-binding fillers.
        const DescriptorSetLayoutHandle extraSets[] = { renderer.EmptySetLayout(), renderer.EmptySetLayout(),
            renderer.BufferSetLayout() };
        PipelineDesc desc = PipelinePreset::Opaque(PixelFormat::RGBA8_UNorm, renderer.DepthPixelFormat());
        desc.vertexLayout = VertexLayout::PositionNormal;
        desc.cullMode = cullBackFaces ? CullMode::Back : CullMode::None;
        desc.layout.extraSetLayouts = extraSets;
        desc.layout.pushConstantByteSize = 128;
        desc.layout.pushConstantStages = ShaderStageMask::Vertex;
        desc.debugName = "ClusterView";
        pipelines[index].emplace(
            renderer.CreatePipeline(desc, vertexSpirv, fragmentSpirv[static_cast<std::size_t>(fragment)]));
        return &*pipelines[index];
    } catch (const std::exception& e) {
        pipelineFailed[index] = true;
        LogOnce(std::string("a viewer pipeline could not be built: ") + e.what());
        return nullptr;
    }
}

ClusterViewRenderer::Impl::RunResult ClusterViewRenderer::Impl::RunPlan(
    SetId finalSet, const ClusterViewCull& cull, const AppliedCull& applied, const DrawPlan* draw, bool readSurvivors)
{
    RunResult result;
    const auto start = std::chrono::steady_clock::now();
    if (!mesh.has_value() || !table.has_value() || !instance.has_value() || tableClusters == 0) {
        result.note = "no mesh is set";
        return result;
    }

    result.occlusionApplied = applied.occlusionApplied;
    result.hizCut = applied.hizCut;

    // Applied flags in shader order; the stage runs add them one by one.
    constexpr ClusterCullFlags kShaderOrder[] = {
        ClusterCullFlags::Frustum, ClusterCullFlags::BackfaceCone, ClusterCullFlags::Occlusion
    };
    std::array<ClusterCullFlags, 3> ordered{};
    std::size_t orderedCount = 0;
    for (const ClusterCullFlags flag : kShaderOrder) {
        if (HasClusterCullFlag(applied.flags, flag)) {
            ordered[orderedCount++] = flag;
        }
    }
    const bool ghost = draw != nullptr && draw->ghost != nullptr;
    const std::uint32_t total = tableClusters;

    if (!EnsureSet(finalSet, true, true) || (orderedCount >= 2 && !EnsureSet(SetId::StageA, false, true))
        || (orderedCount >= 3 && !EnsureSet(SetId::StageB, false, true)) || (ghost && !EnsureSet(SetId::Ghost, false, false))) {
        result.note = "the cull buffers could not be allocated";
        return result;
    }

    const std::size_t finalIndex = static_cast<std::size_t>(finalSet);
    const SetNames& finalNames = kSetNames[finalIndex];
    std::string buildError;

    const auto build = [&](rg::RenderGraphBuilder& builder) {
        std::vector<rg::TextureHandle> outputs;
        if (!cullShader.IsReady(builder)) {
            buildError = "the cull shader is not ready";
            return outputs;
        }
        const rg::BufferHandle instanceHandle = builder.Import(*instance);
        const rg::BufferHandle tableHandle = builder.Import(*table);
        const rg::TextureHandle hizHandle = ImportHizAtlas(builder, applied.occlusionApplied);
        const rg::BufferHandle historyHandle = ImportHistoryStandIn(builder);

        const GraphSet fin = ImportSet(builder, finalSet);
        std::array<GraphSet, 2> stages{};
        const std::size_t stageCount = orderedCount > 0 ? orderedCount - 1 : 0;
        for (std::size_t k = 0; k < stageCount; ++k) {
            stages[k] = ImportSet(builder, k == 0 ? SetId::StageA : SetId::StageB);
        }
        GraphSet ghostSet;
        if (ghost) {
            ghostSet = ImportSet(builder, SetId::Ghost);
        }

        bool ok = DeclareFill(builder, finalNames.reset, fin.count);
        for (std::size_t k = 0; k < stageCount; ++k) {
            ok = DeclareFill(builder, kSetNames[static_cast<std::size_t>(stages[k].id)].reset, stages[k].count) && ok;
        }
        if (ghost) {
            ok = DeclareFill(builder, kSetNames[static_cast<std::size_t>(SetId::Ghost)].reset, ghostSet.count) && ok;
        }

        // Stage run k applies the first k flags; the final run applies all of them.
        // Occlusion is the last flag in shader order, so a stage prefix never holds it.
        ClusterCullFlags prefix = ClusterCullFlags::None;
        for (std::size_t k = 0; k < stageCount; ++k) {
            prefix = prefix | ordered[k];
            ok = DeclareCull(builder, stages[k], instanceHandle, tableHandle, historyHandle, hizHandle,
                     MakeParams(cull, prefix, false))
                && ok;
        }
        ok = DeclareCull(builder, fin, instanceHandle, tableHandle, historyHandle, hizHandle,
                 MakeParams(cull, applied.flags, applied.occlusionApplied))
            && ok;
        if (ghost) {
            ok = DeclareCull(builder, ghostSet, instanceHandle, tableHandle, historyHandle, hizHandle,
                     MakeParams(cull, ClusterCullFlags::None, false))
                && ok;
        }

        if (draw != nullptr) {
            RenderTexture& texture = *draw->target->texture;
            const rg::TextureHandle targetHandle = builder.ImportTexture(
                finalSet == SetId::Main ? "ClusterViewMain" : "ClusterViewPreview", texture.Target(),
                ImageLayout::Undefined, texture.SamplerObjectHandle(), texture.DepthSamplerObjectHandle(), &texture);
            builder.KeepTextureOutput(targetHandle);
            outputs.push_back(targetHandle);
            const ClusterViewFrame& frame = *draw->frame;
            ok = DeclareDraw(builder, kDrawNames[finalIndex], fin, instanceHandle, targetHandle, false,
                     *draw->survivors, frame.viewProjection, 0, ToClearColor(frame.clearRgba), 1.0f)
                && ok;
            if (ghost) {
                ok = DeclareDraw(builder, kGhostDrawNames[finalIndex], ghostSet, instanceHandle, targetHandle, false,
                         *draw->ghost, frame.viewProjection, 1, std::nullopt, std::nullopt)
                    && ok;
            }
        }

        ok = DeclareReadback(builder, finalNames.readCount, fin.count, fin.countHost, kCountBytes) && ok;
        for (std::size_t k = 0; k < stageCount; ++k) {
            const SetNames& names = kSetNames[static_cast<std::size_t>(stages[k].id)];
            ok = DeclareReadback(builder, names.readCount, stages[k].count, stages[k].countHost, kCountBytes) && ok;
        }
        if (draw != nullptr) {
            ok = DeclareReadback(builder, finalNames.readCommands, fin.commands, fin.commandsHost,
                     BufferBytes(kClusterCommandBytes, total))
                && ok;
        }
        if (readSurvivors) {
            ok = DeclareReadback(builder, finalNames.readVisible, fin.visible, fin.visibleHost,
                     BufferBytes(kClusterVisibleBytes, total))
                && ok;
        }
        if (!ok) {
            buildError = "a pass of the run could not be declared";
        }
        return outputs;
    };

    RenderTexture* finalize[1] = { draw != nullptr ? &*draw->target->texture : nullptr };
    const std::span<RenderTexture* const> finalizeList(finalize, draw != nullptr ? 1 : 0);
    if (!Execute(build, finalizeList, result.note)) {
        return result;
    }
    if (!buildError.empty()) {
        result.note = buildError;
        LogOnce(buildError);
        return result;
    }

    // Survivor counts: n[k] after the first k flags; n[0] is every cluster.
    std::array<std::uint32_t, 4> counts{};
    counts[0] = total;
    const std::size_t stageCount = orderedCount > 0 ? orderedCount - 1 : 0;
    for (std::size_t k = 0; k < stageCount; ++k) {
        if (!Set(k == 0 ? SetId::StageA : SetId::StageB).countHost.Download(&counts[k + 1], kCountBytes)) {
            result.note = "the stage count could not be read back";
            return result;
        }
    }
    std::uint32_t visible = 0;
    if (!Set(finalSet).countHost.Download(&visible, kCountBytes)) {
        result.note = "the survivor count could not be read back";
        return result;
    }
    counts[orderedCount] = visible;
    for (std::size_t k = 0; k <= orderedCount; ++k) {
        if (counts[k] > total) {
            result.note = "count past the cluster count";
            LogOnce(result.note);
            return result;
        }
    }

    std::size_t position = 0;
    result.counts.total = total;
    result.counts.passedFrustum = total;
    if (HasClusterCullFlag(applied.flags, ClusterCullFlags::Frustum)) {
        result.counts.passedFrustum = counts[++position];
    }
    result.counts.passedCone = result.counts.passedFrustum;
    if (HasClusterCullFlag(applied.flags, ClusterCullFlags::BackfaceCone)) {
        result.counts.passedCone = counts[++position];
    }
    result.counts.visible = visible;

    CullSet& finalCullSet = Set(finalSet);
    if (visible > 0) {
        if (draw != nullptr) {
            std::vector<std::uint32_t> commandWords(static_cast<std::size_t>(visible) * 5);
            if (!finalCullSet.commandsHost.Download(commandWords.data(), commandWords.size() * sizeof(std::uint32_t))) {
                result.note = "the draw commands could not be read back";
                return result;
            }
            std::uint64_t indexTotal = 0;
            for (std::size_t i = 0; i < visible; ++i) {
                indexTotal += commandWords[i * 5];
            }
            result.counts.visibleTriangles = indexTotal / 3;
        }

        if (readSurvivors) {
            std::vector<std::uint32_t> records(static_cast<std::size_t>(visible) * 2);
            if (!finalCullSet.visibleHost.Download(records.data(), records.size() * sizeof(std::uint32_t))) {
                result.note = "the survivor list could not be read back";
                return result;
            }
            result.survivors.reserve(visible);
            for (std::size_t i = 0; i < visible; ++i) {
                result.survivors.push_back(records[i * 2 + 1]);
            }
        }
    }

    result.ok = true;
    result.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

std::unique_ptr<ClusterViewRenderer> ClusterViewRenderer::TryCreate(Renderer& renderer, std::string* whyNot)
{
    const auto fail = [whyNot](std::string reason) -> std::unique_ptr<ClusterViewRenderer> {
        GTE_LOG_ERROR(kLogCategory, reason);
        if (whyNot != nullptr) {
            *whyNot = std::move(reason);
        }
        return nullptr;
    };
    if (!ClusterPathSupported(renderer.IndirectDrawCaps())) {
        return fail("The cluster path is not supported on this device.");
    }
    try {
        auto impl = std::make_unique<Impl>(renderer);
        impl->Build();
        return std::make_unique<ClusterViewRenderer>(CreateKey{}, std::move(impl));
    } catch (const std::exception& e) {
        return fail(std::string("The cluster viewer could not be built: ") + e.what());
    }
}

ClusterViewRenderer::ClusterViewRenderer(CreateKey, std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ClusterViewRenderer::~ClusterViewRenderer() = default;

ClusterViewMeshResult ClusterViewRenderer::SetMesh(std::span<const MeshPreviewVertex> vertices,
    std::span<const std::uint32_t> indices, std::span<const MeshCluster> clusters)
{
    Impl& s = *m_impl;
    ClusterViewMeshResult result;
    try {
        if (!s.ReleaseMesh()) {
            result.note = "the GPU did not go idle; the old mesh is kept";
            result.retryable = true;
            s.LogOnce(result.note);
            return result;
        }

        constexpr std::size_t kMaxCount = std::numeric_limits<std::uint32_t>::max();
        std::string why;
        if (vertices.empty() || vertices.size() > kMaxCount || indices.size() > kMaxCount) {
            result.note = "the mesh is empty or too large";
            s.LogOnce(result.note);
            return result;
        }
        if (!ValidateMeshIndices(vertices.size(), indices, &why)) {
            result.note = "the index list is not valid: " + why;
            s.LogOnce(result.note);
            return result;
        }

        // Check the cluster table before the upload; an unusable one is replaced by a single cluster.
        bool usable = false;
        if (clusters.empty()) {
            result.note = "the mesh has no cluster table";
        } else if (clusters.size() > s.maxClusters) {
            result.note = "too many clusters for cluster modes (" + std::to_string(clusters.size()) + " > "
                + std::to_string(s.maxClusters) + ")";
        } else if (!ValidateClusterTable(clusters, indices.size(), &why)) {
            result.note = "the cluster table is damaged: " + why;
        } else {
            usable = true;
        }

        MeshCluster single{};
        std::span<const MeshCluster> table = clusters;
        if (!usable) {
            LocalBoundsView bounds;
            bounds.valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                bounds.min[axis] = std::numeric_limits<float>::max();
                bounds.max[axis] = std::numeric_limits<float>::lowest();
            }
            for (const MeshPreviewVertex& vertex : vertices) {
                for (int axis = 0; axis < 3; ++axis) {
                    bounds.min[axis] = std::min(bounds.min[axis], vertex.position[axis]);
                    bounds.max[axis] = std::max(bounds.max[axis], vertex.position[axis]);
                }
            }
            const std::optional<MeshCluster> synthesized = SynthesizeSingleCluster(bounds, indices.size(), &why);
            if (!synthesized.has_value()) {
                result.note = "the mesh bounds are not usable: " + why;
                s.LogOnce(result.note);
                return result;
            }
            single = *synthesized;
            table = std::span<const MeshCluster>(&single, 1);
        }

        const rg::DeviceRef device = s.renderer.Device();
        s.mesh.emplace(s.renderer.CreateMesh(vertices.data(), vertices.size_bytes(),
            static_cast<std::uint32_t>(vertices.size()), indices.data(), indices.size_bytes(),
            static_cast<std::uint32_t>(indices.size()), "ClusterViewMesh"));
        s.table.emplace(rg::GpuBuffer::CreateWithData(
            device, table.data(), table.size_bytes(), "ClusterViewTable", rg::GpuBufferUse::Storage));
        s.instance.emplace(device, sizeof(ClusterInstanceGpu), "ClusterViewInstance",
            rg::GpuBufferUse::Storage | rg::GpuBufferUse::HostWrite);
        float maxScale = 1.0f;
        const std::uint32_t flags = ClusterInstanceFlagsFor(Mat4::Identity(), &maxScale);
        const ClusterInstanceGpu identity = MakeClusterInstanceGpu(Mat4::Identity(), flags, maxScale);
        if (!s.instance->Upload(&identity, sizeof(identity))) {
            throw std::runtime_error("the instance record could not be uploaded");
        }

        s.tableClusters = static_cast<std::uint32_t>(table.size());
        s.realClusters = usable ? s.tableClusters : 0;
        result.meshSet = true;
        result.clustersUsable = usable;
        if (!usable) {
            GTE_LOG_WARNING(kLogCategory, result.note);
        }
        return result;
    } catch (const std::exception& e) {
        static_cast<void>(s.ReleaseMesh()); // best effort: a busy GPU keeps the partial mesh alive
        result = ClusterViewMeshResult{};
        result.note = std::string("the mesh could not be uploaded: ") + e.what();
        s.LogOnce(result.note);
        return result;
    }
}

ClusterViewRender ClusterViewRenderer::Render(ClusterViewSlot slot, const ClusterViewFrame& frame)
{
    Impl& s = *m_impl;
    ClusterViewRender out;
    try {
        if (!s.mesh.has_value()) {
            out.note = "no mesh is set";
            return out;
        }
        if (!s.CheckSize(frame.width, frame.height, out.note) || !s.CheckCullCamera(frame.cull, out.note)) {
            s.LogOnce(out.note);
            return out;
        }
        if (!CameraIsFinite(frame.viewProjection, Vec3::Zero())) {
            out.note = "the draw camera holds a non-finite value";
            s.LogOnce(out.note);
            return out;
        }

        // The cone test and the back-face-culling pipeline are one decision.
        const Impl::AppliedCull applied = s.ResolveFlags(frame.cull);
        const bool cullBackFaces = HasClusterCullFlag(applied.flags, ClusterCullFlags::BackfaceCone);
        const Pipeline* survivors = s.ViewPipeline(
            frame.shade == ClusterViewShade::Tint ? ClusterViewFragment::Tint : ClusterViewFragment::Clay, cullBackFaces);
        const Pipeline* ghost = frame.dimCulled ? s.ViewPipeline(ClusterViewFragment::Dim, cullBackFaces) : nullptr;
        if (survivors == nullptr || (frame.dimCulled && ghost == nullptr)) {
            out.note = "a viewer pipeline is not available";
            return out;
        }

        const bool isMain = slot == ClusterViewSlot::Main;
        Impl::SlotTarget& target = isMain ? s.mainSlot : s.previewSlot;
        if (!s.EnsureTarget(target, isMain ? "ClusterViewMain" : "ClusterViewPreview",
                isMain ? "ClusterViewMainDepth" : "ClusterViewPreviewDepth", frame.width, frame.height)) {
            out.note = "the image target could not be made";
            return out;
        }

        const Impl::DrawPlan draw{ &target, &frame, survivors, ghost };
        const Impl::RunResult run =
            s.RunPlan(isMain ? Impl::SetId::Main : Impl::SetId::Preview, frame.cull, applied, &draw, false);
        out.ok = run.ok;
        out.note = run.note;
        out.counts = run.counts;
        out.occlusionApplied = run.occlusionApplied;
        out.hizCut = run.hizCut;
        out.runMilliseconds = run.milliseconds;
        if (run.ok) {
            out.texture = target.ui.Get();
        }
    } catch (const std::exception& e) {
        out = ClusterViewRender{};
        out.note = std::string("the run failed: ") + e.what();
        s.LogOnce(out.note);
    }
    return out;
}

ClusterViewCullResult ClusterViewRenderer::RunCull(const ClusterViewCull& cull)
{
    Impl& s = *m_impl;
    ClusterViewCullResult out;
    try {
        if (!s.mesh.has_value()) {
            out.note = "no mesh is set";
            return out;
        }
        if (!s.CheckCullCamera(cull, out.note)) {
            s.LogOnce(out.note);
            return out;
        }
        const Impl::AppliedCull applied = s.ResolveFlags(cull);
        Impl::RunResult run = s.RunPlan(Impl::SetId::Main, cull, applied, nullptr, true);
        out.ok = run.ok;
        out.note = std::move(run.note);
        out.survivors = std::move(run.survivors);
        out.occlusionApplied = run.occlusionApplied;
    } catch (const std::exception& e) {
        out = ClusterViewCullResult{};
        out.note = std::string("the cull run failed: ") + e.what();
        s.LogOnce(out.note);
    }
    return out;
}

std::uint32_t ClusterViewRenderer::ClusterCount() const noexcept
{
    return m_impl->realClusters;
}

std::uint32_t ClusterViewRenderer::MaxClusters() const noexcept
{
    return m_impl->maxClusters;
}

void ClusterViewRenderer::ReleaseTargets()
{
    Impl& s = *m_impl;
    s.ReleaseSlot(s.previewSlot);
    s.ReleaseSlot(s.idSlot);
}

} // namespace gte
