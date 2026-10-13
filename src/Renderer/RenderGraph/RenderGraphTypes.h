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
// VkDeviceSize are the only remaining raw Vulkan-header types used
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

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // PixelFormat::Undefined == "match Renderer::ColorPixelFormat() exactly"
    // - the same convention Renderer::CreateRenderTexture()'s own `format`
    // parameter already uses (see AGENTS.md, "Render Target Format
    // Matching"). Engine-native PixelFormat, never a raw VkFormat - Renderer
    // is the ONE place this ever translates to/from a real VkFormat (see
    // VulkanFormatInfo.h).
    PixelFormat format = PixelFormat::Undefined;
    // Whether this logical resource also carries a companion DepthBuffer,
    // mirroring RenderTexture's own shape (see src/Renderer/RenderTexture.h).
    bool hasDepth = false;
    // better-render-pass-1 campaign, PHASE8
    // (PHASE8_TEXTUREDESC_USAGE_FIELD_AND_RESOURCE_POOL_AUDIT.md, R5) -
    // appended at the END per this struct's own universal field-append
    // rule, genuinely belongs here per the "standing rule" comment above:
    // a storage-capable image and a non-storage image are NOT
    // Vulkan-interchangeable, so `usage` genuinely changes whether two
    // requests can share one physical allocation. Compared automatically by
    // the `= default` operator== below, exactly like `hasDepth` already is.
    TextureUsage usage = TextureUsage::None;

    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};

struct BufferDesc {
    VkDeviceSize size = 0;
    // Engine-native BufferUsage bitmask (Buffer.h) - never a raw
    // VkBufferUsageFlags, mirroring TextureDesc::usage's own TextureUsage
    // convention immediately above.
    BufferUsage usage = BufferUsage::None;

    friend bool operator==(const BufferDesc&, const BufferDesc&) noexcept = default;
};

// Atmosphere Scattering campaign, Phase 2
// (ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md) - the
// VolumeTextureHandle counterpart of TextureDesc/BufferDesc above, same
// "purely a physical-shape descriptor, no debugName field" rule (see this
// file's own "standing rule" comment above). `depth` is the one genuinely
// new dimension neither TextureDesc nor BufferDesc has any equivalent of.
struct VolumeTextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;
    PixelFormat format = PixelFormat::Undefined;

    friend bool operator==(const VolumeTextureDesc&, const VolumeTextureDesc&) noexcept = default;
};

// A single Vulkan image with N layers (a Texture2DArray) or a cubemap
// (VK_IMAGE_VIEW_TYPE_CUBE/_CUBE_ARRAY). Poolable and barrier-planned
// exactly like plain TextureDesc/TextureHandle - unlike VolumeTextureDesc,
// which is import-only with no pooling counterpart.
// See src/Renderer/TextureArray2D.h for the owning RAII class.
struct TextureArrayDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t arrayLayers = 1;   // Layer count for a multi-layer poolable image, e.g. 4.
    PixelFormat format = PixelFormat::Undefined;
    // DELIBERATE ASYMMETRY vs. TextureDesc::hasDepth (which defaults false):
    // this defaults to TRUE because a multi-layer depth image array is
    // this struct's dominant real use case. hasDepth here does NOT mean "also allocate a
    // second companion depth image" (TextureDesc's own meaning) - there is
    // only ever ONE image. It selects WHICH KIND of homogeneous image every
    // layer is:
    //   true  (default): every layer is a depth image (VK_IMAGE_ASPECT_DEPTH_BIT,
    //          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | SAMPLED_BIT).
    //          PixelFormat::Undefined resolves to Renderer::DepthPixelFormat()
    //          (NOT ColorPixelFormat() - these are two different,
    //          independently negotiated device formats). usage.Storage must
    //          never combine with hasDepth == true.
    //   false: every layer is a color image (VK_IMAGE_ASPECT_COLOR_BIT,
    //          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | SAMPLED_BIT, plus
    //          STORAGE_BIT when usage has TextureUsage::Storage).
    //          PixelFormat::Undefined resolves to Renderer::ColorPixelFormat(),
    //          exactly like plain TextureDesc.
    // A caller building a color array/cubemap (e.g. a reflection-probe
    // array) MUST pass hasDepth = false explicitly - never rely on the
    // default for that case. Do not "fix" this asymmetry by changing the
    // default.
    bool hasDepth = true;
    // VK_IMAGE_VIEW_TYPE_CUBE/_CUBE_ARRAY + VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT.
    // Requires arrayLayers a positive multiple of 6, AND width == height
    // (Vulkan requires square faces for a cube-compatible image) - enforced
    // unconditionally (even in release) by GpuResourceFactory::CreateTextureArray(),
    // NEVER by a debug-assert alone - see TextureArray2D.h.
    bool isCubemap = false;
    TextureUsage usage = TextureUsage::None;

    friend bool operator==(const TextureArrayDesc&, const TextureArrayDesc&) noexcept = default;
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

// render-pass-6 campaign, PHASE6 (item 2.2) - REPLACES every independently
// hand-rolled `switch (usage.kind) { Texture / Buffer / VolumeTexture }`
// block scattered across RenderGraph.cpp/RenderGraphCompiler.cpp/
// RenderGraphSnapshot.cpp with ONE generic dispatch point every one of them
// routes through instead. `usage.kind` still drives a REAL, exhaustive
// switch internally - deliberately NOT a `default:`-having fallback of any
// kind, so a future 4th ResourceKind enumerator STILL fails to compile here
// until every call site's own three (soon four) lambdas are updated - this
// is the exact same "no default: case, ever" guarantee IsWriteAccess()/
// ToString() already provide for ResourceAccess, extended to ResourceKind's
// own dispatch shape.
//
// `onTexture`/`onBuffer`/`onVolumeTexture` are three separate callables
// (never one generic templated callable dispatched via `if constexpr`) -
// this is deliberate: three DISTINCT lambda parameters, one per kind, means
// a caller who forgets to handle one kind gets a real compile error
// (missing function argument) rather than a silently-empty generic body -
// mirroring this file's own general "plain, explicit code over template-
// heavy machinery" house style (see ResourceUsage's own doc comment on why
// it is a tagged struct, not a std::variant).
//
// All three callables must return the SAME type (C++ requires every return
// statement inside a function with a deduced `auto` return type to agree) -
// `void` is completely valid (RenderGraph.cpp's ApplyUsageBarrierIfNeeded()
// uses this: all the divergent per-kind logic lives inside the three
// lambdas, the dispatcher itself returns nothing), as is a shared pointer
// type (e.g. `ResourceLifetime*`, letting one lambda per kind resolve BOTH
// "which vector" and "which index" in one step - see RenderGraphCompiler.cpp's
// lifetime `touch()` lambda) or a shared plain value type (`std::int32_t`,
// `bool`, `std::string`, ...). This is a per-call-site choice, re-checked
// independently at each of this template's own instantiations - it is never
// a global constraint on DispatchByKind() itself.
template <typename TextureFn, typename BufferFn, typename VolumeTextureFn, typename TextureArrayFn>
auto DispatchByKind(const ResourceUsage& usage, TextureFn&& onTexture, BufferFn&& onBuffer,
    VolumeTextureFn&& onVolumeTexture, TextureArrayFn&& onTextureArray)
{
    switch (usage.kind) {
    case ResourceKind::Texture:
        return onTexture(usage.texture);
    case ResourceKind::Buffer:
        return onBuffer(usage.buffer);
    case ResourceKind::VolumeTexture:
        return onVolumeTexture(usage.volumeTexture);
    case ResourceKind::TextureArray:
        return onTextureArray(usage.textureArray);
    }
    // Unreachable in practice - the switch above is exhaustive over all 4
    // current ResourceKind enumerators with no default: case, so this line
    // is only ever reached if a caller somehow constructed an out-of-range
    // ResourceKind value directly (never possible through any real
    // ResourceUsage::ForTexture()/ForBuffer()/ForVolumeTexture()/
    // ForTextureArray() factory).
    // Kept ONLY for the compiler's own "not all control paths return a
    // value" diagnostic on some compilers/warning levels (MSVC's C4715 in
    // particular, which can fire even for a switch that visibly covers
    // every named enumerator, since it cannot statically rule out a value
    // outside the enum's named range at runtime).
    //
    // Deliberately DOES NOT call onTexture/onBuffer/onVolumeTexture/
    // onTextureArray again as a fallback (unlike naively mirroring
    // IsWriteAccess()/ToString()'s own "return a safe sentinel" convention)
    // - those two functions return a concrete, known, always-safe sentinel
    // type (bool/const char*); this template's return type is caller-chosen
    // and may be a pointer, a pair, or anything else with no safe default -
    // and, more importantly, re-invoking one of the four callables here
    // would silently RE-RUN real, possibly side-effecting per-kind logic
    // (e.g. a barrier site emitting a second GPU barrier) for a usage.kind
    // value that, if this line is ever genuinely reached, is already
    // corrupt - doing nothing and failing loudly is strictly safer than
    // doing something wrong. A thrown exception is valid for ANY deduced
    // return type (including void), since a path that always throws never
    // needs to produce a value.
    assert(false && "DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's 4 named enumerators");
    throw std::logic_error(
        "DispatchByKind: ResourceUsage::kind held a value outside ResourceKind's 4 named enumerators");
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
    TextureArrayHandle handle;
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

// Compute-binding vocabulary - lets a pass declare which descriptor slot a
// resource occupies, orthogonal to the Read*/Write* barrier-tracking family
// above (a Bind* call never implies a barrier - the matching ReadTexture()/
// WriteTexture()/etc. call still owns that). `kind` selects which of the
// handle fields below is meaningful; for StorageBuffer/StorageImage exactly
// one of the two possible handle fields is ever set by the one PassBuilder
// call that produced this entry (see RenderGraphBuilder.h's BindStorageBuffer/
// BindStorageImage overloads) - checked by which one IsValid()/!=Invalid at
// resolve time, never both.
enum class BindingKind : std::uint8_t {
    StorageBuffer,
    CombinedImageSampler,
    CombinedVolumeImageSampler,
    StorageImage,
    // A combined-image-sampler binding against an already-resolved,
    // NOT-graph-tracked ImageViewHandle/SamplerHandle pair - the sibling of
    // BufferObjectHandle for images (e.g. a per-view depth target handed
    // down through ScenePassReadHandles, never itself a graph resource).
    CombinedImageViewSampler,
};

// One descriptor-slot binding declared via PassBuilder::Bind*() - resolved
// into a real VkDescriptorSet write immediately before this pass's execute()
// runs (see RenderGraph.cpp).
struct PendingBinding {
    std::uint32_t binding = 0;
    BindingKind kind = BindingKind::StorageBuffer;
    TextureHandle textureHandle;
    VolumeTextureHandle volumeTextureHandle;
    BufferHandle bufferHandle;
    BufferObjectHandle bufferObjectHandle = BufferObjectHandle::Invalid;
    // CombinedImageViewSampler only - an already-resolved, NOT-graph-tracked
    // image view (e.g. a per-view depth target's own ImageViewHandle).
    ImageViewHandle explicitImageView = ImageViewHandle::Invalid;
    // CombinedImageSampler/CombinedImageViewSampler: SamplerHandle::Invalid
    // on a CombinedImageSampler binding means "use this TextureHandle's own
    // already-resolved sampler" - REQUIRED (never Invalid) for
    // CombinedVolumeImageSampler/CombinedImageViewSampler, which track no
    // implicit sampler of their own.
    SamplerHandle explicitSampler = SamplerHandle::Invalid;
    VkDeviceSize bufferOffset = 0;
    VkDeviceSize bufferRange = VK_WHOLE_SIZE;
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
