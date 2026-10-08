#pragma once

#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// Real implementation of rg::IPassDebugMetadataSink (write) and
// rg::IPassDebugMetadataProvider (read). Compiled only into gte_editor -
// never gte_core. A Player-style build links gte_core alone, so this class
// is never constructed there and every installed sink pointer stays null.
//
// A PassRecord has no stable identity across frames, so this table is
// cleared (but keeps capacity) once per fresh graph declaration via
// BeginFrame() - never a permanent, ever-growing map.
//
// declarationIndexThisFrame is always exactly == m_table.size() when
// OnPassDeclared() fires - a dense 0, 1, 2, ... sequence with no gaps,
// matching RenderGraphBuilder's own pass declaration order.
class FrameDebuggerPassMetadataRecorder final : public rg::IPassDebugMetadataSink,
                                                 public rg::IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, rg::RenderPassCategory category,
        rg::RenderPassDrawKind drawKind, rg::RenderPassTagMask tags) override
    {
        assert(declarationIndexThisFrame == m_table.size());
        m_table.push_back(rg::PassDebugMetadata{ category, drawKind, tags });
        m_barrierLabelsByIndex.emplace_back();
    }

    // Called once per fresh graph declaration, directly by RenderGraph::Execute().
    // Clears entries but keeps reserved capacity from the previous high-water mark.
    void BeginFrame() override
    {
        m_table.clear();
        m_barrierLabelsByIndex.clear();
    }

    // Fires once per WRITE usage that actually required a barrier, at execute
    // time, in the exact order RenderGraph::Execute()'s writes-loop applies them.
    // A single pass can legally write the same resource name more than once
    // (e.g. color + depth-stencil attachment of one combined render target) -
    // each occurrence is queued per-name, FIFO, so a later query consumes
    // labels in the same order they were produced instead of collapsing them.
    void OnResourceBarrierApplied(std::size_t declarationIndex, const std::string& resourceName,
        VkImageLayout oldLayout, VkImageLayout newLayout) override
    {
        if (declarationIndex >= m_barrierLabelsByIndex.size()) {
            return; // Defensive - never expected against a real OnPassDeclared()-grown table.
        }
        m_barrierLabelsByIndex[declarationIndex][resourceName].push_back(
            rg::BarrierTransitionLabel(oldLayout, newLayout));
    }

    bool QueryPassDebugMetadata(std::size_t declarationIndex, rg::PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    // Consumes one label from this (declarationIndex, resourceName) pair's FIFO
    // queue, in the same write order RenderGraphSnapshot.cpp's own write loop
    // queries it. Returns false (never throws) once the queue runs dry - e.g.
    // a read-only usage, or more queries than barriers actually applied.
    bool QueryBarrierTransitionLabel(
        std::size_t declarationIndex, const std::string& resourceName, std::string& outLabel) const override
    {
        if (declarationIndex >= m_barrierLabelsByIndex.size()) {
            return false;
        }
        auto& labelQueues = m_barrierLabelsByIndex[declarationIndex];
        const auto it = labelQueues.find(resourceName);
        if (it == labelQueues.end() || it->second.empty()) {
            return false;
        }
        outLabel = it->second.front();
        it->second.pop_front();
        return true;
    }

    // TEST-ONLY: current entry count, for tests that assert table size directly.
    std::size_t EntryCountForTesting() const noexcept { return m_table.size(); }

private:
    std::vector<rg::PassDebugMetadata> m_table;

    // Parallel to m_table. Per declared pass, one FIFO queue of barrier
    // labels per resource name - NOT a single label per name, so repeated
    // writes to the same resource (color attachment + depth-stencil
    // attachment of one combined target) each get their own, correctly
    // ordered label instead of the last write silently overwriting the first.
    // mutable: QueryBarrierTransitionLabel() consumes (pops) as it reads,
    // which is an internal read-cursor detail, not observable sink state.
    mutable std::vector<std::unordered_map<std::string, std::deque<std::string>>> m_barrierLabelsByIndex;
};

} // namespace gte
