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
#include "RenderGraphDebugMetadataSink.h" // editor-core-separation-25 campaign, PHASE3
#include "RenderGraphPersistentResourceCache.h" // editor-core-separation-27 campaign, PHASE7
#include "../RenderTarget.h"
#include "../VolumeTarget.h"
#include "../TextureArrayTarget.h" // better-render-pass-3 campaign, BLOCK5

#include <volk.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace gte::rg {

enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared to avoid a circular include (mirrors RenderGraphDebugTextureRegistry.h's own identical precedent).

// Per-texture-slot side information for a texture the graph does NOT own
// the lifetime of - see RenderGraphBuilder::ImportTexture() below and
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 3.3. render-pass-6
// campaign, PHASE5 (item 2.1) - this is the `importInfo` field of the
// matching TextureSlot entry (see TextureSlot below), so a non-imported
// (transient) texture's entry here is simply `TextureImportInfo{}`
// (isImported == false) and is never read by Phase 4.
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
    // Only meaningful when isImported == true - the already-live color/
    // depth sampler for this resource, supplied by the caller at import
    // time (VK_NULL_HANDLE when that sub-resource genuinely has none).
    VkSampler colorSampler = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;
};

// Buffer sibling of TextureImportInfo above - see
// RenderGraphBuilder::ImportBuffer() below and
// GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md, Step 3.2,
// for the motivating use case (a future GPU-vertex-skinning per-model
// output buffer - created ONCE, at model-registration time, and
// re-imported into a freshly-built graph every frame, exactly like the
// Editor's persistent Game/Scene RenderTexture is re-imported as a texture
// every frame today - see TextureImportInfo above). render-pass-6 campaign,
// PHASE5 (item 2.1) - this is the `importInfo` field of the matching
// BufferSlot entry (see BufferSlot below), so a non-imported (transient)
// buffer's entry here is simply `BufferImportInfo{}` (isImported == false)
// and is never read by RenderGraphResourcePool.
struct BufferImportInfo {
    bool isImported = false;
    // Only meaningful when isImported == true - the already-live buffer
    // this handle refers to. RenderGraphResourcePool must never try to
    // allocate or free this - see RenderGraph::EnsureBufferResolved()'s own
    // import branch.
    VkBuffer externalBuffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
};

// VolumeTexture sibling of TextureImportInfo above - Atmosphere Scattering
// campaign, Phase 2 (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md).
// render-pass-6 campaign, PHASE5 (item 2.1) - this is the `importInfo` field
// of the matching VolumeTextureSlot entry (see VolumeTextureSlot below).
// Every VolumeTextureHandle today is created exclusively via
// RenderGraphBuilder::ImportVolumeTexture() below (isImported is therefore
// always true in practice) - there is deliberately no
// CreateVolumeTexture()-requested transient/pooled counterpart yet (see
// this campaign's own Phase 2 completion report for why this was
// deliberately deferred).
struct VolumeTextureImportInfo {
    bool isImported = false;
    // Only meaningful when isImported == true - the already-live resource
    // this handle refers to. RenderGraphResourcePool must never try to
    // allocate or free this (it never even sees a volume texture at all -
    // see this campaign's own Phase 2 analysis).
    VolumeTarget externalTarget{};
    // Only meaningful when isImported == true - see ImportVolumeTexture()'s
    // own comment for why this has no default to silently fall back on.
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// TextureArray sibling of VolumeTextureImportInfo above - better-render-
// pass-3 campaign, BLOCK5. Points at the NEW TextureArrayTarget (NOT
// VolumeTarget or RenderTarget - neither existing plain-view struct has the
// right shape for this resource).
struct TextureArrayImportInfo {
    bool isImported = false;
    TextureArrayTarget externalTarget{};
    VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};


// render-pass-6 campaign, PHASE5 (item 2.1,
// task_manager/render-pass-6/PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md) -
// REPLACES the old "textureDescs/textureNames/textureImportInfo trio, three
// parallel vectors kept in lockstep purely by convention" data shape with
// one struct-of-3-fields per declared resource. Nothing enforced the old
// parallelism at compile time - a future edit that pushed to one array
// without the other two would have been a silent, very-hard-to-debug
// misalignment bug, not a compile error. This struct makes that bug class
// structurally impossible: there is only ONE vector to push onto, and its
// three fields can never independently desynchronize. Mirrors
// BufferSlot/VolumeTextureSlot below exactly.
struct TextureSlot {
    TextureDesc desc;
    const char* name = nullptr;
    TextureImportInfo importInfo;
};

struct BufferSlot {
    BufferDesc desc;
    const char* name = nullptr;
    BufferImportInfo importInfo;
};

struct VolumeTextureSlot {
    VolumeTextureDesc desc;
    const char* name = nullptr;
    VolumeTextureImportInfo importInfo;
};

struct TextureArraySlot {
    TextureArrayDesc desc;
    const char* name = nullptr;
    TextureArrayImportInfo importInfo;
};


// The "raw material" handed off to Phase 3's compiler
// (RenderGraphCompiler::Compile(CompiledGraphInput&&)) - NOT yet
// "compiled" in any real sense (no ordering/culling has happened yet)
// despite the name; named this way so Phase 3 reads naturally as "the
// input a compiler consumes". Bundles every pass declared this frame plus
// the texture/buffer/volume-texture SLOT tables (TextureSlot/BufferSlot/
// VolumeTextureSlot above - render-pass-6 campaign, PHASE5, REPLACING the
// old 9-parallel-vector shape) - see RenderGraphBuilder's own class comment
// below for why a resource's name lives here, and nowhere inside
// TextureDesc/BufferDesc themselves.
//
// This hand-off boundary is itself a natural Tier-1 test seam: a test can
// build a RenderGraphBuilder, call Finish(), and assert on the resulting
// CompiledGraphInput's shape directly, with no Phase 3 code needing to
// exist at all - see tests/Renderer/RenderGraph/RenderGraphBuilderTests.cpp.
struct CompiledGraphInput {
    std::vector<PassRecord> passes;

    std::vector<TextureSlot> textures;
    std::vector<BufferSlot> buffers;

    // Atmosphere Scattering campaign, Phase 2.
    std::vector<VolumeTextureSlot> volumeTextures;

    // better-render-pass-3 campaign, BLOCK5.
    std::vector<TextureArraySlot> textureArrays;

    // Atmosphere Scattering campaign, Phase 6
    // (ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) - fixes a
    // genuine Phase 2 infrastructure gap this phase found: RenderGraphCompiler::
    // Compile()'s `finalOutputs` root set is TextureHandle-only, so a pass
    // whose ONLY write is a VolumeTextureHandle (e.g. this campaign's own
    // aerial-perspective froxel volume) could NEVER be kept alive - its
    // write is structurally unreachable from any texture-only root, so it
    // was always silently culled, no matter what it declared. Rather than
    // changing every existing `RenderGraph::Execute()` `build` callback's
    // return type (which is texture-only, `std::vector<TextureHandle>`, and
    // used by every pre-existing call site), RenderGraphBuilder gained a
    // SEPARATE, opt-in way to mark a VolumeTextureHandle as a required root
    // - see RenderGraphBuilder::KeepVolumeTextureOutput() below.
    // RenderGraphCompiler::Compile() reads THIS field directly off `input`
    // (it already takes `CompiledGraphInput&`) rather than needing a new
    // parameter of its own.
    std::vector<VolumeTextureHandle> finalVolumeTextureOutputs;

    // editor-core-separation-26 campaign, PHASE1
    // (BIG_STEP_2_BUFFER_ROOTS_AND_BLIT_PASSES_2026-09-29.txt, Part A) -
    // the BufferHandle sibling of finalVolumeTextureOutputs immediately
    // above, closing the exact same shape of gap for buffers:
    // RenderGraphCompiler::Compile()'s `finalOutputs` root set is
    // TextureHandle-only, and finalVolumeTextureOutputs (Atmosphere
    // Scattering campaign, Phase 6) is VolumeTextureHandle-only - a pass
    // whose ONLY write is a BufferHandle (e.g. a compute pass that fills a
    // buffer this frame purely for a LATER frame's own ImportBuffer() to
    // read, with no in-frame reader) used to be silently culled no matter
    // what it declared. RenderGraphBuilder gained a SEPARATE, opt-in way
    // to mark a BufferHandle as a required root - see
    // RenderGraphBuilder::KeepBufferOutput() below. RenderGraphCompiler::
    // Compile() reads THIS field directly off `input`, exactly like
    // finalVolumeTextureOutputs.
    std::vector<BufferHandle> finalBufferOutputs;

    // better-render-pass-3 campaign, BLOCK5 - the TextureArrayHandle
    // sibling of finalVolumeTextureOutputs/finalBufferOutputs above - a
    // REQUIREMENT, not an open question, per this campaign's own planning
    // doc: both VolumeTextureHandle and BufferHandle originally shipped
    // without this and both needed it added later once a real pass existed
    // whose ONLY write was that handle with no in-frame reader (a
    // persistent cascade-shadow array re-read only by a LATER frame is
    // exactly this shape). See RenderGraphBuilder::KeepTextureArrayOutput()
    // below.
    std::vector<TextureArrayHandle> finalTextureArrayOutputs;

    // editor-core-separation-27 campaign, PHASE2/PHASE8
    // (BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt,
    // Section 5.1/5.2) - every handle either GetOrCreatePersistentTexture()
    // overload mints (PHASE8) is pushed here too, alongside its normal
    // TextureSlot entry - this is what RenderGraphCompiler::Compile()'s
    // root-marking scan (PHASE3) treats as an ALWAYS-required root, with
    // zero action needed from the pass author, and what
    // RenderGraph::ExecuteCompiledGraph()'s own honest-layout-recording
    // loop (PHASE8) walks at the end of every call. UNLIKE
    // finalVolumeTextureOutputs/finalBufferOutputs above, nothing outside
    // RenderGraphBuilder ever pushes onto this directly - there is no
    // public "KeepPersistentTextureOutput()" method; population is
    // entirely internal to GetOrCreatePersistentTexture() itself (PHASE8).
    std::vector<TextureHandle> persistentCacheTextures;
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

        void ReadTexture(TextureHandle handle, ResourceAccess access = ResourceAccess::ShaderRead,
            bool isDepthResource = false);

        // `clearColor`/`clearDepth` are Phase 7 additions
        // (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
        // std::nullopt (the default) preserves Phase 6's original-only
        // behavior (VK_ATTACHMENT_LOAD_OP_LOAD - existing contents kept);
        // supplying a value switches that attachment's loadOp to
        // VK_ATTACHMENT_LOAD_OP_CLEAR with this exact value.
        //
        // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 -
        // CORRECTED description, replacing the old "the Phases 1-8 MVP never
        // declares more than one color/depth write per pass anyway"
        // assumption (no longer true): WriteColorAttachment() is now
        // callable MORE THAN ONCE per pass, and each call APPENDS a new,
        // ordered entry to PassRecord::colorAttachments (see
        // ColorAttachmentDesc, RenderGraphTypes.h) - attachment INDEX in
        // that list equals the shader's own `layout(location = N) out`
        // index. Each attachment carries its OWN independent, optional
        // clear color (no more "last call's clear color silently wins").
        // Capped at kMaxColorAttachments (8), asserted in the .cpp. For
        // backward compatibility, PassRecord::colorClearValue is ALSO still
        // populated with the exact same value every call supplies (last
        // call wins there, unchanged pre-existing behavior) - kept purely so
        // the 2 pre-existing RenderGraphBuilderTests.cpp tests that assert on
        // it directly keep passing unmodified (see
        // task_manager/mrt-1/PHASE1_COMPLETION_REPORT.md's own "Design
        // decision" section). As of PHASE2 of this campaign,
        // RenderGraph::ExecuteCompiledGraph() no longer reads this field at
        // all - it reads `colorAttachments[i].clearColor` exclusively (see
        // task_manager/mrt-1/PHASE2_COMPLETION_REPORT.md) - so
        // `colorClearValue` is genuinely dead weight in production today,
        // deliberately left in place rather than removed (see that report's
        // own "colorClearValue disposition" note).
        // `WriteDepthStencilAttachment()` is UNCHANGED by this campaign -
        // still exactly one depth attachment per pass, by design.
        void WriteColorAttachment(TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor = std::nullopt);
        void WriteDepthStencilAttachment(TextureHandle handle, std::optional<float> clearDepth = std::nullopt);

        // Phase 6 of the compute-shader campaign
        // (COMPUTE_PHASE6_RENDERGRAPH_INTEGRATION_STRATEGY_v2.md) - a
        // general, NON-ATTACHMENT texture write, distinct from
        // WriteColorAttachment()/WriteDepthStencilAttachment() above (both
        // of which ALSO implicitly mark this pass as needing a real
        // vkCmdBeginRendering bracket - see RenderGraph::Execute()'s own
        // hasColorWrite/hasDepthWrite scan, which correctly excludes this
        // access kind via IsColorAttachmentWriteAccess()/TargetsDepthState(),
        // RenderGraphBarrierPlanner.h). This is how a compute pass declares
        // it writes an `RWTexture` (see
        // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) -
        // `access` defaults to ResourceAccess::ComputeShaderWrite, the only
        // access kind this is meant for today. A true read-modify-write
        // `RWTexture` (a compute shader that both `imageLoad`s and
        // `imageStore`s the same image) declares BOTH
        // `pass.ReadTexture(handle, ResourceAccess::ComputeShaderRead)` AND
        // `pass.WriteTexture(handle)` on the SAME handle - mirroring how
        // ReadBuffer()/WriteBuffer() below are already two separate calls a
        // caller combines for buffers.
        // editor-core-separation-26 campaign (Gap B, PHASE2) - added a third,
        // trailing, DEFAULTED `isDepthResource` parameter mirroring
        // ReadTexture()'s own existing one (see ResourceUsage::isDepthResource,
        // RenderGraphTypes.h). Defaults to `false`, matching every pre-existing
        // call site's implicit behavior exactly (TR3 - zero behavior change).
        // This is the prerequisite a future Blit/Copy pass
        // (RenderGraphBuilder::AddBlitPass(), PHASE5) needs to correctly mark a
        // TransferDst write as targeting a texture's DEPTH half rather than its
        // color half - without it, RenderGraph.cpp's ApplyUsageBarrierIfNeeded()
        // has no way to learn that fact, and a blit into a depth destination
        // would be barriered against the wrong physical image/aspect mask.
        void WriteTexture(TextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite,
            bool isDepthResource = false);

        // Symmetric buffer counterparts, for a future compute pass (Phase
        // 9 backlog) - no real Phases 1-8 pass needs these yet, but
        // CreateBuffer()/BufferHandle already exist from Phase 1, so these
        // are added now for the same reason ReadTexture()/
        // WriteColorAttachment() are: so a declared BufferHandle has a way
        // to actually be used inside a pass at all.
        void ReadBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::TransferDst);

        // Atmosphere Scattering campaign, Phase 2
        // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md) -
        // volume-texture counterparts of ReadBuffer()/WriteBuffer() above,
        // mirroring their exact shape. `access` defaults to
        // ComputeShaderRead/ComputeShaderWrite respectively - the only
        // access kinds a volume texture is meant for today (a compute pass
        // writing/reading an `image3D`, or a later full-screen pass
        // sampling it as `sampler3D` via ShaderRead).
        void ReadVolumeTexture(VolumeTextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderRead);
        void WriteVolumeTexture(VolumeTextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite);

        // better-render-pass-3 campaign, BLOCK5 - WHOLE-ARRAY access only
        // (no per-layer read/write entry point in this block - see
        // TextureArrayDesc's own doc comment, RenderGraphTypes.h, and this
        // campaign's Section 5 for exactly why). `WriteTextureArray()`'s
        // default access mirrors `WriteTexture()`'s general-purpose,
        // non-attachment compute-write convention - there is no
        // attachment-style write for this resource kind in this block.
        void ReadTextureArray(TextureArrayHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteTextureArray(TextureArrayHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite);

        // Declares a write to exactly one layer/face of this array
        // resource (not the whole array - see WriteTextureArray() above,
        // which stays whole-array-only and unmodified). At most one
        // array-layer attachment may be declared per pass - asserted.
        // `clearColor` is only meaningful for a color array
        // (TextureArrayDesc::hasDepth == false); `clearDepth` only for a
        // depth array (hasDepth == true) - passing the wrong one for this
        // resource's own aspect is simply ignored by the executor, never
        // asserted here (this builder has no resolved resource to check
        // yet).
        void WriteArrayLayer(TextureArrayHandle handle, std::uint32_t layerIndex, ResourceAccess access,
            std::optional<std::array<float, 4>> clearColor = std::nullopt, std::optional<float> clearDepth = std::nullopt);

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
    // `colorSampler`/`depthSampler` are this resource's already-live
    // color/depth samplers (VK_NULL_HANDLE only when that sub-resource
    // genuinely has no sampler, e.g. the swapchain).
    TextureHandle ImportTexture(const char* name, const RenderTarget& externalTarget, VkImageLayout currentLayout,
        VkSampler colorSampler = VK_NULL_HANDLE, VkSampler depthSampler = VK_NULL_HANDLE);

    // Buffer sibling of ImportTexture() above - GPU Vertex Skinning
    // campaign, Phase 3
    // (GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md, Step
    // 3.2). Wraps an ALREADY-LIVE, externally-owned VkBuffer (e.g. a
    // per-model GPU skinning output buffer, created once at model
    // registration time and re-imported into a fresh graph every frame) as
    // a graph resource - the resulting handle is usable in
    // ReadBuffer()/WriteBuffer() exactly like a CreateBuffer()-minted one.
    //
    // Unlike ImportTexture(), there is no "currentLayout" (or equivalent
    // "current ResourceState") parameter here at all - a buffer has no
    // image-layout concept, and RenderGraph::EnsureBufferResolved() always
    // seeds an imported buffer's tracked ResourceState fresh (the same
    // "never touched before" default a transient/pooled buffer already
    // starts every Execute() call at), relying on this engine's existing
    // whole-frame fence/semaphore synchronization to make that safe -
    // mirrors EnsureTextureResolved()'s own stage/access-mask seeding for
    // an imported TEXTURE, which likewise never carries a true stage/
    // access value across frames, only its layout (see that function's own
    // comment in RenderGraph.cpp).
    BufferHandle ImportBuffer(const char* name, VkBuffer externalBuffer, VkDeviceSize size);

    // VolumeTexture sibling of ImportTexture() above - Atmosphere
    // Scattering campaign, Phase 2
    // (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md). Takes a
    // plain, non-owning VolumeTarget (see VolumeTarget.h) - mirroring
    // ImportTexture()'s own `const RenderTarget&` convention exactly, NEVER
    // the owning VolumeTexture object itself, so this header stays
    // decoupled from VolumeTexture.h's own (heavier) dependency, exactly
    // like ImportTexture()'s existing RenderTarget.h-only layering. The
    // resulting handle is usable in ReadVolumeTexture()/WriteVolumeTexture()
    // exactly like any other declared handle.
    //
    // `currentLayout` is REQUIRED, with no default - same reasoning as
    // ImportTexture()'s own `currentLayout` parameter above: the caller
    // must state exactly what VkImageLayout this image is ACTUALLY in
    // right now.
    VolumeTextureHandle ImportVolumeTexture(
        const char* name, const VolumeTarget& externalVolumeTarget, VkImageLayout currentLayout);

    // better-render-pass-3 campaign, BLOCK5 - declares a brand-new
    // TRANSIENT (pooled) TextureArray this frame, mirroring CreateTexture()'s
    // own exact shape (NOT ImportVolumeTexture()'s import-only shape -
    // TextureArray IS pooled, unlike VolumeTexture). Two CreateTextureArray()
    // calls with an identical `desc` still mint two DISTINCT handles.
    TextureArrayHandle CreateTextureArray(const char* name, const TextureArrayDesc& desc);

    // TextureArray sibling of ImportTexture()/ImportVolumeTexture() above -
    // wraps an ALREADY-LIVE, externally-owned TextureArrayTarget as a graph
    // resource. `currentLayout` is REQUIRED, with no default - same
    // reasoning as ImportTexture()'s own `currentLayout` parameter.
    TextureArrayHandle ImportTextureArray(
        const char* name, const TextureArrayTarget& externalTarget, VkImageLayout currentLayout);

    // Atmosphere Scattering campaign, Phase 6
    // (ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md) - marks
    // `handle` as a REQUIRED root the compiler must keep alive, the
    // VolumeTextureHandle counterpart of `RenderGraph::Execute()`'s own
    // `build` callback returning a `std::vector<TextureHandle>` -
    // deliberately a SEPARATE call rather than changing that callback's
    // return type (which is texture-only and shared by every existing call
    // site) - see CompiledGraphInput::finalVolumeTextureOutputs above for
    // the full reasoning behind this specific shape. A pass whose only
    // write is a VolumeTextureHandle that is NEVER passed to this method
    // (directly, or read by some other pass that is itself kept alive) is
    // silently culled, exactly like an ordinary TextureHandle that never
    // reaches `finalOutputs`. Safe to call more than once for the same
    // handle (idempotent - RenderGraphCompiler::Compile()'s own root-
    // marking scan only ever needs `handle` to appear at least once).
    void KeepVolumeTextureOutput(VolumeTextureHandle handle);

    // editor-core-separation-26 campaign, PHASE1
    // (BIG_STEP_2_BUFFER_ROOTS_AND_BLIT_PASSES_2026-09-29.txt, Part A) -
    // marks `handle` as a REQUIRED root the compiler must keep alive, the
    // BufferHandle counterpart of KeepVolumeTextureOutput() immediately
    // above - see CompiledGraphInput::finalBufferOutputs above for the
    // full reasoning behind this specific shape. A pass whose only write
    // is a BufferHandle that is NEVER passed to this method (directly, or
    // read by some other pass that is itself kept alive) is silently
    // culled, exactly like an ordinary TextureHandle that never reaches
    // `finalOutputs`. Safe to call more than once for the same handle
    // (idempotent - RenderGraphCompiler::Compile()'s own root-marking scan
    // only ever needs `handle` to appear at least once).
    void KeepBufferOutput(BufferHandle handle);

    // better-render-pass-3 campaign, BLOCK5 - marks `handle` as a REQUIRED
    // root the compiler must keep alive, the TextureArrayHandle counterpart
    // of KeepVolumeTextureOutput()/KeepBufferOutput() above - see
    // CompiledGraphInput::finalTextureArrayOutputs above for the full
    // reasoning. Safe to call more than once for the same handle
    // (idempotent).
    void KeepTextureArrayOutput(TextureArrayHandle handle);

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

    // frame-debugger-6 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md) - identical
    // to AddPass() above, plus stamping PassRecord::viewScope. Every
    // pre-existing 3-argument AddPass() call site is completely unaffected -
    // this is purely an additive overload.
    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().viewScope = viewScope;
    }

    // frame-debugger-5 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_COMPUTE_DISPATCH_CHOKEPOINT_INFRASTRUCTURE.md) -
    // UPDATES this method's own former "purely cosmetic" claim: this is now
    // the ONE place in the whole engine that marks a pass as a real compute
    // dispatch (PassRecord::kind - see RenderGraphTypes.h; RENAMED from the
    // original plain `bool isComputePass` by the Render Pass campaign's own
    // PHASE1, task_manager/render-pass-1). Still otherwise behaviorally
    // identical to plain AddPass() - a pass's actual barrier/attachment/
    // dispatch behavior is still entirely determined by what it declares in
    // `reads`/`writes` via `setup`, never by which entry point created it;
    // this method ONLY additionally stamps `kind`. Every real compute pass
    // in this engine already calls this method (not plain AddPass()) - see
    // PHASE0_MASTER_STRATEGY.md's Step 2.2 for the full, confirmed list - so
    // this one flag alone is enough to make every one of them automatically,
    // generically discoverable by any future consumer (the Editor's Frame
    // Debugger, frame-debugger-5 PHASE2, is the first one).
    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().kind = PassKind::Compute;
    }

    // frame-debugger-6 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md) - identical
    // to AddComputePass() above, plus stamping PassRecord::viewScope. Every
    // pre-existing 3-argument AddComputePass() call site is completely
    // unaffected.
    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddComputePass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().viewScope = viewScope;
    }

    // Render Pass campaign (task_manager/render-pass-1), PHASE1 - the ONE,
    // OFFICIAL entry point every real pass declaration in this engine should
    // use from now on (Application layer AND Renderer layer alike - see
    // AtmosphereLutRenderer.cpp for a Renderer-layer example, PHASE3). A thin,
    // lightweight wrapper around the two pre-existing methods below - it adds
    // NO new capability of its own beyond stamping `kind`/`category` in one
    // place, by design (PHASE0's Locked Design Decision #1: no polymorphic
    // pass-object hierarchy). AddPass()/AddComputePass() themselves are NOT
    // removed or deprecated - they remain the low-level primitives this method
    // (and Tier-1 tests) are built on, and existing test-only call sites are
    // free to keep using them directly.
    // PHASE1 - new, TRAILING, DEFAULTED `drawKind` parameter (see
    // RenderPassDrawKind's own doc comment, RenderGraphTypes.h) - purely
    // descriptive metadata for the Editor Frame Debugger's child-event-
    // labeling purposes (PHASE2 of that campaign), stamped in the same
    // one place `category` already is. A trailing defaulted plain-type
    // parameter added after the two template-deduced lambda parameters
    // does not interact with template argument deduction at all, so
    // EVERY pre-existing call site of this overload (which only ever
    // supplies `name`/`kind`/`viewScope`/`category`/`setup`/`execute`)
    // compiles completely unmodified.
    // render-pass-3 campaign (task_manager/render-pass-3), PHASE1
    // (PHASE1_CORE_VOCABULARY_AND_BLACKBOARD.md) - a SECOND new, TRAILING,
    // DEFAULTED parameter, `renderPassEvent` (see RenderPassEvent's own doc
    // comment, RenderGraphTypes.h), added the exact same way `drawKind` was
    // by the render-pass-2 campaign - mirroring that identical precedent
    // one more time, so every pre-existing call site of EITHER overload
    // (which never mentions this new parameter at all) compiles completely
    // unmodified. This is the ONE new thing RenderGraphBuilder.h's public
    // surface needs for the whole render-pass-3 campaign (PHASE0_MASTER_
    // STRATEGY.md's Locked Design Decision 5) - the new RenderPipeline
    // declaration layer (src/Renderer/RenderGraph/RenderPipeline.h)
    // translates its own opaque RenderPassDesc::order into this parameter
    // before calling into this exact, otherwise-unchanged chokepoint.
    // render-pass-4 campaign, PHASE1
    // (task_manager/render-pass-4/PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md)
    // - the stamped `renderPassEvent` value is cross-checked against
    // this pass's real, declared resource dependencies by
    // RenderGraphCompiler::Compile() - see RenderPassEvent's own doc
    // comment (RenderGraphTypes.h).
    // - the stamped `renderPassEvent` value is now ALSO real, load-bearing
    // ordering input Compile() stable-sorts every pass by - see that same
    // doc comment for the full write-up.
    // render-pass-7 campaign (task_manager/render-pass-7), PHASE1
    // (PHASE1_TAG_VOCABULARY_AND_THREADING.md, "Core Campaign 1 -
    // De-hardcode RenderPassCategory") - a THIRD new, TRAILING, DEFAULTED
    // parameter, `tags` (see RenderPassTagMask's own doc comment,
    // RenderGraphTypes.h), added the exact same way `drawKind`/
    // `renderPassEvent` were by the render-pass-2/render-pass-3 campaigns -
    // mirroring that identical precedent one more time, so every
    // pre-existing call site of EITHER overload (which never mentions this
    // new parameter at all) compiles completely unmodified. Defaults to 0
    // (no tags) - purely descriptive metadata, read by NOTHING in
    // RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp.
    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, ViewScope viewScope, RenderPassCategory category,
        SetupFn&& setup, ExecuteFn&& execute, RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques, RenderPassTagMask tags = 0)
    {
        if (kind == PassKind::Compute) {
            AddComputePass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        } else {
            AddPass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        }
        m_passes.back().renderPassEvent = renderPassEvent; // render-pass-3 campaign, PHASE1 - UNCHANGED, still real ordering input, not migrated
        // editor-core-separation-25 campaign - category/drawKind/tags no
        // longer stored on PassRecord (see RenderGraphTypes.h's own
        // PassRecord doc comment) - forwarded to the installed sink
        // instead, exactly once per declared pass, only if a sink is
        // actually installed (a headless/Player build's builder never has
        // one - this is the ONE branch that build pays for this feature).
        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, drawKind, tags);
        }
    }

    // Convenience overload defaulting `viewScope` to Shared and `category` to
    // General - for the (today, majority of) real call sites that need
    // neither. Mirrors AddPass()'s own pre-existing 3-arg/4-arg overload pair
    // exactly. Also forwards the new trailing, defaulted `drawKind` parameter
    // (Frame Debugger Pass-Ownership campaign, task_manager/render-pass-2,
    // PHASE1), the new trailing, defaulted `renderPassEvent` parameter
    // (render-pass-3 campaign, PHASE1), AND the new trailing, defaulted
    // `tags` parameter (render-pass-7 campaign, PHASE1) - same "trailing
    // defaulted plain-type parameter never touches template deduction"
    // reasoning as the overload above, so every pre-existing 4-argument call
    // site compiles completely unmodified. render-pass-4 campaign, PHASE1 -
    // same cross-check note as the overload above applies here too (this
    // overload simply forwards into it). render-pass-4 campaign, PHASE2 -
    // same "now ALSO real, load-bearing ordering input" note applies here
    // too, for the same reason.
    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, SetupFn&& setup, ExecuteFn&& execute,
        RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques, RenderPassTagMask tags = 0)
    {
        AddRenderPass(name, kind, ViewScope::Shared, RenderPassCategory::General,
            std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute), drawKind, renderPassEvent, tags);
    }

    // editor-core-separation-26 campaign, PHASE5
    // (PHASE5_ADDBLITPASS_BUILDER_ENTRYPOINT.md) - the real, official,
    // first-class pass-declaration entry point for a raw image blit/copy
    // (vkCmdBlitImage) - mirrors AddRenderPass()'s own trailing-defaulted-
    // parameter convention, but is NOT built on top of AddPass()/
    // AddComputePass() (neither accepts a setup/execute callback pair shaped
    // like a blit needs - a blit has no callback at all): this constructs its
    // own PassRecord directly, the ONE new function in this whole campaign
    // that does so. Declares ReadTexture(spec.src, ResourceAccess::TransferSrc,
    // spec.srcIsDepth) and WriteTexture(spec.dst, ResourceAccess::TransferDst,
    // spec.dstIsDepth) internally (using PHASE2's new isDepthResource
    // parameter), stores `spec` on PassRecord::blitCommand, stamps
    // `pass.kind = PassKind::Blit`, and forwards
    // drawKind = RenderPassDrawKind::Blit (ALWAYS this fixed value - never a
    // caller-supplied parameter, since a blit pass's draw-kind is always,
    // definitionally, Blit) to the installed debug-metadata sink, exactly
    // like AddRenderPass() already does for its own category/drawKind/tags
    // parameters.
    //
    // `renderPassEvent` deliberately has NO special-cased default beyond the
    // ordinary RenderPassEvent::Opaques every other pass-declaring method
    // already defaults to: a blit with no real in-frame reader has no data
    // dependency to order it by, so it MUST have an explicit way to be placed
    // at a real RenderPassEvent tier - a caller with a genuine ordering
    // requirement (e.g. "this blit must run AFTER every transparent draw")
    // supplies its own explicit value here, exactly like any other pass.
    //
    // Still pure data as of this phase - PHASE6 supplies the actual
    // vkCmdBlitImage call, directly inside RenderGraph::ExecuteCompiledGraph(),
    // never via a pass-author-supplied callback.
    void AddBlitPass(const char* name, const BlitSpec& spec,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques,
        ViewScope viewScope = ViewScope::Shared,
        RenderPassCategory category = RenderPassCategory::General,
        RenderPassTagMask tags = 0);

    // editor-core-separation-25 campaign - optional, nullable, zero-cost-
    // when-absent. Forwarded into this builder by RenderGraph::Execute()'s
    // own template body (PHASE4), immediately after constructing a fresh
    // RenderGraphBuilder, before that call's own build(builder) callback
    // runs. A test may also call this directly (see RenderGraphSnapshotTests.cpp's
    // own updated tests, PHASE3) - RenderGraphBuilder itself has no
    // opinion about who installs this or how often; it is a plain,
    // unconditional setter.
    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }

    // editor-core-separation-27 campaign, PHASE7 - forwarded into this
    // builder by RenderGraph::Execute()'s own template body, immediately
    // after constructing a fresh RenderGraphBuilder. `timingMode` is needed
    // by GetOrCreatePersistentTexture()'s own regime-aware resize-refusal
    // logic (PHASE8/FR4); `currentFrame` is needed by
    // RenderGraphPersistentResourceCache::Resolve()'s own same-frame
    // double-request guard and age-stamping (PHASE5/PHASE8) - bundled here
    // (never a separate setter for either) since RenderGraph::Execute()
    // already has all three values on hand at exactly the point it makes
    // this one call.
    void SetPersistentResourceCache(
        RenderGraphPersistentResourceCache* cache, ExecuteTimingMode timingMode, std::uint64_t currentFrame) noexcept
    {
        m_persistentCache = cache;
        m_persistentCacheTimingMode = timingMode;
        m_persistentCacheCurrentFrame = currentFrame;
    }

    // editor-core-separation-27 campaign, PHASE8 (BIG_STEP_3, FR1) - the
    // plain, always-correct, always-safe entry point any pass author uses to
    // reach a persistent, named, cross-frame GPU color texture - usable
    // exactly like an ImportTexture()-minted handle in
    // ReadTexture()/WriteTexture()/WriteColorAttachment() afterward. `owner`
    // MUST be a built-in feature's own constant from
    // RenderGraphPersistentResourceOwners.h, or a `_v2`/`_v3` plugin/Project
    // Assembly feature's own already-unique `descriptor.name` (Section 6.2) -
    // never a locally hand-typed literal. Returns a default-constructed
    // (invalid) TextureHandle if this builder has no persistent cache
    // installed (asserted in debug builds - see this class's own
    // SetPersistentResourceCache()) or if the underlying
    // RenderGraphPersistentResourceCache::Resolve() call itself refuses the
    // request (already logged there - see that method's own doc comment) -
    // never a second log here. May THROW std::runtime_error if the
    // underlying RenderTexture construction genuinely fails (Section 6.1) -
    // never caught/swallowed here either.
    TextureHandle GetOrCreatePersistentTexture(const char* owner, const char* name, const TextureDesc& desc);

    // editor-core-separation-27 campaign, PHASE8 (BIG_STEP_3, FR7) - the
    // token-based fast path for a caller's own steady-state hot path: skips
    // the owned-string build and hash-map lookup whenever `token` still
    // references a live entry (RenderGraphPersistentResourceCache::
    // IsTokenLive()) - identical result to the plain overload above either
    // way, since both ultimately funnel through the SAME shared
    // RenderGraphPersistentResourceCache::ResolveAgainstEntry() helper. On
    // the slow path (token not yet live), `token` is refreshed for every
    // subsequent call this session. In debug builds, re-using `token`
    // against a DIFFERENT (owner, name) identity than it was originally
    // resolved against is asserted (RenderGraphPersistentResourceCache::
    // DebugTokenIdentityMatches()) - a token must never be shared across two
    // unrelated identities.
    TextureHandle GetOrCreatePersistentTexture(
        PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc);

    // render-pass-3-campaign (better-render-pass-5 effort), BLOCK 3,
    // PHASE1 - two tiny, read-only accessors over m_passes (below),
    // added SPECIFICALLY so a provider that invokes a Project Assembly
    // callback which declares passes directly against this builder
    // (e.g. the new "PreOpaqueFeatures" provider, Core.cpp) can snapshot
    // "how many passes existed before the callback ran" and "how many
    // exist after", then inspect every NEWLY added pass's own
    // RenderPassEvent - this is the ONLY mechanism in this engine that
    // can catch a pass whose RenderPassEvent tag EXACTLY EQUALS (not
    // strictly later than) a known-bad default, since
    // DetectRenderPassEventContradictions() (RenderGraphCompiler.h) only
    // ever fires on a STRICT inequality. Deliberately tiny/read-only -
    // no new mutation surface, no new invariant to maintain beyond what
    // m_passes already guarantees.
    std::size_t DeclaredPassCount() const noexcept { return m_passes.size(); }

    // `index` must be < DeclaredPassCount() - asserted in debug builds
    // (mirrors this class's own existing AddPass() assert-on-misuse
    // discipline); out-of-range access in a release build is undefined
    // behavior, exactly like any other unchecked std::vector::operator[]
    // use already present in this file.
    RenderPassEvent PassEventAt(std::size_t index) const
    {
        assert(index < m_passes.size() && "RenderGraphBuilder::PassEventAt() - index out of range");
        return m_passes[index].renderPassEvent;
    }

    // True if the pass at declaration index `index` declared any read usage
    // (texture kind only) against `handle`, regardless of its
    // isDepthResource flag - a depth-aspect read and a color-aspect read
    // against the same handle both count as a match. `index` must be <
    // DeclaredPassCount() - asserted in debug builds, mirroring
    // PassEventAt()'s own bound check.
    bool PassReadsTexture(std::size_t index, TextureHandle handle) const
    {
        assert(index < m_passes.size() && "RenderGraphBuilder::PassReadsTexture() - index out of range");
        for (const ResourceUsage& usage : m_passes[index].reads) {
            if (usage.kind == ResourceKind::Texture && usage.texture == handle) {
                return true;
            }
        }
        return false;
    }

    // Consumes this builder, handing its whole in-progress description
    // over to Phase 3's compiler. Safe to call at most meaningfully once
    // per builder instance (a builder is a one-frame-lifetime object, per
    // this class's own comment above) - calling it again would simply
    // return an empty CompiledGraphInput, since every table was moved out.
    CompiledGraphInput Finish();

private:
    // editor-core-separation-27 campaign, PHASE8 - the ONE place either
    // GetOrCreatePersistentTexture() overload mints this frame's real
    // TextureHandle from a successful RenderGraphPersistentResourceCache::
    // Resolve()/ResolveFast() result - calls the pre-existing, unchanged
    // ImportTexture() using `resolved.combinedKey->c_str()` (a pointer into
    // the CACHE's own permanently-stable std::string, TR4) as `name`, then
    // pushes the resulting handle onto m_persistentCacheTextures so
    // RenderGraphCompiler::Compile()'s PHASE3 root-marking fix keeps it
    // alive every frame with zero further action needed from the pass
    // author.
    TextureHandle MintPersistentHandle(const RenderGraphPersistentResourceCache::ResolvedTexture& resolved);

    std::vector<PassRecord> m_passes;

    // editor-core-separation-25 campaign - optional, nullable, zero-cost-
    // when-absent. See SetDebugMetadataSink() above.
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;

    // render-pass-6 campaign, PHASE5 (item 2.1) - REPLACES the old 9
    // parallel vectors (m_textureDescs/m_textureNames/m_textureImportInfo,
    // etc.) with exactly 3 slot vectors - see TextureSlot/BufferSlot/
    // VolumeTextureSlot above.
    std::vector<TextureSlot> m_textures;
    std::vector<BufferSlot> m_buffers;

    // Atmosphere Scattering campaign, Phase 2.
    std::vector<VolumeTextureSlot> m_volumeTextures;

    // better-render-pass-3 campaign, BLOCK5.
    std::vector<TextureArraySlot> m_textureArrays;

    // Atmosphere Scattering campaign, Phase 6 - see
    // CompiledGraphInput::finalVolumeTextureOutputs above.
    std::vector<VolumeTextureHandle> m_finalVolumeTextureOutputs;

    // editor-core-separation-26 campaign, PHASE1 - see
    // CompiledGraphInput::finalBufferOutputs above.
    std::vector<BufferHandle> m_finalBufferOutputs;

    // better-render-pass-3 campaign, BLOCK5 - see
    // CompiledGraphInput::finalTextureArrayOutputs above.
    std::vector<TextureArrayHandle> m_finalTextureArrayOutputs;

    // editor-core-separation-27 campaign, PHASE2/PHASE8 - see
    // CompiledGraphInput::persistentCacheTextures above.
    std::vector<TextureHandle> m_persistentCacheTextures;

    // editor-core-separation-27 campaign, PHASE7 - optional, nullable,
    // zero-cost-when-absent, mirroring m_debugMetadataSink's exact shape
    // (see SetPersistentResourceCache() above). `{}` value-initializes to
    // 0 == ExecuteTimingMode::SynchronousImmediateReadback (RenderGraph.h) -
    // the enumerator NAME itself is not visible here since ExecuteTimingMode
    // is only forward-declared in this header, mirroring
    // RenderGraphDebugTextureRegistry.h's own DebugTextureSnapshot::regime{}
    // identical precedent. Never actually read before SetPersistentResourceCache()
    // overwrites it (RenderGraph::Execute() calls it unconditionally, every
    // call, before build(builder) runs).
    RenderGraphPersistentResourceCache* m_persistentCache = nullptr;
    ExecuteTimingMode m_persistentCacheTimingMode{};
    std::uint64_t m_persistentCacheCurrentFrame = 0;
};

} // namespace gte::rg
