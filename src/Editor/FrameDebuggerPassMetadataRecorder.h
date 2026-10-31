#pragma once

#include "../Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gte {

// Implements rg::IPassDebugMetadataSink (write) and rg::IPassDebugMetadataProvider (read). Editor-only.
// The table is cleared (capacity kept) by BeginFrame(); OnPassDeclared() sees an index equal to m_table.size().
class FrameDebuggerPassMetadataRecorder final : public rg::IPassDebugMetadataSink,
                                                 public rg::IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, rg::RenderPassCategory category,
        rg::RenderPassDrawKind drawKind, std::string_view owningFeatureName) override
    {
        assert(declarationIndexThisFrame == m_table.size());
        m_table.push_back(rg::PassDebugMetadata{ category, drawKind, std::string(owningFeatureName) });
        m_barrierLabelsByIndex.emplace_back();
    }

    // Called once per fresh graph declaration, directly by RenderGraph::Execute().
    // Clears entries but keeps reserved capacity from the previous high-water mark.
    void BeginFrame() override
    {
        m_table.clear();
        m_barrierLabelsByIndex.clear();
    }

    // Fires once per write that needed a barrier, in write order. One pass can write the same resource
    // twice, so labels are queued per name and consumed in order.
    void OnResourceBarrierApplied(
        std::size_t declarationIndex, const std::string& resourceName, std::string transitionLabel) override
    {
        if (declarationIndex >= m_barrierLabelsByIndex.size()) {
            return; // Defensive - never expected against a real OnPassDeclared()-grown table.
        }
        m_barrierLabelsByIndex[declarationIndex][resourceName].push_back(std::move(transitionLabel));
    }

    bool QueryPassDebugMetadata(std::size_t declarationIndex, rg::PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    // Pops one label from the (declarationIndex, resourceName) queue.
    // Returns false once the queue is empty.
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

    // Parallel to m_table. One FIFO queue of barrier labels per resource name, so repeated writes to one
    // resource each keep their own label. mutable: queries pop labels as a read cursor.
    mutable std::vector<std::unordered_map<std::string, std::deque<std::string>>> m_barrierLabelsByIndex;
};

} // namespace gte
