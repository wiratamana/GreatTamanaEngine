#include "RenderGraphMetadata.h"

#include "RenderGraphSnapshotFormatting.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>

namespace gte::rg {

namespace {

const char* ViewLabel(ViewScope scope) noexcept
{
    switch (scope) {
    case ViewScope::Shared: return "shared";
    case ViewScope::Primary: return "primary";
    case ViewScope::Secondary: return "secondary";
    }
    return "shared";
}

// "0x..." hex text of a binding instance id; empty for 0 (the pass never executed).
std::string InstanceText(std::uint64_t instance)
{
    if (instance == 0) {
        return {};
    }
    std::array<char, 18> digits{};
    const std::to_chars_result result = std::to_chars(digits.data(), digits.data() + digits.size(), instance, 16);
    return "0x" + std::string(digits.data(), result.ptr);
}

// Zips the parallel name/kind/access arrays into one array of ref structs, clamped to the shortest.
// `barrierLabels` is the write-barrier label list, or null for reads.
std::vector<RenderGraphResourceRefMetadata> BuildResourceRefs(const std::vector<std::string>& names,
    const std::vector<ResourceKind>& kinds, const std::vector<ResourceAccess>& access,
    const std::vector<std::string>* barrierLabels)
{
    std::vector<RenderGraphResourceRefMetadata> refs;
    const std::size_t count = std::min({ names.size(), kinds.size(), access.size() });
    refs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        RenderGraphResourceRefMetadata ref;
        ref.name = names[i];
        ref.kind = ToString(kinds[i]);
        ref.bindingStageLabel = BindingStageLabel(access[i]);
        if (barrierLabels != nullptr && i < barrierLabels->size() && !(*barrierLabels)[i].empty()) {
            ref.barrierTransitionLabel = (*barrierLabels)[i];
        }
        refs.push_back(std::move(ref));
    }
    return refs;
}

RenderGraphPassMetadata BuildPassMetadata(const RenderGraphPassSnapshot& pass)
{
    RenderGraphPassMetadata metadata;
    metadata.name = pass.name;
    metadata.isCulled = pass.isCulled;
    metadata.kind = ToString(pass.kind);
    metadata.category = ToString(pass.category);
    metadata.drawKind = ToString(pass.drawKind);
    metadata.viewScope = pass.viewScope;
    metadata.view = ViewLabel(pass.viewScope);
    metadata.instance = InstanceText(pass.stats.gpu.bindingInstance);
    metadata.renderPassEvent = ToString(pass.renderPassEvent);
    metadata.renderPassEventOrder = static_cast<std::uint32_t>(pass.renderPassEvent);

    metadata.owningFeatureName = pass.owningFeatureName;

    metadata.reads = BuildResourceRefs(pass.readNames, pass.readKinds, pass.readAccess, nullptr);
    metadata.writes = BuildResourceRefs(pass.writeNames, pass.writeKinds, pass.writeAccess, &pass.writeBarrierLabels);

    // A culled pass has default stats, so no special case is needed.
    metadata.drawCallCount = pass.stats.drawStats.drawCallCount;
    metadata.triangleCount = pass.stats.drawStats.triangleCount;
    metadata.indirectDrawCount = pass.stats.drawStats.indirectDrawCount;
    metadata.gpuTimingState = pass.stats.gpu.state;
    metadata.gpuFoldedPasses = pass.stats.gpu.foldedPasses;
    metadata.gpuRecordedMilliseconds =
        static_cast<double>(pass.stats.gpu.barrierMs) + static_cast<double>(pass.stats.gpu.workMs);
    if (pass.stats.gpu.state == GpuReadState::Present) {
        metadata.gpuTimingMilliseconds = metadata.gpuRecordedMilliseconds;
    }

    return metadata;
}

RenderGraphResourceMetadata BuildResourceMetadata(
    const RenderGraphResourceSnapshot& resource, const RenderGraphSnapshot& snapshot)
{
    RenderGraphResourceMetadata metadata;
    metadata.name = resource.name;
    metadata.isImported = resource.isImported;
    metadata.firstUsePassIndex = resource.firstUsePassIndex;
    metadata.lastUsePassIndex = resource.lastUsePassIndex;
    metadata.firstUsePassName = (resource.firstUsePassIndex < 0)
        ? std::nullopt
        : std::optional<std::string>(ResolvePassNameAtSurvivingIndex(snapshot, resource.firstUsePassIndex));
    metadata.lastUsePassName = (resource.lastUsePassIndex < 0)
        ? std::nullopt
        : std::optional<std::string>(ResolvePassNameAtSurvivingIndex(snapshot, resource.lastUsePassIndex));
    return metadata;
}

RenderGraphRegimeMetadata BuildRegimeMetadata(const char* regimeName, const RenderGraphSnapshot& snapshot)
{
    RenderGraphRegimeMetadata regime;
    regime.regimeName = regimeName;

    regime.passes.reserve(snapshot.passesInExecutionOrder.size());
    for (const RenderGraphPassSnapshot& pass : snapshot.passesInExecutionOrder) {
        regime.passes.push_back(BuildPassMetadata(pass));
    }

    regime.resources.reserve(snapshot.resources.size());
    for (const RenderGraphResourceSnapshot& resource : snapshot.resources) {
        regime.resources.push_back(BuildResourceMetadata(resource, snapshot));
    }

    regime.gpuFrameStatus = ToString(snapshot.gpuReport.state);
    if (snapshot.gpuReport.spanValid) {
        regime.gpuSpanMilliseconds = snapshot.gpuReport.spanMs;
    }
    regime.gpuFoldedPasses = snapshot.gpuReport.foldedPasses;
    regime.droppedGpuFrames = snapshot.droppedGpuFrames;
    return regime;
}

} // namespace

RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures)
{
    RenderGraphMetadata metadata;
    metadata.schemaVersion = 2;
    metadata.offscreenRegime = BuildRegimeMetadata("SynchronousImmediateReadback", offscreen);
    metadata.presentRegime = BuildRegimeMetadata("PipelinedDeferredReadback", present);
    metadata.renderFeatures = renderFeatures;
    return metadata;
}

RenderGraphGpuMemoryMetadata BuildGpuMemoryMetadata(std::span<const GpuAllocationSample> samples, std::uint64_t meshCount)
{
    RenderGraphGpuMemoryMetadata memory;
    memory.meshCount = meshCount;
    for (const GpuAllocationSample& sample : samples) {
        memory.totalBytes += sample.bytes;
        memory.byName[sample.name.empty() ? std::string(kUnnamedGpuAllocation) : sample.name] += sample.bytes;
    }
    return memory;
}

} // namespace gte::rg
