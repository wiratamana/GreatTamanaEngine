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
// Deliberately Vulkan-header-PRESENT-but-Vulkan-call-FREE:
// VkFormat is the only remaining raw Vulkan-header type used
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

#include "RenderGraphHandles.h"
#include "../Buffer.h" // BufferUsage - BufferDesc::usage below, engine-native, never a raw VkBufferUsageFlags.
#include "../../Core/GraphicsHandles.h" // SamplerHandle/DescriptorSetLayoutHandle - PendingBinding/PassRecord below.
#include "../../Core/TextureDimension.h"
#include "../../Core/PixelFormat.h" // TextureDesc/VolumeTextureDesc/TextureArrayDesc::format below - engine-native, never a raw VkFormat.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

// Forward declaration must live in its own plain `namespace gte { ... }`
// block, as a SIBLING of `namespace gte::rg { ... }` below - declaring it
// directly inside the nested gte::rg block would instead declare the
// wrong type, gte::rg::RenderTexture.
namespace gte {
class RenderTexture;
} // namespace gte

namespace gte::rg {

// kInvalidIndex (shared "no slot assigned" sentinel for every handle type,
// including PassHandle below) moved to RenderGraphHandles.h, included above.

// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - matches
// typical real-world VkPhysicalDeviceLimits::maxColorAttachments (locked via
// ask_questions, see task_manager/mrt-1/PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision 2). Enforced by an assert() in
// RenderGraphBuilder::PassBuilder::WriteColorAttachment()
// (RenderGraphBuilder.cpp) - never silently truncated.
inline constexpr std::uint32_t kMaxColorAttachments = 8;

// TextureHandle/BufferHandle/VolumeTextureHandle/TextureArrayHandle moved to
// RenderGraphHandles.h (included above). PassHandle stays here.



struct PassHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const PassHandle&, const PassHandle&) noexcept = default;
};

// --- Resource access kind ----------------------------------------------

// ResourceAccess moved to RenderGraphHandles.h (included above).

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

// Short binding-stage label for a declared resource access - Editor
// Inspector display only, never consulted by RenderGraph.cpp/
// RenderGraphCompiler.cpp. Never returns nullptr.
const char* BindingStageLabel(ResourceAccess access) noexcept;

// True exactly when a resolved view is non-null but its paired sampler is
// null - a plumbing mistake somewhere upstream of a resolve call. Pure and
// Vulkan-call-free so it is directly Tier-1-testable; shared by every
// resolve path (PassContext::resolveReadTexture()/resolveDepthTexture(),
// RenderGraphBuilder::ResolveImportedTextureSamplers()) that needs to guard
// against this exact shape.
bool IsResolvedViewMissingItsSampler(VkImageView view, VkSampler sampler) noexcept;

// --- TextureUsage ---------------------------------------------------------
//
// better-render-pass-1 campaign, PHASE8
// (PHASE8_TEXTUREDESC_USAGE_FIELD_AND_RESOURCE_POOL_AUDIT.md, R5) - a
// VkFlags-shaped bitmask describing what a TextureDesc-declared (transient/
// pooled) texture may be used FOR, mirroring ResourceAccess's own "named
// after WHAT THEY DO" convention immediately above. `Sampled` is the
// real-world default-equivalent meaning every RenderTexture already has
// unconditionally today (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
// VK_IMAGE_USAGE_SAMPLED_BIT, see RenderTexture::Create()) - it carries no
// separate CreateRenderTexture() plumbing of its own, it exists purely so
// `TextureUsage::None` (the universal default) is distinguishable from "I
// explicitly want a plain sampled texture" in caller code, if that
// distinction is ever useful. `Storage` is the one bit genuinely wired all
// the way through today - RenderGraphResourcePool::AcquireTexture() (see
// RenderGraphResourcePool.cpp) translates HasFlag(desc.usage,
// TextureUsage::Storage) into Renderer::CreateRenderTexture()'s own
// `allowStorageImageAccess` parameter, making a genuinely TRANSIENT/pooled
// `RWTexture` possible for the first time (closing
// COMPUTE_SHADER_FEATURES_DELIBERATELY_NOT_IMPLEMENTED.md's Section A.6/C.2
// gap). `TransferSrc`/`TransferDst` are added now purely as vocabulary for a
// FUTURE phase/campaign - confirmed, by direct reading of
// RenderTexture::Create(), that `imageInfo.usage` never includes
// VK_IMAGE_USAGE_TRANSFER_SRC_BIT/_DST_BIT under any condition today, and
// this engine's one real AddBlitPass() consumer (src/Editor/BlitValidation.cpp)
// always blits between directly-owned, imported RenderTexture instances,
// never a pooled/TextureDesc-driven builder.CreateTexture() resource - so
// these two bits have ZERO wired CreateRenderTexture() plumbing behind them
// yet; a future pass needing to blit into a pooled resource would need to
// add that plumbing then, mirroring how Storage/allowStorageImageAccess
// already works.
enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1u << 0,
    Storage = 1u << 1,
    // NOT YET WIRED into RenderTexture::Create()'s own image-usage flags -
    // no CreateRenderTexture() parameter accepts this yet. See this enum's
    // own doc comment above for the full reasoning.
    TransferSrc = 1u << 2,
    // NOT YET WIRED into RenderTexture::Create()'s own image-usage flags -
    // no CreateRenderTexture() parameter accepts this yet. See this enum's
    // own doc comment above for the full reasoning.
    TransferDst = 1u << 3,
};

constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept
{
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr TextureUsage& operator|=(TextureUsage& a, TextureUsage b) noexcept
{
    a = a | b;
    return a;
}

constexpr bool HasFlag(TextureUsage value, TextureUsage flag) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0u;
}

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

// Physical shape of any graph-owned texture: 2D, 3D volume, 2D array or cube.
// New fields are appended at the end so positional initializers like TextureDesc{ w, h, format, hasDepth, usage } stay valid.
struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // PixelFormat::Undefined == "match the renderer's color format (or depth format, for a depth array)".
    PixelFormat format = PixelFormat::Undefined;
    // Tex2D: also carries a companion depth image (like RenderTexture).
    // Array2D/Cube: the single image IS a depth image (true) or a color image (false) - never both.
    bool hasDepth = false;
    TextureUsage usage = TextureUsage::None; // Storage and non-storage images are not interchangeable.
    TextureDimension dimension = TextureDimension::Tex2D;
    std::uint32_t depth = 1;       // Z slices (Tex3D only).
    std::uint32_t arrayLayers = 1; // Layer count (Array2D/Cube; cube needs a positive multiple of 6 and width == height).

    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};

struct BufferDesc {
    uint64_t size = 0;
    // Engine-native BufferUsage bitmask (Buffer.h) - never a raw
    // VkBufferUsageFlags, mirroring TextureDesc::usage's own TextureUsage
    // convention immediately above.
    BufferUsage usage = BufferUsage::None;

    friend bool operator==(const BufferDesc&, const BufferDesc&) noexcept = default;
};

// --- Pass metadata -----------------------------------------------------
//
// The pure record Phase 2's builder API fills in (one read/write
// declaration at a time) and Phase 3's compiler reads (to compute
// dependency order, culling, and resource lifetimes).

// ResourceKind moved to RenderGraphHandles.h (included above).

// ViewScope moved to RenderGraphHandles.h (included above).

// PassKind moved to RenderGraphHandles.h (included above).

const char* ToString(PassKind kind) noexcept;

// RenderPassCategory moved to RenderGraphHandles.h (included above).

const char* ToString(RenderPassCategory category) noexcept;

// RenderPassDrawKind moved to RenderGraphHandles.h (included above).

const char* ToString(RenderPassDrawKind drawKind) noexcept;

// RenderPassEvent moved to RenderGraphHandles.h (included above). Still a
// real, load-bearing sort/ordering key for RenderGraphCompiler::Compile().

const char* ToString(RenderPassEvent renderPassEvent) noexcept;

// ResourceUsage moved to RenderGraphHandles.h (included above).

// Single dispatch point over a usage's resource kind. Exhaustive switch with no `default:`, so a new
// ResourceKind fails to compile here. All callables must return the same type (void is fine).
// A texture-like kind (Texture/VolumeTexture/TextureArray) routes to `onTexture`.
template <typename TextureFn, typename BufferFn>
auto DispatchByKind(const ResourceUsage& usage, TextureFn&& onTexture, BufferFn&& onBuffer)
{
    switch (usage.kind) {
    case ResourceKind::Texture:
    case ResourceKind::VolumeTexture:
    case ResourceKind::TextureArray:
        return onTexture(usage.texture);
    case ResourceKind::Buffer:
        return onBuffer(usage.buffer);
    }
    // Only reachable with an out-of-range kind. Fail loudly rather than re-run a callable's side effects.
    assert(false && "DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's named enumerators");
    throw std::logic_error("DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's named enumerators");
}

// Forward-declared only - fully specified in Phase 6
// (RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md), once Phase 4's
// physical-resource-realization result exists to give it a real shape.
// PassRecord::execute below only needs to know it exists as an opaque type
// its std::function closes over - see
// RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md, Step 3.1.
struct PassContext;

// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - one
// ORDERED color-attachment slot on a pass. Attachment INDEX in
// PassRecord::colorAttachments (below) is the CONTRACT: it equals the
// shader's own `layout(location = N) out` index for that pass's fragment
// shader. This is deliberately NOT inferred by scanning `writes` (a pass's
// `writes` vector may, in the future, also contain non-attachment texture
// writes once mixed with WriteTexture()-style compute writes on the same
// pass - order there is declaration order for ALL kinds combined, not
// guaranteed to match render-target order) - `colorAttachments` is the one
// and only source of truth for "which handle is attachment slot N", kept
// in lockstep with a mirrored ColorAttachmentWrite entry in `writes` (see
// PassBuilder::WriteColorAttachment() below) purely so barrier planning/
// dependency-edge computation - which only ever reads `writes`, never this
// new field - keeps working with NO changes at all.
struct ColorAttachmentDesc {
    TextureHandle handle;
    std::optional<std::array<float, 4>> clearColor;
};

// The TextureArray-layer counterpart of ColorAttachmentDesc above - the
// authoritative declaration of "this pass's one attachment is layer N of
// this array resource", populated by WriteArrayLayer() in lockstep with a
// matching ResourceUsage pushed onto pass.writes. Which aspect (color vs.
// depth) this attachment targets is not stored here - it is a property of
// the underlying TextureArrayDesc::hasDepth, read off the resolved
// physical resource by the executor.
struct ArrayLayerAttachmentDesc {
    TextureHandle handle;
    std::uint32_t layerIndex = 0;
    std::optional<std::array<float, 4>> clearColor; // meaningful only when the array is color (hasDepth == false)
    std::optional<float> clearDepth;                 // meaningful only when the array is depth (hasDepth == true)
};

// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE2 - PURE,
// Tier-1-testable (plain VkExtent2D comparison only - no VkDevice/VkImage
// involved) - given the resolved extents of a pass's declared color
// attachments, in pass.colorAttachments order, returns the 0-based index of
// the FIRST entry whose extent differs from entry 0, or std::nullopt if all
// N extents match (vacuously true for N <= 1 - the single-attachment/
// no-attachment case every existing pass in the engine hits today).
// Extracted as its own function, rather than left inline inside
// RenderGraph::ExecuteCompiledGraph(), specifically so a real Tier-1 test
// can verify this exact decision with plain data and no live VkDevice -
// mirroring TargetsDepthState()/IsColorAttachmentWriteAccess()'s own
// precedent (RenderGraphBarrierPlanner.h) for the identical reason. Lives
// here (rather than RenderGraphBarrierPlanner.h) since it operates on this
// campaign's own ColorAttachmentDesc/colorAttachments shape, not on
// barrier-transition ResourceState.
std::optional<std::size_t> FindMismatchedColorAttachmentExtent(const std::vector<VkExtent2D>& extents) noexcept;

// editor-core-separation-26 campaign, PHASE3
// (PHASE3_BLITSPEC_PASSKIND_AND_PURE_HELPERS.md) - a raw image copy/blit is
// NOT a generic "arbitrary setup+execute" pass; it has one fixed shape,
// captured here as a small, typed, POD-shaped spec struct rather than an
// open-ended callback pair. See `RenderGraphBuilder::AddBlitPass()`
// (PHASE5) for the real, producible entry point that constructs one of
// these and stores it on `PassRecord::blitCommand` (below).
struct BlitSpec {
    TextureHandle src;
    TextureHandle dst;
    VkFilter filter = VK_FILTER_LINEAR;
    bool srcIsDepth = false;
    bool dstIsDepth = false;
    // Defaults to the FULL resolved extent of each handle when left at
    // {0,0,0}/{0,0,0} — an explicit sub-region is opt-in, not required.
    // A region is malformed (and must be rejected, debug-asserted against,
    // at minimum, per this section's own "region validation" bullet below)
    // if either axis's max is not strictly greater than its min, UNLESS
    // both min and max are simultaneously {0,0,0} (the "use full extent"
    // sentinel) - that exact all-zero shape is the only degenerate value
    // this struct ever treats as meaningful.
    VkOffset3D srcRegionMin{};
    VkOffset3D srcRegionMax{};
    VkOffset3D dstRegionMin{};
    VkOffset3D dstRegionMax{};
};

// Resolves BlitSpec::filter into the EFFECTIVE filter a real
// vkCmdBlitImage call must use: verbatim when neither srcIsDepth nor
// dstIsDepth is set, forced to VK_FILTER_NEAREST otherwise (Vulkan
// disallows linear filtering against a depth/stencil format
// unconditionally - there is no valid caller intent to trust either
// build configuration with here, unlike ResolveBlitRegion()/
// IsValidBlitRegion() below). The ONE official place BlitSpec::filter
// is ever turned into a real Vulkan filter value - see RenderGraph.cpp's
// own execution branch, which calls this rather than reading
// spec.filter directly.
VkFilter ResolveEffectiveBlitFilter(const BlitSpec& spec) noexcept;

// One resolved [min, max) 3D region, in the SAME coordinate space
// vkCmdBlitImage's own VkOffset3D srcOffsets[2]/dstOffsets[2] expect.
struct ResolvedBlitRegion {
    VkOffset3D min{};
    VkOffset3D max{};
};

// Resolves ONE side (src or dst) of a BlitSpec's region fields against
// that side's own physically resolved 2D extent: the all-zero sentinel
// (regionMin AND regionMax both {0,0,0}) resolves to the FULL extent
// ({0,0,0} to {extent.width, extent.height, 1}); any other value is
// passed through UNCHANGED (validation is IsValidBlitRegion()'s job
// below, not this function's - this function only ever expands the
// sentinel, it never rejects/clamps anything).
ResolvedBlitRegion ResolveBlitRegion(
    VkOffset3D regionMin, VkOffset3D regionMax, VkExtent2D resolvedExtent) noexcept;

// Debug-assert-level validation (source document's own "REGION
// VALIDATION" bullet, Part B.2) against an ALREADY-RESOLVED region
// (i.e. called AFTER ResolveBlitRegion() above, never against raw,
// possibly-sentinel input) - true only if max is STRICTLY greater than
// min on every one of the 3 axes, AND the whole region's min/max both
// fall within [0, resolvedExtent] on x/y and within [0, 1] on z (a 2D
// texture's own resolved extent has no meaningful 3rd dimension beyond
// exactly 1). A release build is permitted to trust the caller and
// never call this at all (this engine's general "asserts are the
// safety net, not a runtime check" convention) - see RenderGraph.cpp's
// own execution branch for where this is actually asserted.
bool IsValidBlitRegion(const ResolvedBlitRegion& region, VkExtent2D resolvedExtent) noexcept;

// Compute-binding vocabulary - declares which descriptor slot a resource occupies. A Bind*() call never
// implies a barrier; the matching ReadTexture()/WriteTexture() call still owns that.
enum class BindingKind : std::uint8_t {
    StorageBuffer,
    CombinedImageSampler,
    StorageImage,
    // Combined-image-sampler against an already-resolved ImageViewHandle/SamplerHandle pair that is not a graph resource.
    CombinedImageViewSampler,
};

// One descriptor-slot binding declared via PassBuilder::Bind*(); resolved into a descriptor write right before the pass runs.
struct PendingBinding {
    std::uint32_t binding = 0;
    BindingKind kind = BindingKind::StorageBuffer;
    TextureHandle textureHandle;
    // CombinedImageSampler only: Depth binds the texture's depth image view + depth sampler.
    TextureAspect textureAspect = TextureAspect::Color;
    BufferHandle bufferHandle;
    BufferObjectHandle bufferObjectHandle = BufferObjectHandle::Invalid;
    // CombinedImageViewSampler only.
    ImageViewHandle explicitImageView = ImageViewHandle::Invalid;
    // Invalid on a CombinedImageSampler binding means "use the texture's own sampler" (volumes have none: pass one).
    SamplerHandle explicitSampler = SamplerHandle::Invalid;
    uint64_t bufferOffset = 0;
    uint64_t bufferRange = VK_WHOLE_SIZE;
};

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

    // Render Pass campaign (task_manager/render-pass-1), PHASE1 - REPLACES
    // the old plain `bool isComputePass` (frame-debugger-5 campaign,
    // PHASE1) with a real `PassKind` enum (see this file's own comment on
    // `PassKind` above). `PassKind::Compute` for every pass declared via
    // RenderGraphBuilder::AddComputePass() (see that method's own updated
    // doc comment, RenderGraphBuilder.h); `PassKind::Graphics` (the
    // default) for a pass declared via plain AddPass() - i.e. every real
    // graphics/draw pass in this engine today (e.g. "GameView"). This is
    // PURELY DESCRIPTIVE metadata: nothing in RenderGraph.cpp/
    // RenderGraphCompiler.cpp reads this field at all - it exists solely so
    // a downstream CONSUMER (RenderGraphSnapshot.h's own
    // RenderGraphPassSnapshot::kind, read by the Editor's Frame Debugger -
    // see FrameDebuggerData.cpp) can generically discover "which passes
    // that ran this frame were compute dispatches" without needing to
    // already know every compute pass's exact string name in advance.
    PassKind kind = PassKind::Graphics;

    // frame-debugger-6 campaign, PHASE1
    // (PHASE1_RENDERGRAPH_VIEWSCOPE_CHOKEPOINT_INFRASTRUCTURE.md) - see
    // ViewScope's own doc comment above. Defaults to Shared so every
    // pre-existing AddPass()/AddComputePass() call site (which never
    // mentions ViewScope at all) keeps its exact prior behavior/meaning
    // unchanged.
    ViewScope viewScope = ViewScope::Shared;

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

    // category/drawKind/owningFeatureName are deliberately NOT stored on
    // this hot, always-live struct - each is PURELY DESCRIPTIVE metadata
    // read only by the Editor's Frame Debugger, so each lives on a
    // separate, Core-owned, zero-cost-when-absent hook instead - see
    // RenderGraphDebugMetadataSink.h's own PassDebugMetadata/
    // IPassDebugMetadataSink/IPassDebugMetadataProvider. PassRecord::kind/
    // ::viewScope (above) are real fields on this struct regardless.

    // render-pass-3 campaign, PHASE1 - a SORT HINT (see RenderPassEvent's
    // own doc comment above - as of render-pass-4 PHASE2, a REAL,
    // load-bearing one, no longer "purely descriptive") - read by
    // RenderGraphCompiler::Compile() as of render-pass-4 PHASE2 (unlike
    // every other purely-descriptive PassRecord field before it, which
    // genuinely still are read by NOTHING in RenderGraph.cpp/
    // RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp). Defaults to
    // Opaques so every pre-existing AddRenderPass() call site (which never
    // mentions this at all) is completely unaffected - the default is
    // irrelevant for any pass the Frame Debugger already excludes via
    // category == Debug (PHASE4 never reads this field for those).
    // Appended at the END of the struct (never inserted in the middle) -
    // see this file's own header comment.
    //
    // render-pass-4 campaign, PHASE1 - this field is cross-checked against
    // every pass's real, declared resource dependencies by
    // RenderGraphCompiler::Compile() - see RenderPassEvent's own doc
    // comment above for the full write-up.
    //
    // render-pass-4 campaign, PHASE2 - and, as of this phase, this field is
    // also the sort key Compile() stable-sorts every pass by (ties broken by
    // original declaration order) before its RAW/WAW edge scan and Kahn's
    // tie-break - see RenderPassEvent's own doc comment above for the full
    // write-up.
    RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;

    // Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE1 - the
    // ordered list of this pass's color attachments (index == shader
    // `layout(location = N) out`) - see ColorAttachmentDesc's own doc
    // comment above. Populated exclusively by
    // RenderGraphBuilder::PassBuilder::WriteColorAttachment() (one push_back
    // per call, in call order) - NEVER read by RenderGraphCompiler.cpp
    // (culling/lifetime/dependency-edge computation still only ever reads
    // `writes`, completely unchanged by this campaign) - read ONLY by
    // RenderGraph::ExecuteCompiledGraph() (PHASE2 of this campaign) to build
    // the real vkCmdBeginRendering attachment array, in this exact order.
    // Size is bounded by kMaxColorAttachments (asserted in
    // WriteColorAttachment(), never here).
    std::vector<ColorAttachmentDesc> colorAttachments;

    // editor-core-separation-26 campaign, PHASE3
    // (PHASE3_BLITSPEC_PASSKIND_AND_PURE_HELPERS.md) - only meaningful when
    // `kind == PassKind::Blit` - see `RenderGraphBuilder::AddBlitPass()`
    // (PHASE5). `std::nullopt` for every other pass. Appended at the END of
    // the struct (never inserted in the middle), per this file's own header
    // comment.
    std::optional<BlitSpec> blitCommand;

    // See ArrayLayerAttachmentDesc above. std::nullopt for every pass that
    // never calls WriteArrayLayer(). At most one per pass -
    // WriteArrayLayer() asserts against a second call on the same pass.
    std::optional<ArrayLayerAttachmentDesc> arrayLayerAttachment;

    // Compute-binding vocabulary - see PendingBinding's own doc comment
    // above. Populated exclusively by RenderGraphBuilder::PassBuilder's
    // BindDescriptorSetLayout()/Bind*() calls. Empty `bindings` (every
    // pre-existing pass today) costs nothing beyond one empty-vector check
    // in RenderGraph::ExecuteCompiledGraph().
    DescriptorSetLayoutHandle computeBindingLayout = DescriptorSetLayoutHandle::Invalid;
    std::vector<PendingBinding> bindings;
};

} // namespace gte::rg
