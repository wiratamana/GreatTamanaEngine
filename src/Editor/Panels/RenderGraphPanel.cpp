#include "RenderGraphPanel.h"

#include "../EditorContext.h"
#include "../RenderGraphDotExport.h"
#include "../../Core/Logging.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <string>
#include <vector>

namespace gte {

namespace {

// editor-core-separation-7 campaign, PHASE3
// (PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md) - the ONLY
// new helper this migration needs: rg::RenderGraphPassMetadata::reads/writes
// are now std::vector<rg::RenderGraphResourceRefMetadata> (name+kind pairs,
// PHASE2) rather than a plain std::vector<std::string> - this table only
// ever showed NAMES before this phase (never kind text, confirmed against
// the ORIGINAL BuildPassRow() below), so this extracts just the `.name`
// field before handing the result to rg::JoinNames(), for a truly
// byte-identical visual result. The `.kind` field exists for PHASE4's JSON
// consumer, not because this ImGui table needs to start showing it.
std::vector<std::string> ExtractResourceRefNames(const std::vector<rg::RenderGraphResourceRefMetadata>& refs)
{
    std::vector<std::string> names;
    names.reserve(refs.size());
    for (const rg::RenderGraphResourceRefMetadata& ref : refs) {
        names.push_back(ref.name);
    }
    return names;
}

void BuildPassRow(const rg::RenderGraphPassMetadata& pass)
{
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    if (pass.isCulled) {
        ImGui::TextDisabled("%s", pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    } else {
        ImGui::TextUnformatted(pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    }

    ImGui::TableSetColumnIndex(1);
    if (pass.isCulled) {
        ImGui::TextDisabled("culled");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Culled: no path from this pass's declared writes to this call's own "
                               "final output(s) was found - see RenderGraphCompiler.h.");
        }
    } else {
        ImGui::Text("%u", pass.drawCallCount);
    }

    ImGui::TableSetColumnIndex(2);
    if (!pass.isCulled) {
        ImGui::Text("%u", pass.triangleCount);
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(3);
    if (!pass.isCulled) {
        ImGui::TextUnformatted(pass.gpuTimingText.c_str());
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(4);
    const std::string reads = rg::JoinNames(ExtractResourceRefNames(pass.reads));
    ImGui::TextUnformatted(reads.c_str());

    ImGui::TableSetColumnIndex(5);
    const std::string writes = rg::JoinNames(ExtractResourceRefNames(pass.writes));
    ImGui::TextUnformatted(writes.c_str());
}

void BuildPassTable(const char* tableId, const rg::RenderGraphRegimeMetadata& regime)
{
    if (regime.passes.empty()) {
        ImGui::TextDisabled("No passes were declared the last time this regime ran.");
        return;
    }

    // ImGuiTableFlags_NoSavedSettings is REQUIRED here, not cosmetic - see the
    // matching comment on BuildResourceTable()'s own tableFlags below for the
    // full "why": without it, a column's width/weight can get corrupted (an
    // observed real case: the two stretch columns below, "Reads"/"Writes",
    // persisted into imgui.ini with Weight=nan after this table was first
    // laid out at a degenerate zero/near-zero available width - e.g. the
    // very first frame this panel's dock tab existed but wasn't yet the
    // visible/selected one) and, once written to disk, silently keeps
    // reloading that same NaN weight on every future launch - collapsing
    // both columns down to an unreadable "..", and reportedly crashing the
    // app outright the moment a user tries to drag (expand) one of them
    // back out, since ImGui's stretch-weight redistribution math has no
    // NaN-recovery path. NoSavedSettings makes this table always start each
    // session from the sane, freshly-computed proportional widths declared
    // below, so a corrupted weight can never survive to be reloaded.
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable(tableId, 6, tableFlags)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Reads");
        ImGui::TableSetupColumn("Writes");
        ImGui::TableHeadersRow();

        for (const rg::RenderGraphPassMetadata& pass : regime.passes) {
            BuildPassRow(pass);
        }

        ImGui::EndTable();
    }
}

void BuildResourceTable(const char* tableId, const rg::RenderGraphRegimeMetadata& regime)
{
    if (regime.resources.empty()) {
        ImGui::TextDisabled("No resources were declared the last time this regime ran.");
        return;
    }

    // ImGuiTableFlags_NoSavedSettings is REQUIRED here, not cosmetic - see
    // BuildPassTable()'s own tableFlags comment above for the full "why":
    // this table's own "Lifetime" stretch column hit the exact same
    // persisted-NaN-weight corruption (confirmed directly in a real
    // imgui.ini: "[Table][0xF8B6D9C2,3] ... Column 2 Weight=nan") - fixed the
    // same way, for the same reason.
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable(tableId, 3, tableFlags)) {
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Lifetime");
        ImGui::TableHeadersRow();

        for (const rg::RenderGraphResourceMetadata& resource : regime.resources) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(resource.name.empty() ? "(unnamed)" : resource.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(resource.isImported ? "Imported" : "Transient");

            ImGui::TableSetColumnIndex(2);
            if (resource.firstUsePassIndex < 0) {
                ImGui::TextDisabled("never used (fully culled)");
            } else {
                // firstUsePassName/lastUsePassName are already resolved by
                // BuildRenderGraphMetadata() via the SAME
                // rg::ResolvePassNameAtSurvivingIndex() this table used to
                // call directly (PHASE0_MASTER_STRATEGY.md's Locked Design
                // Decision #10) - always has_value() whenever the matching
                // index is >= 0 (guaranteed by construction), the "?"
                // fallback below is purely defensive.
                ImGui::Text("%s -> %s", resource.firstUsePassName.has_value() ? resource.firstUsePassName->c_str() : "?",
                    resource.lastUsePassName.has_value() ? resource.lastUsePassName->c_str() : "?");
            }
        }

        ImGui::EndTable();
    }
}

void BuildRegimeSection(const char* label, const char* idSuffix, const rg::RenderGraphRegimeMetadata& regime)
{
    ImGui::SeparatorText(label);

    const std::string passTableId = std::string("RgPasses##") + idSuffix;
    BuildPassTable(passTableId.c_str(), regime);

    ImGui::Spacing();
    ImGui::TextDisabled("Resources");
    const std::string resourceTableId = std::string("RgResources##") + idSuffix;
    BuildResourceTable(resourceTableId.c_str(), regime);
}

// GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md,
// Section 3.1) - one small line per eligible batch: "<batch>: N / M
// instances visible" - N is the GPU-computed, culling-survived count
// (visibleCount - a deliberately delayed GPU readback, per
// GpuDrivenBatchDebugInfo's own doc comment), M is the CPU-known real
// instance count. Kept clearly labeled as two DIFFERENT kinds of number
// (PHASE2's own documented rule) - "pending" (never a fabricated 0) for the
// rare frame visibleCount hasn't been read back yet at all.
void BuildGpuDrivenBatchesSection(const std::vector<GpuDrivenBatchDebugInfo>& batches)
{
    ImGui::SeparatorText("GPU-Driven Batches (instances culled this frame)");
    if (batches.empty()) {
        ImGui::TextDisabled(
            "No GPU-driven-eligible batch is live this frame (Game View only - see PHASE0_MASTER_STRATEGY.md's "
            "Locked Design Decision 11/7).");
        return;
    }

    for (const GpuDrivenBatchDebugInfo& batch : batches) {
        const std::string name = batch.batchName.empty() ? "(unnamed)" : batch.batchName;
        if (batch.visibleCount.has_value()) {
            const std::uint32_t culled =
                (batch.instanceCount > *batch.visibleCount) ? (batch.instanceCount - *batch.visibleCount) : 0u;
            ImGui::Text("%s: %u / %u instances visible (%u culled)", name.c_str(), *batch.visibleCount,
                batch.instanceCount, culled);
        } else {
            ImGui::Text("%s: pending / %u instances (GPU readback not yet available)", name.c_str(),
                batch.instanceCount);
        }
    }
}

// editor-core-separation-6 campaign, PHASE7
// (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - one line per loaded
// `IRenderFeatureModule_v2` entry, mirroring
// BuildGpuDrivenBatchesSection()'s own exact shape immediately above: a
// free function taking a small, already-CPU-side-collected
// std::vector<RenderFeatureDebugEntry>, `ImGui::SeparatorText(...)` + a loop
// of `ImGui::Text(...)` calls - no new ImGui widget kind, no new panel, no
// per-frame GPU readback. `entries` is never frozen by this panel's own
// Pause control (unlike the two RenderGraphSnapshot regimes and the
// GPU-driven-batch readout above) - RenderFeatureCompositor::OnPluginsLoaded()
// resolves this ordering exactly ONCE, at plugin-load time, and it never
// changes again for the remaining lifetime of the process, so there is
// nothing for "Pause" to usefully freeze here.
void BuildPluginRenderFeaturesSection(const std::vector<RenderFeatureDebugEntry>& entries)
{
    ImGui::SeparatorText("Plugin Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No loaded plugin implements IRenderFeatureModule_v2 this session.");
        return;
    }

    for (const RenderFeatureDebugEntry& entry : entries) {
        ImGui::Text("[%s] %s - priority %d, blend %s", entry.stage.c_str(), entry.name.c_str(), entry.priority,
            entry.blendMode.c_str());
    }
}

} // namespace

void RenderGraphPanel::Build(EditorContext& /*ctx*/, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries)
{
    ImGui::Begin("Render Graph");

    const bool wasPaused = m_paused;
    ImGui::Checkbox("Pause", &m_paused);
    ImGui::SameLine();
    ImGui::TextDisabled("(freezes only this panel's own display - the graph keeps running underneath)");

    // See RenderGraphPanel.h's own doc comment for why direction 2 (staying
    // paused) and direction 3 (un-pausing) both need no code here at all -
    // every section below simply reads m_paused's current value each frame,
    // exactly like ProfilerPanel::Build() already does for its own Pause.
    //
    // editor-core-separation-7 campaign, PHASE3
    // (PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md) - this
    // panel now builds ONE rg::RenderGraphMetadata per frame (or reuses the
    // frozen one) instead of reaching into RenderGraphSnapshot/
    // GpuDrivenBatchDebugInfo/RenderFeatureDebugEntry directly.
    // BuildRenderGraphMetadata() is called AT MOST once per Build() call:
    // either into m_frozenMetadata on the pause-transition frame, or into
    // liveMetadata for immediate display - never both.
    if (m_paused && !wasPaused) {
        m_frozenMetadata = rg::BuildRenderGraphMetadata(
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback), gpuDrivenBatchDebugInfo,
            renderFeatureEntries);
    }

    rg::RenderGraphMetadata liveMetadata;
    if (!m_paused) {
        liveMetadata = rg::BuildRenderGraphMetadata(
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback),
            renderGraph.LastSnapshot(rg::ExecuteTimingMode::PipelinedDeferredReadback), gpuDrivenBatchDebugInfo,
            renderFeatureEntries);
    }
    const rg::RenderGraphMetadata& metadata = m_paused ? m_frozenMetadata : liveMetadata;

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 - placed FIRST (right after the Pause row), before the two
    // regime sections below - the "instances culled this frame" readout is
    // this panel's own newest, most immediately actionable live signal, and
    // this placement keeps it visible without scrolling past both regimes'
    // own (often much longer) pass/resource tables.
    BuildGpuDrivenBatchesSection(metadata.gpuDrivenBatches);
    ImGui::Spacing();

    // editor-core-separation-6 campaign, PHASE7
    // (PHASE7_RENDER_GRAPH_PANEL_VISIBILITY.md) - placed right after the
    // GPU-Driven Batches section (same "live, actionable ordering signal,
    // shown early, before the two much-longer regime pass/resource tables"
    // placement logic that section's own comment already documents).
    BuildPluginRenderFeaturesSection(metadata.renderFeatures);
    ImGui::Spacing();

    BuildRegimeSection("Offscreen Regime (Game View + Scene View)", "Offscreen", metadata.offscreenRegime);
    ImGui::Spacing();
    BuildRegimeSection("Pipelined Regime (Present)", "Present", metadata.presentRegime);

    // editor-core-separation-7 campaign, PHASE3 - the real "Export DOT"
    // implementation, finally wired up (disabled since the original Render
    // Graph campaign's own Phase 8 - see the old tooltip text this replaces).
    // BuildRenderGraphDot()/ExportRenderGraphDotToFile() both consume this
    // SAME `metadata` object (src/Editor/RenderGraphDotExport.h/.cpp - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #14 for why this
    // lives under src/Editor/, not src/Renderer/RenderGraph/).
    ImGui::Spacing();
    ImGui::SeparatorText("Export");
    if (ImGui::Button("Export DOT")) {
        const std::string path = ExportRenderGraphDotToFile(metadata);
        if (!path.empty()) {
            GTE_LOG_INFO("RenderGraphPanel", "Exported Render Graph DOT file to: " + path);
        } else {
            GTE_LOG_WARNING("RenderGraphPanel", "Failed to export Render Graph DOT file - could not open "
                                                 "render_graph_export.dot for writing.");
        }
    }

    ImGui::End();
}

} // namespace gte
