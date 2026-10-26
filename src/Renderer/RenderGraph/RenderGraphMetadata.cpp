#include "RenderGraphMetadata.h"

#include "RenderGraphSnapshotFormatting.h"

#include <algorithm>
#include <cstddef>
#include <map>

namespace gte::rg {

namespace {

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
    metadata.viewScope = ToString(pass.viewScope);
    metadata.renderPassEvent = ToString(pass.renderPassEvent);
    metadata.renderPassEventOrder = static_cast<std::uint32_t>(pass.renderPassEvent);

    metadata.owningFeatureName = pass.owningFeatureName;

    metadata.reads = BuildResourceRefs(pass.readNames, pass.readKinds, pass.readAccess, nullptr);
    metadata.writes = BuildResourceRefs(pass.writeNames, pass.writeKinds, pass.writeAccess, &pass.writeBarrierLabels);

    // A culled pass has default stats, so no special case is needed.
    metadata.drawCallCount = pass.stats.drawStats.drawCallCount;
    metadata.triangleCount = pass.stats.drawStats.triangleCount;
    metadata.indirectDrawCount = pass.stats.drawStats.indirectDrawCount;
    metadata.gpuTimingText = FormatGpuTiming(pass.stats.timing);
    metadata.gpuTimingMilliseconds = (pass.stats.timing.status == GpuTimingSample::Status::Present)
        ? std::optional<double>(pass.stats.timing.milliseconds)
        : std::nullopt;

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

    regime.timingSlotBudgetExhausted = snapshot.timingSlotBudgetExhausted;
    return regime;
}

// Short display label of a resolved view scope string ("Game", "Scene", "Shared").
const char* ShortViewLabel(const std::string& viewScope)
{
    if (viewScope == "GameView") {
        return "Game";
    }
    if (viewScope == "SceneView") {
        return "Scene";
    }
    if (viewScope == "Shared") {
        return "Shared";
    }
    return viewScope.c_str(); // Defensive fallback - should never happen.
}

// Fixed order when combining view labels, so the output is deterministic.
int ShortViewLabelPriority(const std::string& shortLabel)
{
    if (shortLabel == "Game") {
        return 0;
    }
    if (shortLabel == "Scene") {
        return 1;
    }
    if (shortLabel == "Shared") {
        return 2;
    }
    return 3;
}

// View badge ("Game+Scene", "Game only", "Shared") from the contributing instances: distinct short
// labels sorted by priority and joined with "+"; a lone Game or Scene gets " only".
std::string BuildViewLabel(const std::vector<const RenderGraphPassMetadata*>& contributors)
{
    std::vector<std::string> distinct;
    for (const RenderGraphPassMetadata* instance : contributors) {
        const std::string label = ShortViewLabel(instance->viewScope);
        if (std::find(distinct.begin(), distinct.end(), label) == distinct.end()) {
            distinct.push_back(label);
        }
    }
    std::sort(distinct.begin(), distinct.end(), [](const std::string& a, const std::string& b) {
        return ShortViewLabelPriority(a) < ShortViewLabelPriority(b);
    });

    if (distinct.empty()) {
        return "(no instance)"; // Defensive - GroupPassMetadataByName() never builds a group with zero instances.
    }
    if (distinct.size() == 1) {
        return (distinct[0] == "Game" || distinct[0] == "Scene") ? (distinct[0] + " only") : distinct[0];
    }
    std::string joined;
    for (const std::string& label : distinct) {
        if (!joined.empty()) {
            joined += "+";
        }
        joined += label;
    }
    return joined;
}

// Per-view timing text ("Game: 0.12 ms, Scene: 0.08 ms"), not summed. A single instance shows its own
// text. Views are ordered by ShortViewLabelPriority(); equal views keep their relative order.
std::string BuildGroupedGpuTimingText(const std::vector<RenderGraphPassMetadata>& instances)
{
    if (instances.size() == 1) {
        return instances.front().gpuTimingText;
    }
    std::vector<const RenderGraphPassMetadata*> sorted;
    sorted.reserve(instances.size());
    for (const RenderGraphPassMetadata& instance : instances) {
        sorted.push_back(&instance);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const RenderGraphPassMetadata* a, const RenderGraphPassMetadata* b) {
        return ShortViewLabelPriority(ShortViewLabel(a->viewScope)) < ShortViewLabelPriority(ShortViewLabel(b->viewScope));
    });
    std::string joined;
    for (const RenderGraphPassMetadata* instance : sorted) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += std::string(ShortViewLabel(instance->viewScope)) + ": " + instance->gpuTimingText;
    }
    return joined;
}

// Combined, de-duplicated (first-seen order) names of every instance's reads (or writes when !reads).
std::vector<std::string> CombineResourceRefNames(const std::vector<RenderGraphPassMetadata>& instances, bool reads)
{
    std::vector<std::string> combined;
    for (const RenderGraphPassMetadata& instance : instances) {
        const std::vector<RenderGraphResourceRefMetadata>& refs = reads ? instance.reads : instance.writes;
        for (const RenderGraphResourceRefMetadata& ref : refs) {
            if (std::find(combined.begin(), combined.end(), ref.name) == combined.end()) {
                combined.push_back(ref.name);
            }
        }
    }
    return combined;
}

} // namespace

RenderGraphMetadata BuildRenderGraphMetadata(const RenderGraphSnapshot& offscreen, const RenderGraphSnapshot& present,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures)
{
    RenderGraphMetadata metadata;
    metadata.schemaVersion = 1;
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

// std::map iteration is sorted by name, so row order does not depend on input order.
std::vector<RenderGraphGroupedPassMetadata> GroupPassMetadataByName(const std::vector<RenderGraphPassMetadata>& ungrouped)
{
    std::map<std::string, std::vector<RenderGraphPassMetadata>> instancesByName;
    for (const RenderGraphPassMetadata& pass : ungrouped) {
        instancesByName[pass.name].push_back(pass);
    }

    std::vector<RenderGraphGroupedPassMetadata> grouped;
    grouped.reserve(instancesByName.size());
    for (auto& [name, instances] : instancesByName) {
        RenderGraphGroupedPassMetadata group;
        group.name = name;

        bool allCulled = true;
        std::vector<const RenderGraphPassMetadata*> nonCulled;
        std::vector<const RenderGraphPassMetadata*> allInstances;
        for (const RenderGraphPassMetadata& instance : instances) {
            group.drawCallCount += instance.drawCallCount;
            group.triangleCount += instance.triangleCount;
            if (!instance.isCulled) {
                allCulled = false;
                nonCulled.push_back(&instance);
            }
            allInstances.push_back(&instance);
        }
        group.isCulled = allCulled;
        // The badge reflects only views that contributed a non-culled instance.
        group.viewLabel = BuildViewLabel(nonCulled.empty() ? allInstances : nonCulled);

        group.gpuTimingText = BuildGroupedGpuTimingText(instances);
        group.reads = CombineResourceRefNames(instances, /*reads=*/true);
        group.writes = CombineResourceRefNames(instances, /*reads=*/false);
        group.instances = std::move(instances);

        grouped.push_back(std::move(group));
    }
    return grouped;
}

// --- JSON (namespace gte::rg) --------------------------------------------
// Snake_case keys, matching the other HTTP JSON bodies. Defined bottom-up so unqualified lookup finds each one.

void to_json(nlohmann::json& j, const RenderGraphResourceRefMetadata& ref)
{
    j = nlohmann::json{
        { "name", ref.name },
        { "kind", ref.kind },
        { "binding_stage_label", ref.bindingStageLabel },
        { "barrier_transition_label",
            ref.barrierTransitionLabel.has_value() ? nlohmann::json(*ref.barrierTransitionLabel)
                                                    : nlohmann::json(nullptr) },
    };
}

void to_json(nlohmann::json& j, const RenderGraphPassMetadata& pass)
{
    j = nlohmann::json{
        { "name", pass.name },
        { "is_culled", pass.isCulled },
        { "kind", pass.kind },
        { "category", pass.category },
        { "draw_kind", pass.drawKind },
        { "view_scope", pass.viewScope },
        { "render_pass_event", pass.renderPassEvent },
        { "render_pass_event_order", pass.renderPassEventOrder },
        { "owning_feature_name", pass.owningFeatureName },
        { "reads", pass.reads },
        { "writes", pass.writes },
        { "draw_call_count", pass.drawCallCount },
        { "triangle_count", pass.triangleCount },
        { "indirect_draw_count", pass.indirectDrawCount },
        { "gpu_timing_text", pass.gpuTimingText },
        { "gpu_timing_milliseconds",
            pass.gpuTimingMilliseconds.has_value() ? nlohmann::json(*pass.gpuTimingMilliseconds)
                                                    : nlohmann::json(nullptr) },
    };
}

void to_json(nlohmann::json& j, const RenderGraphResourceMetadata& resource)
{
    j = nlohmann::json{
        { "name", resource.name },
        { "is_imported", resource.isImported },
        { "first_use_pass_index", resource.firstUsePassIndex },
        { "last_use_pass_index", resource.lastUsePassIndex },
        { "first_use_pass_name",
            resource.firstUsePassName.has_value() ? nlohmann::json(*resource.firstUsePassName)
                                                   : nlohmann::json(nullptr) },
        { "last_use_pass_name",
            resource.lastUsePassName.has_value() ? nlohmann::json(*resource.lastUsePassName)
                                                  : nlohmann::json(nullptr) },
    };
}

void to_json(nlohmann::json& j, const RenderGraphRegimeMetadata& regime)
{
    j = nlohmann::json{
        { "regime_name", regime.regimeName },
        { "passes", regime.passes },
        { "resources", regime.resources },
        { "timing_slot_budget_exhausted", regime.timingSlotBudgetExhausted },
    };
}

void to_json(nlohmann::json& j, const RenderGraphMetadata& metadata)
{
    j = nlohmann::json{
        { "schema_version", metadata.schemaVersion },
        { "offscreen_regime", metadata.offscreenRegime },
        { "present_regime", metadata.presentRegime },
        { "render_features", metadata.renderFeatures },
        { "indirect_draw",
            {
                { "draw_indirect_count", metadata.indirectDraw.drawIndirectCount },
                { "multi_draw_indirect", metadata.indirectDraw.multiDrawIndirect },
                { "draw_indirect_first_instance", metadata.indirectDraw.drawIndirectFirstInstance },
                { "max_draw_indirect_count", metadata.indirectDraw.maxDrawIndirectCount },
            } },
        { "gpu_wait",
            {
                { "device_lost", metadata.gpuWait.deviceLost },
                { "consecutive_frame_timeouts", metadata.gpuWait.consecutiveFrameTimeouts },
                { "abandoned_objects", metadata.gpuWait.abandonedObjects },
            } },
        { "gpu_memory",
            {
                { "total_bytes", metadata.gpuMemory.totalBytes },
                { "mesh_count", metadata.gpuMemory.meshCount },
                { "by_name", metadata.gpuMemory.byName },
            } },
    };
}

} // namespace gte::rg

namespace gte {

// Defined here so RenderFeatureDebugEntry.h needs no json include.
void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)
{
    j = nlohmann::json{
        { "name", entry.name },
        { "stage", entry.stage },
        { "priority", entry.priority },
        { "blend_mode", entry.blendMode },
        // Host-side enable/disable override.
        { "enabled", entry.enabled },
        // True for a feature declared by a loaded project.
        { "is_project_feature", entry.isProjectFeature },
    };
}

} // namespace gte
