#include "RenderGraphMetadata.h"

#include "RenderGraphSnapshotFormatting.h"
#include "RenderPassGroupRegistry.h"

#include <cstddef>

namespace gte::rg {

namespace {

// Zips `names`/`kinds` (same-length parallel arrays on RenderGraphPassSnapshot
// - a hard, load-bearing, pre-existing invariant elsewhere in the engine, see
// PHASE2_RENDERGRAPH_METADATA_MODEL_AND_JSON.md's own Step 3.2) into the
// JSON-friendlier "one array of {name,kind} structs" shape. Defensively
// clamped to the shorter of the two vectors rather than asserting - this
// phase does not need to newly defend an invariant that already holds
// elsewhere, but a silent out-of-bounds read would be strictly worse than a
// silently-shorter result.
std::vector<RenderGraphResourceRefMetadata> BuildResourceRefs(
    const std::vector<std::string>& names, const std::vector<ResourceKind>& kinds)
{
    std::vector<RenderGraphResourceRefMetadata> refs;
    const std::size_t count = names.size() < kinds.size() ? names.size() : kinds.size();
    refs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        refs.push_back(RenderGraphResourceRefMetadata{ names[i], ToString(kinds[i]) });
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

    const std::optional<std::size_t> groupIndex = FindPassGroupIndexForTags(pass.tags);
    metadata.tagGroupLabel = groupIndex.has_value()
        ? std::optional<std::string>(PassGroupLabelUiHeadingAt(*groupIndex))
        : std::nullopt;

    metadata.reads = BuildResourceRefs(pass.readNames, pass.readKinds);
    metadata.writes = BuildResourceRefs(pass.writeNames, pass.writeKinds);

    // Always falls out correctly for a culled pass, without any special-case
    // branch here - a culled pass's own `stats` is always left at its
    // default (empty DrawStats, Absent GpuTimingSample) by
    // BuildRenderGraphSnapshot() itself. See that function's own doc comment
    // (RenderGraphSnapshot.h) for why.
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

// --- JSON (namespace gte::rg) --------------------------------------------
//
// Explicit, field-by-field, snake_case JSON keys - matching every EXISTING
// NetworkRoutes.cpp JSON body's own key convention (e.g.
// "frames_since_update", "has_depth") - never camelCase in the JSON itself,
// even though the C++ struct fields are camelCase. Defined bottom-up (a
// sub-struct's own to_json() always appears BEFORE the first to_json() that
// embeds it) so ordinary unqualified lookup finds each one without relying
// on ADL alone to reach across a forward-declaration gap.

void to_json(nlohmann::json& j, const RenderGraphResourceRefMetadata& ref)
{
    j = nlohmann::json{
        { "name", ref.name },
        { "kind", ref.kind },
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
        { "tag_group_label",
            pass.tagGroupLabel.has_value() ? nlohmann::json(*pass.tagGroupLabel) : nlohmann::json(nullptr) },
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

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6 - see
// RenderGraphMetadata.h's own matching declaration/comment for the full
// reasoning behind why these two live here, in namespace gte, rather than
// inside GpuDrivenBatchDebugInfo.h/RenderFeatureDebugEntry.h themselves.

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
    };
}

} // namespace gte
