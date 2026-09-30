#pragma once

#include "../Renderer/RenderGraph/RenderGraphDebugMetadataSink.h"

#include <cassert>
#include <cstddef>
#include <vector>

namespace gte {

// editor-core-separation-25 campaign - "Core/Editor Separation: PassRecord
// Debug Metadata Sink". The REAL implementation of BOTH
// rg::IPassDebugMetadataSink (write) and rg::IPassDebugMetadataProvider
// (read) - see RenderGraphDebugMetadataSink.h's own header comment for the
// full contract each base class promises. Lives entirely under
// src/Editor/ - i.e. compiled exclusively into the separate `gte_editor`
// static library, never `gte_core` (this codebase has no
// `GTE_ENABLE_EDITOR` preprocessor macro anymore - see AGENTS.md's
// "`gte_core` / `gte_editor` Library Separation" section) - only ever
// constructed/installed by Editor-tier startup code (see PHASE4),
// mirroring EditorGpuMemoryNameOverlay's own placement exactly.
//
// PER-FRAME TABLE, NEVER PERMANENT (source doc, Section 4): a PassRecord
// has no stable identity across frames ("a PassRecord is never 'upserted
// by name'; every AddPass() call mints a brand new PassRecord every
// frame" - RenderGraphTypes.h's own PassRecord doc comment), so a
// permanent, ever-growing map would leak forever across a session. This
// class instead CLEARS-BUT-KEEPS-CAPACITY once per fresh graph
// declaration (BeginFrame()), mirroring RenderPassBlackboard::BeginFrame()'s
// own established clearing discipline - but NEVER its scan-by-key LOOKUP
// shape. m_table is a plain, dense, contiguously-indexed std::vector,
// appended to via exactly one push_back() per OnPassDeclared() call, with
// NO key comparison of any kind, ever - a future maintainer who instead
// reaches for RenderPassBlackboard's own scan-by-key container shape would
// still be functionally correct, just needlessly O(n) per declared pass
// and O(n^2) across one graph's whole declaration, for a feature whose
// entire point is to be free when the Editor is compiled out and cheap
// when it isn't.
//
// declarationIndexThisFrame is always exactly == m_table.size() at the
// moment OnPassDeclared() fires - a dense, monotonically increasing
// 0, 1, 2, ... sequence with no gaps, produced in the exact same order
// PassRecord entries themselves are appended to RenderGraphBuilder's own
// m_passes (since OnPassDeclared() is only ever called from inside
// AddRenderPass(), the ONE call site that fires it - AddPass()/
// AddComputePass() never call it at all, see RenderGraphTypes.h's own
// PassRecord doc comment / source doc Section 2 for why those two
// primitives are permanently out of this campaign's scope).
class FrameDebuggerPassMetadataRecorder final : public rg::IPassDebugMetadataSink,
                                                 public rg::IPassDebugMetadataProvider {
public:
    void OnPassDeclared(std::size_t declarationIndexThisFrame, rg::RenderPassCategory category,
        rg::RenderPassDrawKind drawKind, rg::RenderPassTagMask tags) override
    {
        // See this class's own doc comment above - a plain push_back,
        // never a find/insert-by-key. Fires exactly once per pass, from
        // AddRenderPass() alone.
        assert(declarationIndexThisFrame == m_table.size());
        m_table.push_back(rg::PassDebugMetadata{ category, drawKind, tags });
    }

    // Called exactly once per fresh graph declaration, directly by
    // RenderGraph::Execute() itself (never by RenderGraphBuilder, never by
    // a pass author) - see PHASE4 for the exact call site. Clears entries
    // but keeps whatever capacity was already reserved from a previous
    // high-water mark - mirrors RenderPassBlackboard::BeginFrame()'s own
    // discipline, never its lookup shape (see this class's own doc comment
    // above).
    void BeginFrame() override { m_table.clear(); }

    // Defensive, never-throwing read accessor - see
    // IPassDebugMetadataProvider::QueryPassDebugMetadata()'s own doc
    // comment (RenderGraphDebugMetadataSink.h) for the full contract.
    bool QueryPassDebugMetadata(std::size_t declarationIndex, rg::PassDebugMetadata& outMetadata) const override
    {
        if (declarationIndex >= m_table.size()) {
            return false;
        }
        outMetadata = m_table[declarationIndex];
        return true;
    }

    // TEST-ONLY: how many entries this table currently holds - lets a test
    // assert the table's own size directly, without needing to know any
    // particular declarationIndex's value in advance. Never called by
    // production code.
    std::size_t EntryCountForTesting() const noexcept { return m_table.size(); }

private:
    std::vector<rg::PassDebugMetadata> m_table;
};

} // namespace gte
