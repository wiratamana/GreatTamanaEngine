#pragma once

// Phase 4 (RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md, part 4 of
// the wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "Make Real
// GPU Pictures and Reuse Them": turns Phase 3's still-virtual
// gte::rg::TextureHandle/gte::rg::BufferHandle (each backed only by a
// TextureDesc/BufferDesc - see RenderGraphTypes.h) into a REAL, physical
// gte::RenderTexture/gte::Buffer, backed by Renderer::CreateRenderTexture()/
// CreateBuffer() - and, critically, REUSES an already-allocated resource
// left over from a previous frame whenever this frame's request has an
// IDENTICAL desc, rather than allocating a brand new VkImage/VkDeviceMemory
// every single frame for a resource whose description hasn't changed. See
// this phase's own strategy document for the full reasoning - in
// particular why TextureDesc's PURELY STRUCTURAL operator== (Phase 1 v2's
// fix, removing a v1 `debugName` field that would have silently compared
// pointer identity instead) is what makes this whole pooling mechanism
// trustworthy at all: a wrong operator== here would make this class look
// like it works while silently creating a brand-new pool entry every call,
// forever.
//
// This is the FIRST phase in the whole Render Graph campaign that actually
// touches a live VkDevice (through Renderer) - and therefore the first
// phase that falls into this engine's already-accepted "Tier 2, no
// automated GPU-backed test coverage yet" bucket (see AGENTS.md,
// "Testability & Regression Safety") - exactly the same bucket
// Buffer/RenderTexture/Pipeline/GpuResourceFactory themselves already live
// in. Manual verification (a debug build, driven through Phase 7's real
// Application integration once it lands, checked against the Editor's
// "Memory" panel to confirm the GPU resource COUNT stays stable
// frame-to-frame at steady state) is this phase's accepted verification
// bar - see this phase's own "Step 5: Their Role" for the exact thing to
// check if that ever regresses.
//
// Nothing outside src/Renderer/RenderGraph/ includes this header yet, and
// nothing calls into it from production code yet - Phase 6/7 are the first
// real consumers.

#include "RenderGraphTypes.h"
#include "../Buffer.h"
#include "../RenderTexture.h"

#include <deque>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Owns every physical RenderTexture/Buffer this render graph has EVER
// created, for this pool's entire lifetime - a pool entry, once created, is
// NEVER destroyed/evicted (see this phase's own "What We Will NOT Do":
// stale-entry eviction/trim is explicitly deferred to Phase 9, flagged
// there as a real, worthwhile follow-up once actual memory pressure from
// this is ever observed). This mirrors FramePresenter's own per-swapchain-
// image DepthBuffers precedent exactly (lazily created only once actually
// needed via EnsureDepthBuffersForSwapchain(), then kept alive and reused
// for the rest of that owner's lifetime) - just generalized from "one
// specific, hardcoded depth buffer" to "any resource, of any description,
// that any pass declares via CreateTexture()/CreateBuffer()".
//
// Matching key: `(TextureDesc/BufferDesc, claimed-this-frame == false)`.
// The FIRST unclaimed entry whose desc equals the request is claimed and
// returned; if none matches, a new entry is created and appended. A pool
// entry, once claimed by ANY resource this frame, is excluded from
// matching again until BeginFrame() resets every entry's claim next frame
// - this is what guarantees a pool entry is used by AT MOST ONE virtual
// resource per frame (trivially memory-safe, even though it deliberately
// under-utilizes potential aliasing opportunities between two
// non-overlapping-lifetime resources that happen to share a desc - see
// this phase's own "What We Will NOT Do": real lifetime-aware aliasing is
// Phase 9 backlog). Without this guard, two DIFFERENT transient resources
// with an IDENTICAL desc that are simultaneously live within the same
// frame (e.g. a ping and a pong buffer for a blur pass) could be handed
// the SAME pool entry, corrupting one via writes meant for the other.
//
// An ImportTexture()-declared resource (Phase 2) NEVER goes through this
// pool at all - it resolves directly to its own externally-supplied
// RenderTarget (see TextureImportInfo in RenderGraphBuilder.h). This pool
// exists strictly for GRAPH-OWNED, transient resources.
//
// Stored in a std::deque (not std::vector) specifically because
// AcquireTexture()/AcquireBuffer() return a REFERENCE into an entry that
// must stay valid even after a LATER call in the SAME frame appends a
// brand-new entry - std::deque guarantees references/pointers to existing
// elements are never invalidated by push_back()/emplace_back() at the
// other end (only iterators are), unlike std::vector, which may reallocate
// its whole backing store and dangle every previously-returned reference.
// This is a real correctness requirement, not a style choice: a render
// graph pass author declaring several transient resources in the same
// frame must be able to hold onto every RenderTexture&/Buffer& already
// acquired without any of them becoming a dangling reference the moment a
// LATER resource is acquired for the first time.
//
// Debug names and GpuMemoryTracker integration are a deliberate
// non-feature here: `debugName` threads straight into
// Renderer::CreateRenderTexture()/CreateBuffer()'s own debugName
// parameter, exactly like any other call site - there is no separate,
// parallel memory-tracking mechanism for graph-owned resources (see
// AGENTS.md, "GPU Resource Memory Tracking" - one tracker, Renderer owns
// it, every resource type registers with THAT ONE).
class RenderGraphResourcePool {
public:
    explicit RenderGraphResourcePool(Renderer& renderer) noexcept;

    // Returns an existing pooled RenderTexture whose TextureDesc equals
    // `desc` and that is NOT already claimed by an earlier caller THIS
    // frame (see BeginFrame() below) - or creates a fresh one via
    // Renderer::CreateRenderTexture() if none qualifies. Matching is
    // PURELY `desc == entry.desc` - `debugName` is used ONLY when a
    // brand-new entry actually has to be created (forwarded straight to
    // Renderer::CreateRenderTexture()'s own debugName parameter), and
    // NEVER participates in matching - see RenderGraphTypes.h's own
    // "standing rule" comment on TextureDesc/BufferDesc for why. Never
    // returns a null/dangling reference; this pool owns every RenderTexture
    // it ever creates for its own entire lifetime, same "resize in place,
    // never destroy-then-later-need-again" ownership model
    // ImGuiEditorLayer's own m_gameView/m_sceneView already use.
    RenderTexture& AcquireTexture(const TextureDesc& desc, const char* debugName);

    // Symmetric counterpart for BufferDesc/Buffer - see AcquireTexture()
    // above for the full matching-rule reasoning, which applies identically
    // here. BufferDesc (Phase 1) does not carry a BufferMemoryUsage, unlike
    // Renderer::CreateBuffer()'s own required parameter - a freshly created
    // entry always uses BufferMemoryUsage::GpuOnly (device-local), the
    // natural default for a render-graph-owned transient buffer with no
    // CPU-side access pattern declared anywhere in its desc. No real
    // Phases 1-8 pass exercises this path yet (see
    // RenderGraphBuilder::PassBuilder::ReadBuffer()/WriteBuffer()'s own
    // "for a future compute pass" comment) - this exists purely so
    // CreateBuffer()'s BufferHandle has a real Phase 4 counterpart,
    // completing the same API surface AcquireTexture() already provides.
    Buffer& AcquireBuffer(const BufferDesc& desc, const char* debugName);

    // Call once per frame, BEFORE realizing this frame's compiled graph -
    // marks every pooled entry as "not yet claimed this frame". Mirrors
    // FrameRecorder::BeginFrame()'s own "clear last frame's queue before
    // this frame re-populates it" spirit, applied to per-entry claim flags
    // instead of a draw queue.
    void BeginFrame() noexcept;

private:
    struct TextureEntry {
        TextureDesc desc;
        RenderTexture texture;
        bool claimedThisFrame = false;
    };

    struct BufferEntry {
        BufferDesc desc;
        Buffer buffer;
        bool claimedThisFrame = false;
    };

    Renderer* m_renderer = nullptr;

    // std::deque, not std::vector - see this class's own comment above for
    // why reference stability across a same-frame emplace_back() is a real
    // correctness requirement here, not a style preference.
    std::deque<TextureEntry> m_textureEntries;
    std::deque<BufferEntry> m_bufferEntries;
};

} // namespace gte::rg
