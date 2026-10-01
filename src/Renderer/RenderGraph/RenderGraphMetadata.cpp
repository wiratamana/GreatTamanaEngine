#include "RenderGraphMetadata.h"

#include "RenderGraphSnapshotFormatting.h"
#include "RenderPassGroupRegistry.h"

#include <algorithm>
#include <cstddef>
#include <map>

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

// editor-core-separation-22 campaign, PHASE5
// (PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md) - grouping
// helpers for GroupPassMetadataByName() below. All pure, all local to this
// TU (mirrors this file's own existing anonymous-namespace helper
// discipline).

// Short, human display label for an already-resolved ViewScope string
// (RenderGraphPassMetadata::viewScope is always exactly "Shared" /
// "GameView" / "SceneView" - see RenderGraphSnapshotFormatting.cpp's own
// ToString(ViewScope)). A `default:`-shaped fallback is genuinely correct
// here (unlike RenderGraphTypes.cpp's own "no default: case, ever"
// discipline for the REAL enum) since this is a pure UI-label mapper over
// an already-resolved std::string, not an exhaustive enum switch.
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
    return viewScope.c_str(); // defensive fallback - should never happen.
}

// Fixed priority for deterministic ordering when combining more than one
// instance's own short view label into either the row's own viewLabel
// badge or its gpuTimingText per-view breakdown - Game, then Scene, then
// Shared, then anything else (never alphabetical - that would coincidentally
// still put "Game" first, but this is written explicitly, not left to
// coincidence).
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

// Builds the row's own view-label badge text (e.g. "Game+Scene", "Game
// only", "Scene only", "Shared") from `contributors` - the CALLER decides
// which subset of a group's own instances counts as a "contributor" (see
// GroupPassMetadataByName() below: normally the non-culled subset, falling
// back to every instance when the whole group is culled). Distinct short
// labels only, sorted by ShortViewLabelPriority() for determinism, then
// joined with "+". A lone "Game"/"Scene" label gets an " only" suffix
// (matching this phase's own Step 3.2 examples exactly); "Shared" (already
// inherently singular - see ViewScope::Shared's own doc comment) and any
// defensive fallback label do not.
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
        return "(no instance)"; // defensive - GroupPassMetadataByName() never builds a group with zero instances.
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

// Per-view GPU timing breakdown text (e.g. "Game: 0.12 ms, Scene: 0.08
// ms") - locked decision (via ask_questions during this phase's own
// implementation, see PHASE5_COMPLETION_REPORT.md): NEVER summed/maxed,
// since GPU timing is not meaningfully additive/comparable across two
// logically-separate view passes the way draw/triangle counts are. A
// single-instance group shows that one instance's own gpuTimingText
// verbatim (no redundant view-name prefix - the row's own viewLabel badge
// already states the view). Ordered by ShortViewLabelPriority() for the
// same determinism reason BuildViewLabel() above sorts by it, using
// std::stable_sort so two instances that happen to share the exact same
// viewScope (should never happen for a real PerActiveView pass, but this
// is still deterministic if it ever did) keep their original relative
// order from `instances`.
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

// Combined, de-duplicated (order-preserving, first-seen) resource NAMES
// across every instance sharing this group's own name - `reads` selects
// RenderGraphPassMetadata::reads when true, ::writes when false. The full,
// per-instance, kind-resolved breakdown is still available via the group's
// own `instances` field for anything that needs more than just names.
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

// editor-core-separation-22 campaign, PHASE5
// (PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md) - see this
// function's own declaration in RenderGraphMetadata.h for the full contract.
// std::map<std::string, ...> gives sorted-by-key iteration for free (mirrors
// RenderPassToggleRegistry::ListAll()'s own std::sort-by-name discipline
// without needing a second, explicit sort pass here) - this is what makes
// the result's own row order independent of `ungrouped`'s own input order
// (Step 3.5 item (e)).
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
        // A pass surviving in one view but culled in the other must NOT
        // read as fully culled, and its own viewLabel badge must reflect
        // ONLY the view(s) that actually contributed a real, non-culled
        // instance this frame - see RenderGraphGroupedPassMetadata::viewLabel's
        // own doc comment for the "every instance culled" fallback rule.
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
        // editor-core-separation-8 campaign, PHASE2 - the host-side enable/
        // disable override, reported automatically via the ALREADY-SHIPPING
        // GET /render_graph endpoint (no new endpoint needed for this).
        { "enabled", entry.enabled },
        // better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
        // "is_v3" JSON field removed outright, alongside RenderFeatureDebugEntry::isV3
        // itself (meaningless once no plugin of either kind can ever load again).
        // editor-core-separation-23 campaign, PHASE2 - see
        // RenderFeatureDebugEntry.h's own doc comment (isProjectFeature) for
        // the full "why".
        { "is_project_feature", entry.isProjectFeature },
    };
}

} // namespace gte
