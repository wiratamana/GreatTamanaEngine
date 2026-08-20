#include "MemoryPanel.h"

#include "../EditorContext.h"
#include "../ImGuiMemoryTracker.h"
#include "../MemoryPanelData.h"
#include "../../Memory/SdlMemoryTracker.h"
#include "../../Renderer/Renderer.h"

#include <imgui.h>

namespace gte {

namespace {

// "CPU (Engine Dependencies)" section - SdlMemoryTracker/ImGuiMemoryTracker
// (src/Memory/SdlMemoryTracker.h, src/Editor/ImGuiMemoryTracker.h) each
// install a byte-counting wrapper around their own library's allocator, so
// these numbers are exact live totals for THAT dependency specifically -
// not a guess, and not the whole process (see GetVmaHeapBudgets()'s section
// below for a real, driver-reported, whole-GPU-heap cross-check, and Task
// Manager/a process explorer for the whole-process CPU RAM ceiling to
// compare all of this against).
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

// "GPU (Tracked by Engine)" section - GpuMemoryTracker's own tally of every
// Buffer/RenderTexture this engine has created and not yet destroyed (see
// Renderer::GetMemoryTotals()/GetMemoryResources()) - unchanged from before
// this function existed, just pulled out of BuildMemoryPanel() so it reads
// the same as the two sections around it.
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
    const std::vector<MemoryRow> rows = BuildMemoryRows(
        entries, [&renderer](GpuResourceHandle handle) { return renderer.GetMemoryDebugName(handle); });

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

// "GPU Heap Budgets (Driver-Reported)" section - the REAL, driver-reported
// usage/budget for every Vulkan memory heap (see
// Renderer::GetVmaHeapBudgets()/VulkanAllocator::GetHeapBudgets()), fetched
// straight from VMA rather than tallied by this engine. This is the section
// that answers the actual question this whole panel exists for: does
// GpuMemoryTracker's total above plausibly account for everything a real
// GPU tool (or Task Manager's dedicated GPU memory column) would show for
// this heap, or is something else - the swapchain's own images, ImGui's own
// Vulkan backend, driver/loader overhead - unaccounted for?
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

} // namespace

void BuildMemoryPanel(EditorContext& /*ctx*/, Renderer& renderer)
{
    ImGui::Begin("Memory");

    BuildCpuDependenciesSection();
    ImGui::Separator();
    BuildGpuTrackedSection(renderer);
    ImGui::Separator();
    BuildGpuHeapBudgetSection(renderer);

    ImGui::End();
}

} // namespace gte
