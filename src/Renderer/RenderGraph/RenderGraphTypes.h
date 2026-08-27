#pragma once

// Phase 1 (RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md, part of the
// wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - the render
// graph's entire VOCABULARY, and nothing else: every handle, enum, and
// plain descriptor struct later phases need to even talk about "a
// transient texture," "a pass," or "a read/write dependency". Mirrors
// src/Renderer/DrawStats.h/src/Renderer/GpuTiming.h's own precedent
// exactly - the pure data/math half of a Vulkan-adjacent feature is
// designed and tested FIRST, fully independent of the live-device half
// that will consume it later (Phase 4/5/6).
//
// Nothing in this file is called from anywhere yet, and nothing outside
// src/Renderer/RenderGraph/ includes this header yet - that is deliberate
// (see the Phase 1 doc's own Step 4 "What We Will NOT Do"). Phase 2 is the
// first consumer (the AddPass()/CreateTexture()/ImportTexture() builder
// API built on top of these types).
//
// Deliberately Vulkan-header-PRESENT-but-Vulkan-call-FREE: VkFormat/
// VkDeviceSize/VkBufferUsageFlags are the only Vulkan-header types used
// here (via a minimal <volk.h> include, exactly like FrameRecorder.h's own
// DrawItem - see that file's header comment), and no Vulkan device/queue
// call of any kind appears anywhere in this file. This keeps the whole
// file hand-verifiable/testable with no live VkDevice at all (see
// tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp) - the same "Tier 1"
// bucket DrawStats.h/GpuTiming.h already occupy, just one small notch
// closer to Vulkan than GpuTiming.h itself (which has zero Vulkan-header
// dependency at all - a deliberate, documented difference, not an
// inconsistency; see the Phase 1 doc's own Step 3.1 for the full
// reasoning).
//
// Everything below lives in namespace gte::rg (a nested namespace,
// deliberately distinct from plain gte::Mesh/gte::Buffer/gte::Texture2D)
// so every render-graph symbol is unambiguously grep-able and never
// collides with Renderer's own existing type names - e.g. rg::TextureDesc
// vs. plain Texture2D.

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace gte::rg {

// Sentinel "no slot assigned yet" index shared by every handle type below -
// mirrors gte::Entity's own `generation == 0` invalidity convention in
// spirit, but as an index sentinel instead, since a render-graph handle's
// generation legitimately starts at 0 for its very first real use (there is
// no equivalent "0 always means never-assigned" generation rule here - see
// each handle's own IsValid() below, which checks `index`, not
// `generation`).
inline constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

// --- Handles ---------------------------------------------------------------
//
// Cheap, POD, generational identifiers - mirrors gte::Entity
// (src/ECS/Entity.h) and gte::GpuResourceHandle
// (src/Renderer/Memory/GpuResourceHandle.h) exactly: same index+generation
// shape, same "always minted by the one system that owns the real thing,
// never invented by calling code" rule (see AGENTS.md's "Entity-Component-
// System"/"GPU Resource Memory Tracking" sections). Three DISTINCT structs
// (never one shared `template<Tag> struct Handle`) so the compiler catches
// "passed a TextureHandle where a BufferHandle was expected" at compile
// time - the same reasoning MeshHandle/PipelineHandle/TextureHandle are
// already three distinct types in src/Renderer/MeshHandle.h/PipelineHandle.h/
// TextureHandle.h, rather than one generic template instantiated three ways
// with implicit convertibility between them.

struct TextureHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const TextureHandle&, const TextureHandle&) noexcept = default;
};

struct BufferHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const BufferHandle&, const BufferHandle&) noexcept = default;
};

struct PassHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const PassHandle&, const PassHandle&) noexcept = default;
};

// --- Resource access kind ----------------------------------------------

// What a pass DOES with a resource it reads/writes - deliberately small,
// deliberately named after WHAT THEY DO, not raw Vulkan enum values
// (mirrors BufferMemoryUsage in Buffer.h, which is GpuOnly/CpuToGpu/
// GpuToCpu, never a raw VkMemoryPropertyFlags). This is Phase 5's raw
// material: every declared read/write in Phase 2's builder API records one
// of these per resource per pass, and Phase 5 turns a (previousAccess,
// nextAccess) pair into a concrete VkImageMemoryBarrier2/
// VkBufferMemoryBarrier2.
//
// Originally scoped to exactly what Phases 1-8's graphics-only MVP needed
// - see RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md's "What We Will NOT Do".
// Extended by Phase 5 of the compute-shader campaign
// (COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md) with three new
// enumerators - ComputeShaderRead/ComputeShaderWrite (a StructuredBuffer/
// RWStructuredBuffer or RWTexture's compute-shader access - see
// COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md) and
// IndirectCommandRead (the companion GPU-driven-rendering document's own
// indirect-draw-buffer read, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT/
// VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT - added here, alongside the other
// two, as the single shared source of truth both documents are meant to
// consume, per COMPUTE_PHASE5_SYNCHRONIZATION_STRATEGY_v2.md's own Step 2:
// "whichever document's implementation lands FIRST is where the enum
// values are actually added"). Adding a new enumerator here MUST be
// accompanied by updating every exhaustive switch that consumes it (see
// IsWriteAccess()/ToString() below, both deliberately written with NO
// `default:` case so a future addition fails to compile here until every
// consumer is updated too) - RenderGraphBarrierPlanner.cpp's
// RequiredStateFor() follows the exact same rule.
enum class ResourceAccess : std::uint8_t {
    ColorAttachmentWrite,
    DepthStencilAttachmentReadWrite,
    ShaderRead, // sampled in a fragment shader (e.g. a material texture / a previous pass's output)
    TransferSrc,
    TransferDst,
    // A StructuredBuffer/RWStructuredBuffer or RWTexture read by a compute
    // shader - VK_DESCRIPTOR_TYPE_STORAGE_BUFFER/STORAGE_IMAGE, never a
    // write hazard source for a later read (see
    // COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md's own
    // RWStructuredBuffer-vs-StructuredBuffer split - both map to this one
    // ResourceAccess value; the read-only/read-write distinction is
    // enforced at the GLSL level - `readonly buffer` vs. plain `buffer` -
    // never here).
    ComputeShaderRead,
    // The write-side counterpart of ComputeShaderRead above - a
    // RWStructuredBuffer/RWTexture written by a compute shader.
    ComputeShaderWrite,
    // A buffer read as the source of vkCmdDrawIndexedIndirect/
    // vkCmdDrawIndirect - the companion GPU-driven-rendering document's own
    // indirect-draw-buffer read. Buffer-only in practice (an indirect
    // command buffer has no texture equivalent) - never asserted against
    // isDepthResource in RequiredStateFor(), since it is meaningful
    // regardless of that flag.
    IndirectCommandRead,
};

// True for any access kind that can WRITE the resource's contents (used by
// Phase 3's dependency-edge computation to decide whether one pass's usage
// must be ordered after another's). A deliberately exhaustive `switch` with
// NO `default:` case - see this file's own header comment and
// RenderGraphTypes.cpp's definition for why an unhandled enumerator must be
// a compile WARNING (promoted to an error by this engine's warning
// settings, matching every other exhaustive-switch precedent in this
// codebase), never a silently-wrong `false`.
bool IsWriteAccess(ResourceAccess access) noexcept;

// Human-readable name for a ResourceAccess value - for Phase 8's debug dump
// and any future assertion message. Every enum in this campaign gets one of
// these from day one, mirroring GpuTimingSample::Status's own eventual
// ToString()-shaped consumers in MemoryPanelData.cpp. Never returns
// nullptr.
const char* ToString(ResourceAccess access) noexcept;

// --- Resource descriptors ------------------------------------------------
//
// The "what do you want," independent of "what you got" - Phase 4's
// resource pool matches "do I already have a physical resource whose desc
// equals this one" purely by VALUE EQUALITY, so both structs below must
// stay plain, comparable, hashable-by-extension value types where EVERY
// field genuinely describes the resource's PHYSICAL SHAPE (what determines
// whether two requests can share one underlying VkImage/VkBuffer), and
// NOTHING ELSE.
//
// Standing rule for anyone adding a field to either struct in a later
// phase: before adding it, ask "does this field change whether two
// requests can share one physical allocation?" If the answer is no (e.g. a
// cosmetic label, an author-supplied hint that doesn't affect the actual
// vmaCreateImage()/vmaCreateBuffer() call), it does NOT belong in this
// struct - thread it as a separate parameter instead (see
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md's CreateTexture(name, desc)
// and RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md's
// AcquireTexture(desc, debugName), both of which already thread a
// resource's human-readable name as a wholly separate parameter for
// exactly this reason). This is not a hypothetical concern - see this
// file's own git history / RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md's
// "V2 Revision Notes": v1 of this exact struct carried a `const char*
// debugName` field with `operator== = default`, which silently compared
// the raw POINTER (not string content), defeating Phase 4's entire
// resource-pooling scheme for any two logically-identical requests issued
// from different call sites. `debugName` is intentionally ABSENT from both
// structs below as the direct fix for that bug - do not reintroduce it.

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // VK_FORMAT_UNDEFINED == "match Renderer::ColorFormat() exactly" - the
    // same convention Renderer::CreateRenderTexture()'s own `format`
    // parameter already uses (see AGENTS.md, "Render Target Format
    // Matching").
    VkFormat format = VK_FORMAT_UNDEFINED;
    // Whether this logical resource also carries a companion DepthBuffer,
    // mirroring RenderTexture's own shape (see src/Renderer/RenderTexture.h).
    bool hasDepth = false;

    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;

    friend bool operator==(const BufferDesc&, const BufferDesc&) noexcept = default;
};

// --- Pass metadata -----------------------------------------------------
//
// The pure record Phase 2's builder API fills in (one read/write
// declaration at a time) and Phase 3's compiler reads (to compute
// dependency order, culling, and resource lifetimes).

// Which of ResourceUsage's two handle fields is actually meaningful for a
// given value - see ResourceUsage below.
enum class ResourceKind : std::uint8_t {
    Texture,
    Buffer,
};

// A single declared read/write on either a texture OR a buffer resource.
// Phase 1 shipped this as a texture-only shape and explicitly flagged (see
// its own completion report's "Handoff notes") that Phase 2 - this phase -
// is where it grows into this real tagged-union shape once an actual
// builder API exists to justify it (RenderGraphBuilder::PassBuilder's
// ReadBuffer()/WriteBuffer(), see RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md,
// Step 3.1's "...ReadBuffer/WriteBuffer, symmetric, for a future compute
// pass").
//
// `kind` says which of `texture`/`buffer` is meaningful - the other field
// is simply left at its own default and never read. Deliberately a plain
// "both fields present, one tag" struct rather than a std::variant,
// matching this codebase's general preference for plain, explicit structs
// (e.g. TextureDesc/BufferDesc are two separate structs, not one variant)
// over template-heavy machinery it doesn't otherwise use. The two static
// factory functions below are what every real call site
// (RenderGraphBuilder::PassBuilder, see Phase 2) actually constructs one
// through - nothing outside this file/its own tests should need to spell
// out all four fields by hand.
struct ResourceUsage {
    ResourceKind kind = ResourceKind::Texture;
    TextureHandle texture;
    BufferHandle buffer;
    ResourceAccess access = ResourceAccess::ShaderRead;

    static ResourceUsage ForTexture(TextureHandle handle, ResourceAccess access) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::Texture;
        usage.texture = handle;
        usage.access = access;
        return usage;
    }

    static ResourceUsage ForBuffer(BufferHandle handle, ResourceAccess access) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::Buffer;
        usage.buffer = handle;
        usage.access = access;
        return usage;
    }
};

// Forward-declared only - fully specified in Phase 6
// (RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md), once Phase 4's
// physical-resource-realization result exists to give it a real shape.
// PassRecord::execute below only needs to know it exists as an opaque type
// its std::function closes over - see
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 3.1.
struct PassContext;

struct PassRecord {
    // Must be a string literal / static-storage-duration const char* -
    // mirrors GTE_PROFILE_SCOPE's own rule (see AGENTS.md, "Profiling":
    // ScopeTimer's `name` parameter) for the exact same reason: this is
    // never owned/copied, just compared/displayed, so a temporary/stack-
    // lifetime string would be a use-after-free risk for zero benefit.
    // Deliberately NOT compared for equality anywhere (unlike TextureDesc/
    // BufferDesc above) - a PassRecord is never "upserted by name"; every
    // AddPass() call (Phase 2) mints a brand new PassRecord every frame, so
    // there is no pooling/identity risk here the way there is for the desc
    // structs above.
    const char* name = nullptr;
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
    // Written by Phase 3's compiler (a pass whose outputs are provably
    // never read, directly or transitively, is culled), read by Phase 6's
    // executor (a culled pass's Execute callback is never invoked).
    bool isCulled = false;

    // Captured by RenderGraphBuilder::AddPass() (Phase 2) - stored, never
    // invoked by AddPass()/Finish() themselves. Invoked exactly once by
    // Phase 6's RenderGraph::Execute(), for every pass that survives Phase
    // 3's culling (isCulled == false) - never for a culled one. A
    // default-constructed PassRecord carries an empty std::function
    // (operator bool() == false); nothing calls it without checking that
    // first.
    std::function<void(PassContext&)> execute;

    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md)
    // addition - optional clear values for this pass's color/depth
    // attachment, set via RenderGraphBuilder::PassBuilder::
    // WriteColorAttachment()/WriteDepthStencilAttachment()'s own optional
    // parameters. std::nullopt (the default) means "preserve existing
    // contents" (RenderGraph::Execute() uses VK_ATTACHMENT_LOAD_OP_LOAD for
    // that attachment, Phase 6's original and only behavior) - when set,
    // that attachment's loadOp becomes VK_ATTACHMENT_LOAD_OP_CLEAR with
    // this exact value. This closes the gap RENDERGRAPH_PHASE6_COMPLETION_REPORT.md
    // flagged explicitly: Phase 6's RenderGraph::Execute() had no way to
    // ask for a clear at all, since Phase 2's builder API had no clear-color
    // concept yet - Phase 7's three real passes (Game/Scene/Present) are
    // what finally need one (see src/Application/RenderPasses.cpp).
    std::optional<std::array<float, 4>> colorClearValue;
    std::optional<float> depthClearValue;
};

} // namespace gte::rg
