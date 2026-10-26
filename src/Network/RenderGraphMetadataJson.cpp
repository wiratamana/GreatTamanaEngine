#include "RenderGraphMetadataJson.h"

#include "ViewWireName.h"
#include "../Application/GpuTimingText.h"
#include "../Editor/EditorViewNames.h"
#include "../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

namespace gte::Network {

namespace {

template <typename T>
nlohmann::json OptionalToJson(const std::optional<T>& value)
{
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

nlohmann::json RefToJson(const rg::RenderGraphResourceRefMetadata& ref)
{
    return nlohmann::json{
        { "name", ref.name },
        { "kind", ref.kind },
        { "binding_stage_label", ref.bindingStageLabel },
        { "barrier_transition_label", OptionalToJson(ref.barrierTransitionLabel) },
    };
}

nlohmann::json PassToJson(const rg::RenderGraphPassMetadata& pass)
{
    nlohmann::json reads = nlohmann::json::array();
    for (const rg::RenderGraphResourceRefMetadata& ref : pass.reads) {
        reads.push_back(RefToJson(ref));
    }
    nlohmann::json writes = nlohmann::json::array();
    for (const rg::RenderGraphResourceRefMetadata& ref : pass.writes) {
        writes.push_back(RefToJson(ref));
    }
    return nlohmann::json{
        { "name", pass.name },
        { "is_culled", pass.isCulled },
        { "kind", pass.kind },
        { "category", pass.category },
        { "draw_kind", pass.drawKind },
        { "view_scope", ViewScopeLabel(pass.viewScope) },
        { "view", ViewWireName(pass.viewScope) },
        { "instance", pass.instance },
        { "render_pass_event", pass.renderPassEvent },
        { "render_pass_event_order", pass.renderPassEventOrder },
        { "owning_feature_name", pass.owningFeatureName },
        { "reads", std::move(reads) },
        { "writes", std::move(writes) },
        { "draw_call_count", pass.drawCallCount },
        { "triangle_count", pass.triangleCount },
        { "indirect_draw_count", pass.indirectDrawCount },
        { "gpu_timing_text", FormatGpuTimingText(pass.gpuTimingState, pass.gpuRecordedMilliseconds) },
        { "gpu_timing_status", rg::ToString(pass.gpuTimingState) },
        { "gpu_folded_passes", pass.gpuFoldedPasses },
        { "gpu_timing_milliseconds", OptionalToJson(pass.gpuTimingMilliseconds) },
    };
}

nlohmann::json ResourceToJson(const rg::RenderGraphResourceMetadata& resource)
{
    return nlohmann::json{
        { "name", resource.name },
        { "is_imported", resource.isImported },
        { "first_use_pass_index", resource.firstUsePassIndex },
        { "last_use_pass_index", resource.lastUsePassIndex },
        { "first_use_pass_name", OptionalToJson(resource.firstUsePassName) },
        { "last_use_pass_name", OptionalToJson(resource.lastUsePassName) },
    };
}

nlohmann::json RegimeToJson(const rg::RenderGraphRegimeMetadata& regime)
{
    nlohmann::json passes = nlohmann::json::array();
    for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
        passes.push_back(PassToJson(pass));
    }
    nlohmann::json resources = nlohmann::json::array();
    for (const rg::RenderGraphResourceMetadata& resource : regime.resources) {
        resources.push_back(ResourceToJson(resource));
    }
    return nlohmann::json{
        { "regime_name", regime.regimeName },
        { "passes", std::move(passes) },
        { "resources", std::move(resources) },
        { "gpu_frame_status", rg::ToString(regime.gpuFrameStatus) },
        { "gpu_span_milliseconds", OptionalToJson(regime.gpuSpanMilliseconds) },
        { "gpu_folded_passes", regime.gpuFoldedPasses },
        { "dropped_gpu_frames", regime.droppedGpuFrames },
    };
}

nlohmann::json FeatureToJson(const RenderFeatureDebugEntry& entry)
{
    return nlohmann::json{
        { "name", entry.name },
        { "stage", entry.stage },
        { "priority", entry.priority },
        { "blend_mode", entry.blendMode },
        { "enabled", entry.enabled }, // Host-side enable/disable override.
        { "is_project_feature", entry.isProjectFeature }, // Declared by a loaded project.
    };
}

} // namespace

nlohmann::json RenderGraphMetadataToJson(const rg::RenderGraphMetadata& metadata)
{
    nlohmann::json features = nlohmann::json::array();
    for (const RenderFeatureDebugEntry& entry : metadata.renderFeatures) {
        features.push_back(FeatureToJson(entry));
    }
    return nlohmann::json{
        { "schema_version", metadata.schemaVersion },
        { "offscreen_regime", RegimeToJson(metadata.offscreenRegime) },
        { "present_regime", RegimeToJson(metadata.presentRegime) },
        { "render_features", std::move(features) },
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

} // namespace gte::Network
