#include "RenderGraphMetadata.h"

#include "RenderGraphSnapshotFormatting.h"

#include <algorithm>
#include <cstddef>
#include <map>

namespace gte::rg {

namespace {

// Zips `names`/`kinds`/`access` (same-length parallel arrays on
// RenderGraphPassSnapshot) into the JSON-friendlier "one array of
// {name,kind,bindingStageLabel} structs" shape. Clamped to the shortest of
// the three vectors rather than asserting - a silently-shorter result beats
// an out-of-bounds read. `barrierLabels`, when non-null, is
// RenderGraphPassSnapshot::writeBarrierLabels (nullptr for a reads call).
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

    // Falls out correctly for a culled pass with no special-case branch -
    // a culled pass's own `stats` is always left at its default by
    // BuildRenderGraphSnapshot().
    metadata.drawCallCount = pass.stats.drawStats.drawCallCount;
    metadata.triangleCount = pass.stats.drawStats.triangleCount;
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

// Grouping helpers for GroupPassMetadataByName() below. All pure, all local
// to this TU.

// Short display label for an already-resolved ViewScope string
// (RenderGraphPassMetadata::viewScope is always exactly "Shared" /
// "GameView" / "SceneView"). The `default:`-shaped fallback is correct here
// since this maps an already-resolved std::string, not an exhaustive enum.
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

// Fixed priority for deterministic ordering when combining more than one
// instance's short view label into either the row's viewLabel badge or its
// per-view gpuTimingText breakdown.
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

// Builds the row's view-label badge text (e.g. "Game+Scene", "Game only",
// "Shared") from `contributors` - the caller decides which subset of a
// group's instances counts as a contributor (normally the non-culled
// subset, falling back to every instance when the whole group is culled).
// Distinct short labels only, sorted by ShortViewLabelPriority(), joined
// with "+". A lone "Game"/"Scene" label gets an " only" suffix.
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

// Per-view GPU timing breakdown text (e.g. "Game: 0.12 ms, Scene: 0.08 ms") -
// never summed/maxed, since GPU timing is not meaningfully additive across
// logically-separate view passes the way draw/triangle counts are. A
// single-instance group shows that instance's own gpuTimingText verbatim.
// Ordered by ShortViewLabelPriority() for the same determinism reason
// BuildViewLabel() sorts by it; std::stable_sort keeps two same-viewScope
// instances in their original relative order.
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

// Combined, de-duplicated (order-preserving, first-seen) resource names
// across every instance sharing this group's name - `reads` selects
// RenderGraphPassMetadata::reads when true, ::writes when false.
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
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatches,
    const std::vector<RenderFeatureDebugEntry>& renderFeatures)
{
    RenderGraphMetadata metadata;
    metadata.schemaVersion = 1;
    metadata.offscreenRegime = BuildRegimeMetadata("SynchronousImmediateReadback", offscreen);
    metadata.presentRegime = BuildRegimeMetadata("PipelinedDeferredReadback", present);
    metadata.gpuDrivenBatches = gpuDrivenBatches;
    metadata.renderFeatures = renderFeatures;
    return metadata;
}

// Groups `ungrouped` by pass name - see this function's declaration in
// RenderGraphMetadata.h for the full contract. std::map<std::string, ...>
// gives sorted-by-key iteration for free, which is what makes the result's
// row order independent of `ungrouped`'s own input order.
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
        // A pass surviving in one view but culled in the other must not
        // read as fully culled, and its viewLabel badge must reflect only
        // the view(s) that actually contributed a real, non-culled
        // instance this frame.
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
//
// Explicit, field-by-field, snake_case JSON keys - matching every existing
// NetworkRoutes.cpp JSON body's own key convention, even though the C++
// struct fields are camelCase. Defined bottom-up (a sub-struct's own
// to_json() appears before the first to_json() that embeds it) so ordinary
// unqualified lookup finds each one without relying on ADL alone.

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
        { "gpu_driven_batches", metadata.gpuDrivenBatches },
        { "render_features", metadata.renderFeatures },
    };
}

} // namespace gte::rg

namespace gte {

// See RenderGraphMetadata.h's own matching declaration for why these two
// live here, in namespace gte, rather than inside
// GpuDrivenBatchDebugInfo.h/RenderFeatureDebugEntry.h themselves.

void to_json(nlohmann::json& j, const GpuDrivenBatchDebugInfo& info)
{
    j = nlohmann::json{
        { "batch_name", info.batchName },
        { "instance_count", info.instanceCount },
        { "visible_count", info.visibleCount.has_value() ? nlohmann::json(*info.visibleCount) : nlohmann::json(nullptr) },
    };
}

void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)
{
    j = nlohmann::json{
        { "name", entry.name },
        { "stage", entry.stage },
        { "priority", entry.priority },
        { "blend_mode", entry.blendMode },
        // Host-side enable/disable override, reported via the existing
        // GET /render_graph endpoint.
        { "enabled", entry.enabled },
        // True for a render feature declared by a loaded project, false for
        // a built-in engine feature - see RenderFeatureDebugEntry.h.
        { "is_project_feature", entry.isProjectFeature },
    };
}

} // namespace gte
