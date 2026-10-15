#include "MemoryPanel.h"

#include "../EditorContext.h"
#include "../EditorGpuMemoryNameOverlay.h"
#include "../ImGuiMemoryTracker.h"
#include "../MemoryPanelData.h"
#include "../../Memory/SdlMemoryTracker.h"
#include "../../Renderer/Renderer.h"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace gte {

namespace {

// Live byte totals of the SDL and ImGui allocators (exact for those libraries, not the whole process).
void BuildCpuDependenciesSection()
{
    if (!ImGui::CollapsingHeader("CPU (Engine Dependencies)", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Text("SDL:       %s across %llu allocations", FormatBytes(SdlMemoryTracker::LiveBytes()).c_str(),
        static_cast<unsigned long long>(SdlMemoryTracker::LiveAllocationCount()));
    ImGui::Text("Dear ImGui: %s across %llu allocations", FormatBytes(ImGuiMemoryTracker::LiveBytes()).c_str(),
        static_cast<unsigned long long>(ImGuiMemoryTracker::LiveAllocationCount()));
}

// Every buffer and render texture the engine created and has not destroyed yet.
void BuildGpuTrackedSection(Renderer& renderer)
{
    if (!ImGui::CollapsingHeader("GPU (Tracked by Engine)", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const GpuMemoryTracker::Totals totals = renderer.GetMemoryTotals();

    ImGui::Text("Total GPU Memory: %s", FormatBytes(totals.totalBytes).c_str());
    ImGui::Separator();
    ImGui::Text("Buffers:  %s across %zu", FormatBytes(totals.bufferBytes).c_str(), totals.bufferCount);
    ImGui::Text("Textures: %s across %zu", FormatBytes(totals.textureBytes).c_str(), totals.textureCount);
    ImGui::Separator();
    ImGui::Text("%s: %s", ToString(GpuMemoryLocation::GpuOnly), FormatBytes(totals.gpuOnlyBytes).c_str());
    ImGui::Text("%s: %s", ToString(GpuMemoryLocation::CpuOnly), FormatBytes(totals.cpuOnlyBytes).c_str());
    ImGui::Text("%s: %s", ToString(GpuMemoryLocation::Shared), FormatBytes(totals.sharedBytes).c_str());
    ImGui::Separator();

    const std::vector<GpuMemoryTracker::Entry> entries = renderer.GetMemoryResources();
    // Debug names live in the Editor overlay, never in the renderer.
    const std::vector<MemoryRow> rows =
        BuildMemoryRows(entries, [](GpuResourceHandle handle) { return EditorGpuMemoryNameOverlay::GetDebugName(handle); });

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("MemoryResourcesTable", 5, tableFlags, ImVec2(0.0f, 200.0f))) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Format");
        ImGui::TableSetupColumn("Location");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (const MemoryRow& row : rows) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name.empty() ? "(unnamed)" : row.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ToString(row.type));

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(ToString(row.format).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(ToString(row.location));

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(FormatBytes(row.sizeBytes).c_str());
        }

        ImGui::EndTable();
    }

    if (rows.empty()) {
        ImGui::TextDisabled("(no live GPU resources)");
    }
}

// Driver-reported usage and budget per Vulkan memory heap (straight from VMA, not tallied by the engine).
void BuildGpuHeapBudgetSection(Renderer& renderer)
{
    if (!ImGui::CollapsingHeader("GPU Heap Budgets (Driver-Reported)", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const std::vector<VmaBudget> budgets = renderer.GetVmaHeapBudgets();
    const std::vector<HeapBudgetRow> rows = BuildHeapBudgetRows(budgets);

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("HeapBudgetsTable", 4, tableFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Heap");
        ImGui::TableSetupColumn("VMA Allocated");
        ImGui::TableSetupColumn("Driver Usage");
        ImGui::TableSetupColumn("Driver Budget");
        ImGui::TableHeadersRow();

        for (const HeapBudgetRow& row : rows) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%u", row.heapIndex);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(
                FormatBlockSummary(row.vmaBlockBytes, row.vmaBlockCount, row.vmaAllocationCount).c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(FormatBytes(row.usageBytes).c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(FormatBytes(row.budgetBytes).c_str());
        }

        ImGui::EndTable();
    }

    if (rows.empty()) {
        ImGui::TextDisabled("(no memory heaps reported)");
    }
}

// Descriptor pool use per descriptor type: in use / capacity, with the high-water mark.
void BuildDescriptorPoolSection(Renderer& renderer)
{
    if (!ImGui::CollapsingHeader("Descriptor Pools", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    const DescriptorPoolStats stats = renderer.GetDescriptorPoolStats();
    ImGui::Text("Pools: %zu", stats.poolCount);

    struct Row {
        const char* label;
        std::uint32_t used;
        std::uint32_t capacity;
        std::uint32_t highWater;
    };
    const Row rows[] = {
        { "Sets", stats.used.sets, stats.capacity.sets, stats.highWater.sets },
        { "Storage buffers", stats.used.storageBuffers, stats.capacity.storageBuffers, stats.highWater.storageBuffers },
        { "Storage images", stats.used.storageImages, stats.capacity.storageImages, stats.highWater.storageImages },
        { "Combined samplers", stats.used.combinedSamplers, stats.capacity.combinedSamplers,
            stats.highWater.combinedSamplers },
        { "Uniform buffers", stats.used.uniformBuffers, stats.capacity.uniformBuffers, stats.highWater.uniformBuffers },
    };

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("DescriptorPoolsTable", 4, tableFlags, ImVec2(0.0f, 0.0f))) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("In use");
        ImGui::TableSetupColumn("Capacity");
        ImGui::TableSetupColumn("High water");
        ImGui::TableHeadersRow();

        for (const Row& row : rows) {
            ImGui::TableNextRow();
            const bool warn = DescriptorBudget::Fraction(row.used, row.capacity) > DescriptorBudget::kWarnFraction;
            if (warn) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
            }
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", row.used);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", row.capacity);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", row.highWater);
            if (warn) {
                ImGui::PopStyleColor();
            }
        }

        ImGui::EndTable();
    }
}

} // namespace

void BuildMemoryPanel(EditorContext& /*ctx*/, Renderer& renderer)
{
    ImGui::Begin("Memory");

    BuildCpuDependenciesSection();
    ImGui::Separator();
    BuildGpuTrackedSection(renderer);
    ImGui::Separator();
    BuildGpuHeapBudgetSection(renderer);
    ImGui::Separator();
    BuildDescriptorPoolSection(renderer);

    ImGui::End();
}

} // namespace gte
