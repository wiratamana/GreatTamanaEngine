#include "RenderGraphPanel.h"

#include "../EditorContext.h"
#include "../RenderGraphDotExport.h"
#include "../../Core/Logging.h"
#include "../../Core/Plugins/RenderFeatureCompositor.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderGraphSnapshotFormatting.h"
#include "../../Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h"
#include "../ImGuiUniqueId.h"

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

// editor-core-separation-8 campaign, PHASE4
// (PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md, Step 3.1) - the new FIRST column,
// "Enabled". Placed before "Pass" (rather than appended at the end) so an
// unchecked box sits immediately to the LEFT of the pass name it controls -
// every other column below is simply shifted by a uniform +1 index. This is
// the first place in this file a checkbox actually MUTATES Core-owned state
// (rg::RenderPassToggleRegistry) directly from the Editor UI, per
// PHASE0_MASTER_STRATEGY.md's Step 2.6 (same thread, no bridge needed).
void BuildPassRow(int rowIndex, const rg::RenderGraphPassMetadata& pass, rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    // task_manager/editor-core-separation-10 campaign, PHASE2 - THE fix for
    // the reported bug: this row's ImGui ID scope is now keyed by its own
    // loop index (always distinct per row, per frame, by construction),
    // never by pass.name alone - so two rows sharing the exact same
    // pass.name (e.g. "AtmosphereSkyViewLutPass" appearing once for Game
    // View and once for Scene View - a real, permanent, INTENTIONAL fact
    // about this table, see PHASE0_MASTER_STRATEGY.md section 2.2) no
    // longer collide as ImGui widgets, even though they intentionally keep
    // sharing the exact same renderPassToggleRegistry STATE (pass.name is
    // still the toggle registry's own lookup key below - that business
    // logic is completely unchanged).
    ScopedUniqueId idScope(rowIndex, "RenderGraphPanel::BuildPassRow", pass.name.c_str());

    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    bool enabled = renderPassToggleRegistry.IsEnabled(pass.name);
    // Uniqueness now comes entirely from the ScopedUniqueId scope above -
    // this literal, un-suffixed label is intentional and correct.
    if (ImGui::Checkbox("##Enabled", &enabled)) {
        renderPassToggleRegistry.SetEnabled(pass.name, enabled);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Unchecking this disables \"%s\" starting next frame - it will stop appearing in this "
                           "table entirely once disabled (see the \"Disabled Built-In Passes\" section below). "
                           "If this same name appears in BOTH the Offscreen Regime table (Game View + Scene View "
                           "share it), toggling it here affects EVERY row with this exact name at once - there is "
                           "no per-view control (see PHASE0_MASTER_STRATEGY.md's Step 2.1).",
            pass.name.c_str());
    }

    ImGui::TableSetColumnIndex(1); // was 0
    if (pass.isCulled) {
        ImGui::TextDisabled("%s", pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    } else {
        ImGui::TextUnformatted(pass.name.empty() ? "(unnamed)" : pass.name.c_str());
    }

    ImGui::TableSetColumnIndex(2); // was 1
    if (pass.isCulled) {
        ImGui::TextDisabled("culled");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Culled: no path from this pass's declared writes to this call's own "
                               "final output(s) was found - see RenderGraphCompiler.h.");
        }
    } else {
        ImGui::Text("%u", pass.drawCallCount);
    }

    ImGui::TableSetColumnIndex(3); // was 2
    if (!pass.isCulled) {
        ImGui::Text("%u", pass.triangleCount);
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(4); // was 3
    if (!pass.isCulled) {
        ImGui::TextUnformatted(pass.gpuTimingText.c_str());
    } else {
        ImGui::TextDisabled("-");
    }

    ImGui::TableSetColumnIndex(5); // was 4
    const std::string reads = rg::JoinNames(ExtractResourceRefNames(pass.reads));
    ImGui::TextUnformatted(reads.c_str());

    ImGui::TableSetColumnIndex(6); // was 5
    const std::string writes = rg::JoinNames(ExtractResourceRefNames(pass.writes));
    ImGui::TextUnformatted(writes.c_str());
}

void BuildPassTable(
    const char* tableId, const rg::RenderGraphRegimeMetadata& regime, rg::RenderPassToggleRegistry& renderPassToggleRegistry)
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
    // editor-core-separation-8 campaign, PHASE4 - column count is now 7 (was
    // 6): the new "Enabled" column is the FIRST TableSetupColumn() call
    // below, matching BuildPassRow()'s own new column-index-0 checkbox
    // exactly. Every other TableSetupColumn() call below is unchanged.
    if (ImGui::BeginTable(tableId, 7, tableFlags)) {
        ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("GPU Time", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("Reads");
        ImGui::TableSetupColumn("Writes");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < regime.passes.size(); ++i) {
            BuildPassRow(static_cast<int>(i), regime.passes[i], renderPassToggleRegistry);
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

void BuildRegimeSection(const char* label, const char* idSuffix, const rg::RenderGraphRegimeMetadata& regime,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    ImGui::SeparatorText(label);

    const std::string passTableId = std::string("RgPasses##") + idSuffix;
    BuildPassTable(passTableId.c_str(), regime, renderPassToggleRegistry);

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
//
// editor-core-separation-8 campaign, PHASE4
// (PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md, Step 3.3) - gains a per-entry
// "Enabled" checkbox and an editable priority field, both live-mutating
// `renderFeatureCompositor` directly (main-thread-only, same as
// BuildPassRow()'s new checkbox above - see PHASE0_MASTER_STRATEGY.md's Step
// 2.6). `renderFeatureCompositor == nullptr` (no loaded _v2 plugin / a
// degraded build) means the widgets still render but any edit is silently a
// no-op - mirrors this whole campaign's "null bridge/pointer degrades
// gracefully, never crashes" discipline; deliberately NOT wrapped in
// ImGui::BeginDisabled() for this, since a transient null is not a real,
// reachable, steady-state UI mode worth a special disabled-look here.
void BuildPluginRenderFeaturesSection(
    const std::vector<RenderFeatureDebugEntry>& entries, RenderFeatureCompositor* renderFeatureCompositor)
{
    ImGui::SeparatorText("Plugin Render Features");
    if (entries.empty()) {
        ImGui::TextDisabled("No loaded plugin implements IRenderFeatureModule_v2 this session.");
        return;
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RenderFeatureDebugEntry& entry = entries[i];
        // task_manager/editor-core-separation-10 campaign, PHASE2 - was a
        // bare ImGui::PushID(entry.name.c_str())/PopID() pair with the same
        // LATENT "two entries could share a name" shape as the reported
        // bug, even though no real collision has ever been observed here.
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildPluginRenderFeaturesSection", entry.name.c_str());

        bool enabled = entry.enabled;
        if (ImGui::Checkbox("##FeatureEnabled", &enabled) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeatureEnabled(entry.name, enabled);
        }
        ImGui::SameLine();

        int priority = entry.priority;
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::InputInt("##FeaturePriority", &priority) && renderFeatureCompositor != nullptr) {
            renderFeatureCompositor->SetFeaturePriority(entry.name, priority);
        }
        ImGui::SameLine();

        ImGui::Text("[%s] %s - blend %s%s", entry.stage.c_str(), entry.name.c_str(), entry.blendMode.c_str(),
            entry.enabled ? "" : " (DISABLED)");

        // editor-core-separation-9 campaign, PHASE4
        // (PHASE4_BLACKBOARD_AND_DIAGNOSTICS_INTEGRATION.md, Step 3.3.5) - a
        // small, additive "v3" label, added after a real, live confirmed
        // gap: without it, the ONLY way to tell a _v3 (multi-pass, generic
        // resource-graph) plugin row apart from a _v2 (fixed-op) one is by
        // eyeballing the plugin's own chosen name string - never a real,
        // structural signal a third-party plugin author is obligated to
        // follow.
        if (entry.isV3) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[v3]");
        }
        // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor
        // handles it when idScope goes out of scope at the end of this
        // loop body.
    }
}

// editor-core-separation-8 campaign, PHASE4
// (PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md, Step 3.2) - the ONLY place a
// built-in pass the caller has switched OFF is still visible at all (see
// PHASE0_MASTER_STRATEGY.md's Step 2.4 - a disabled pass leaves ZERO trace
// in rg::RenderGraphMetadata, since it is never declared into the graph at
// all). Reads renderPassToggleRegistry.ListAll() directly - the registry
// itself, not the metadata, is this section's own source of truth.
//
// Uses ScopedUniqueId (see ImGuiUniqueId.h) exactly like BuildPassRow()
// above - task_manager/editor-core-separation-10 campaign, PHASE2.
void BuildDisabledBuiltInPassesSection(rg::RenderPassToggleRegistry& renderPassToggleRegistry)
{
    const std::vector<rg::RenderPassToggleState> allStates = renderPassToggleRegistry.ListAll();
    std::vector<rg::RenderPassToggleState> disabled;
    for (const rg::RenderPassToggleState& state : allStates) {
        if (!state.enabled) {
            disabled.push_back(state);
        }
    }

    ImGui::SeparatorText("Disabled Built-In Passes");
    if (disabled.empty()) {
        ImGui::TextDisabled("Every known built-in pass is currently enabled.");
        return;
    }
    for (std::size_t i = 0; i < disabled.size(); ++i) {
        const rg::RenderPassToggleState& state = disabled[i];
        // task_manager/editor-core-separation-10 campaign, PHASE2 - same
        // fix as BuildPassRow() above: index-scoped, not name-scoped.
        ScopedUniqueId idScope(static_cast<int>(i), "RenderGraphPanel::BuildDisabledBuiltInPassesSection", state.name.c_str());

        bool enabled = false; // always false here by construction (this loop only ever sees disabled entries).
        if (ImGui::Checkbox("##Enabled", &enabled)) {
            renderPassToggleRegistry.SetEnabled(state.name, enabled);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(state.name.c_str());
        if (!state.everDeclaredThisSession) {
            ImGui::SameLine();
            ImGui::TextDisabled("(never run yet this session)");
        }
    }
}

} // namespace

void RenderGraphPanel::Build(EditorContext& ctx, const rg::RenderGraph& renderGraph,
    const std::vector<GpuDrivenBatchDebugInfo>& gpuDrivenBatchDebugInfo,
    const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
    rg::RenderPassToggleRegistry& renderPassToggleRegistry, RenderFeatureCompositor* renderFeatureCompositor)
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
    BuildPluginRenderFeaturesSection(metadata.renderFeatures, renderFeatureCompositor);
    ImGui::Spacing();

    // editor-core-separation-21 campaign, PHASE2
    // (PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md) - snapshotted
    // BEFORE any of this panel's own checkbox-drawing sections below run this
    // frame, so the comparison right after BOTH BuildRegimeSection() calls
    // (the last section whose own BuildPassRow() can call
    // renderPassToggleRegistry.SetEnabled()) can tell "did a checkbox click
    // somewhere in this whole block actually flip anything". See
    // EditorContext::renderPassToggleRegistryChangedThisFrame's own doc
    // comment for the full contract this feeds.
    const std::vector<rg::RenderPassToggleState> toggleStatesBeforeThisPanelsOwnUi = renderPassToggleRegistry.ListAll();
    // editor-core-separation-8 campaign, PHASE4
    // (PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md, Step 3.4) - placed immediately
    // after Plugin Render Features and BEFORE the two regime sections, so a
    // caller sees "what's currently OFF" before scrolling past the (often
    // much longer) live pass/resource tables - mirrors this file's own
    // existing placement rationale for GPU-Driven Batches/Plugin Render
    // Features above ("this panel's own newest, most immediately actionable
    // live signal, shown early").
    BuildDisabledBuiltInPassesSection(renderPassToggleRegistry);
    ImGui::Spacing();

    BuildRegimeSection(
        "Offscreen Regime (Game View + Scene View)", "Offscreen", metadata.offscreenRegime, renderPassToggleRegistry);
    ImGui::Spacing();
    BuildRegimeSection("Pipelined Regime (Present)", "Present", metadata.presentRegime, renderPassToggleRegistry);

    // editor-core-separation-21 campaign, PHASE2 - the "after" half of the
    // comparison started above: if any checkbox this panel drew anywhere in
    // this whole block actually changed the registry, tell the Frame
    // Debugger (via the shared EditorContext flag) to request a fresh
    // capture on the very next frame it runs, instead of silently leaving a
    // stale, now-contradicting event tree on screen (PHASE1_COMPLETION_REPORT.md's
    // confirmed root cause).
    if (rg::DidRenderPassToggleEnabledStatesChange(toggleStatesBeforeThisPanelsOwnUi, renderPassToggleRegistry.ListAll())) {
        ctx.renderPassToggleRegistryChangedThisFrame = true;
    }
    // editor-core-separation-8 campaign, PHASE4 (Step 3.4) - a natural final
    // "debug toggles" grouping, placed AFTER both regime sections and BEFORE
    // "Export" - the exact same EditorContext bools ScenePanel.cpp's own two
    // checkboxes already flip (never a copy/duplicate field), so this panel
    // finally becomes a genuine one-stop place to toggle them too. These are
    // PLAIN, direct EditorContext field writes, NOT routed through
    // IEditorLayer - mirrors ScenePanel.cpp's own existing checkboxes exactly
    // (see PHASE4_RENDER_GRAPH_PANEL_CONTROLS.md's Step 3.4 for why the 2
    // IEditorLayer setters PHASE3 added exist only for the HTTP path,
    // PHASE5).
    ImGui::Spacing();
    ImGui::SeparatorText("Debug Passes");
    ImGui::Checkbox("Show Compute Blur (debug)", &ctx.showBlurredSceneOutput);
    ImGui::Checkbox("Show GBuffer Validation (debug)", &ctx.showGBufferValidationOutput);

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
