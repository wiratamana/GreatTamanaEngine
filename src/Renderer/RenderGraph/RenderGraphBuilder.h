#pragma once

// Phase 2 (RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, part 2 of the
// wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - the
// declarative "describe a frame's drawing jobs" authoring API, built
// directly on top of Phase 1's pure vocabulary (RenderGraphTypes.h).
//
// RenderGraphBuilder lets a future pass author say "I want a texture like
// THIS" (CreateTexture()/ImportTexture()) and "here is a pass that reads/
// writes some of those textures, here is the code that records its draws"
// (AddPass()) - and produces nothing more than an INERT, in-memory
// description of one frame's intended work (Finish() -> CompiledGraphInput).
// No Vulkan call is issued and no physical resource is allocated anywhere
// in this file - see RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 1.
// Compilation (dependency ordering/culling - Phase 3) and execution (real
// Vulkan recording - Phase 6) both come later; this is purely the
// "setup phase" half of the classic two-phase setup/execute render-graph
// pattern (Frostbite's Frame Graph, Unreal's FRDGBuilder).
//
// Nothing outside src/Renderer/RenderGraph/ includes this header yet, and
// nothing here is wired into Application::Run()/Renderer - that is
// deliberate (see this phase's own Step 4, "What We Will NOT Do"). Phase 7
// is the first real production consumer.
//
// Same "Vulkan-header-present-but-Vulkan-call-free" discipline Phase 1
// established: RenderTarget.h (a plain struct of Vulkan handles, no
// Vulkan calls of its own either) is the only Vulkan-adjacent dependency
// here besides RenderGraphTypes.h itself.

#include "RenderGraphTypes.h"
#include "../RenderTarget.h"

#include <volk.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace gte::rg {

// Per-texture-slot side information for a texture the graph does NOT own
// the lifetime of - see RenderGraphBuilder::ImportTexture() below and
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 3.3. Parallel to
// CompiledGraphInput::textureDescs/textureNames (same index), so a
// non-imported (transient) texture's entry here is simply
// `TextureImportInfo{}` (isImported == false) and is never read by Phase 4.
struct TextureImportInfo {
    bool isImported = false;
    // Only meaningful when isImported == true - the already-live resource
    // this handle refers to (the swapchain image acquired this frame, or
    // ImGuiEditorLayer's own persistent Game/Scene RenderTexture). Phase 4
    // must never try to allocate or free this.
    RenderTarget externalTarget{};
    // Only meaningful when isImported == true - the caller-supplied,
    // REQUIRED statement of exactly what VkImageLayout this image is
    // actually in right now. See ImportTexture()'s own comment for why
    // this has no default to silently fall back on.
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// The "raw material" handed off to Phase 3's compiler
// (RenderGraphCompiler::Compile(CompiledGraphInput&&)) - NOT yet
// "compiled" in any real sense (no ordering/culling has happened yet)
// despite the name; named this way so Phase 3 reads naturally as "the
// input a compiler consumes". Bundles every pass declared this frame plus
// the texture/buffer desc tables and their PARALLEL name tables (see
// RenderGraphBuilder's own class comment below for why a resource's name
// lives here, and nowhere inside TextureDesc/BufferDesc themselves).
//
// This hand-off boundary is itself a natural Tier-1 test seam: a test can
// build a RenderGraphBuilder, call Finish(), and assert on the resulting
// CompiledGraphInput's shape directly, with no Phase 3 code needing to
// exist at all - see tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp.
struct CompiledGraphInput {
    std::vector<PassRecord> passes;

    std::vector<TextureDesc> textureDescs;
    std::vector<const char*> textureNames;
    std::vector<TextureImportInfo> textureImportInfo;

    std::vector<BufferDesc> bufferDescs;
    std::vector<const char*> bufferNames;
};

// Owns the whole in-progress description of one frame. A fresh
// RenderGraphBuilder is meant to be built up once per frame (CreateTexture/
// CreateBuffer/ImportTexture/AddPass calls), then consumed exactly once via
// Finish() - it is NOT a persistent, cross-frame object (see this phase's
// own Step 4: no pass/resource declared here is ever "upserted" - a graph
// is rebuilt, in full, from scratch, every single frame).
//
// A resource's human-readable name (CreateTexture()/CreateBuffer()/
// ImportTexture()'s own `name` parameter) is captured HERE, in a table
// parallel to the desc table, and NOWHERE else - see
// RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md's "standing rule" and
// this phase's own Revision Notes for why TextureDesc/BufferDesc
// themselves must never carry a name field again. `name` must be a string
// literal (or otherwise static-storage-duration) const char* - stored by
// pointer, never copied, mirroring GTE_PROFILE_SCOPE's own rule (see
// AGENTS.md, "Profiling").
class RenderGraphBuilder {
public:
    // Declares one pass's reads/writes while its owning AddPass() call's
    // `setup` callback runs - see AddPass() below. Deliberately exposes
    // NOTHING beyond "declare a read/write" - no live VkCommandBuffer, no
    // Renderer&, no GpuResourceFactory& - by design (this phase's own Step
    // 4): if a future requirement seems to need one of those inside
    // `setup`, that requirement belongs in `execute` instead.
    class PassBuilder {
    public:
        explicit PassBuilder(PassRecord& pass) noexcept
            : m_pass(pass)
        {
        }

        void ReadTexture(TextureHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteColorAttachment(TextureHandle handle);
        void WriteDepthStencilAttachment(TextureHandle handle);

        // Symmetric buffer counterparts, for a future compute pass (Phase
        // 9 backlog) - no real Phases 1-8 pass needs these yet, but
        // CreateBuffer()/BufferHandle already exist from Phase 1, so these
        // are added now for the same reason ReadTexture()/
        // WriteColorAttachment() are: so a declared BufferHandle has a way
        // to actually be used inside a pass at all.
        void ReadBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::TransferDst);

    private:
        PassRecord& m_pass;
    };

    // Declares a brand-new TRANSIENT texture this frame - Phase 4 later
    // decides whether it's freshly allocated or reused from a pool of
    // matching-desc resources left over from a previous frame (purely by
    // TextureDesc value equality - see RenderGraphTypes.h). Two
    // CreateTexture() calls with an identical `desc` still mint two
    // DISTINCT handles - handle identity and physical-resource identity
    // are deliberately different concepts (see this phase's own Step 3.4).
    TextureHandle CreateTexture(const char* name, const TextureDesc& desc);
    BufferHandle CreateBuffer(const char* name, const BufferDesc& desc);

    // Wraps an ALREADY-LIVE, externally-owned resource (the swapchain
    // image acquired this frame, or ImGuiEditorLayer's own persistent
    // Game/Scene RenderTexture) as a graph resource, so existing call
    // sites keep working unmodified - see Phase 4's own "imported vs.
    // transient" split. The resulting handle is usable in
    // ReadTexture()/WriteColorAttachment()/WriteDepthStencilAttachment()
    // exactly like a CreateTexture()-minted one - a pass author cannot
    // tell the difference from the handle alone.
    //
    // `currentLayout` is REQUIRED, with no default: the caller must state
    // exactly what VkImageLayout this image is ACTUALLY in right now
    // (VK_IMAGE_LAYOUT_UNDEFINED for a freshly-acquired swapchain image;
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL for a Game/Scene
    // RenderTexture left in that state by last frame's graph - see Phase
    // 5/6 for how this seeds that resource's tracked state). Guessing this
    // wrong is a silent correctness bug (a barrier built with the wrong
    // oldLayout/srcAccessMask), not a compile error - if you don't know
    // what layout a resource is actually in at the point you're importing
    // it, that uncertainty needs resolving upstream first, never guessed
    // at here.
    TextureHandle ImportTexture(const char* name, const RenderTarget& externalTarget, VkImageLayout currentLayout);

    // `name` must be a string literal (mirrors GTE_PROFILE_SCOPE's own
    // static-storage-duration requirement - see AGENTS.md, "Profiling").
    // `setup` runs IMMEDIATELY, synchronously, exactly once, right here in
    // this call - it declares this pass's reads/writes via the
    // PassBuilder& it's handed. `execute` is stored (type-erased into a
    // std::function<void(PassContext&)>) but NOT run here - Phase 6's
    // RenderGraph::Execute() is the only thing that ever invokes it, and
    // only for a pass that survives Phase 3's culling.
    //
    // The two-callback shape is deliberate, mirroring Unreal's
    // FRDGBuilder::AddPass almost exactly: `setup` must never be handed a
    // live VkCommandBuffer (nothing is being recorded yet); `execute` has
    // no further use for a PassBuilder& (every declaration already
    // happened by the time it runs). Fusing them into one callback would
    // either leave an unused parameter or, worse, tempt a future pass
    // author into recording draws inside `setup` - two distinct callback
    // types make that illegal state impossible to express.
    //
    // Template (not a plain std::function parameter) so `setup`/`execute`
    // can each be an ordinary lambda with no explicit std::function
    // wrapping needed at the call site - `execute` is type-erased into
    // PassRecord::execute right here, inside this call.
    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
    {
        assert(name != nullptr && name[0] != '\0' &&
            "RenderGraphBuilder::AddPass requires a non-empty, static-storage-duration pass name");

        m_passes.push_back(PassRecord{});
        PassRecord& pass = m_passes.back();
        pass.name = name;

        PassBuilder passBuilder(pass);
        setup(passBuilder);

        pass.execute = std::function<void(PassContext&)>(std::forward<ExecuteFn>(execute));
    }

    // Consumes this builder, handing its whole in-progress description
    // over to Phase 3's compiler. Safe to call at most meaningfully once
    // per builder instance (a builder is a one-frame-lifetime object, per
    // this class's own comment above) - calling it again would simply
    // return an empty CompiledGraphInput, since every table was moved out.
    CompiledGraphInput Finish();

private:
    std::vector<PassRecord> m_passes;

    std::vector<TextureDesc> m_textureDescs;
    std::vector<const char*> m_textureNames;
    std::vector<TextureImportInfo> m_textureImportInfo;

    std::vector<BufferDesc> m_bufferDescs;
    std::vector<const char*> m_bufferNames;
};

} // namespace gte::rg
