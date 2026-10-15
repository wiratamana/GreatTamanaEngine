#pragma once

// Render graph vocabulary: handles, enums and plain descriptor structs (Vulkan headers only, no device calls).
// Lives in gte::rg to stay distinct from gte::Mesh/Buffer/Texture2D.

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

// Must be a sibling of gte::rg, not nested in it: otherwise it declares gte::rg::RenderTexture.
namespace gte {
class RenderTexture;
} // namespace gte

namespace gte::rg {

// Upper bound on color attachments per pass (matches typical maxColorAttachments); asserted in WriteColorAttachment().
inline constexpr std::uint32_t kMaxColorAttachments = 8;

struct PassHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const PassHandle&, const PassHandle&) noexcept = default;
};

// --- Resource access kind ----------------------------------------------

// True for any access kind that can WRITE the resource. Exhaustive switch without `default:`,
// so a new enumerator fails to compile instead of silently returning false.
bool IsWriteAccess(ResourceAccess access) noexcept;

// Human-readable name of an access kind, for debug output. Never nullptr.
const char* ToString(ResourceAccess access) noexcept;

// Short binding-stage label for an access (Editor Inspector display only). Never nullptr.
const char* BindingStageLabel(ResourceAccess access) noexcept;

// True when a resolved view is non-null but its paired sampler is null (an upstream plumbing mistake).
bool IsResolvedViewMissingItsSampler(VkImageView view, VkSampler sampler) noexcept;

// What a graph-owned 2D texture may be used for. Sampled is the implicit default; Storage and the transfer
// flags add image usage bits at creation (a copy, clear or blit needs them).
enum class TextureUsage : std::uint32_t {
    None = 0,
    Sampled = 1u << 0,
    Storage = 1u << 1,
    TransferSrc = 1u << 2,
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

// --- Resource descriptors (matched by value: only PHYSICAL shape fields, never labels or pointers) ---

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
    // Format of the depth companion. Set only on imported textures; Undefined means "the renderer's depth format".
    PixelFormat depthFormat = PixelFormat::Undefined;

    friend bool operator==(const TextureDesc&, const TextureDesc&) noexcept = default;
};

struct BufferDesc {
    uint64_t size = 0;
    // Engine-native BufferUsage bitmask, never a raw VkBufferUsageFlags.
    BufferUsage usage = BufferUsage::None;

    friend bool operator==(const BufferDesc&, const BufferDesc&) noexcept = default;
};

// --- Pass metadata -----------------------------------------------------

const char* ToString(PassKind kind) noexcept;
const char* ToString(RenderPassCategory category) noexcept;
const char* ToString(RenderPassDrawKind drawKind) noexcept;
const char* ToString(RenderPassEvent renderPassEvent) noexcept;

// Dispatches on a usage's resource kind (texture-like kinds go to `onTexture`). No `default:`, so a new
// ResourceKind fails to compile here. All callables must return the same type.
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

// Opaque execution context handed to PassRecord::execute.
struct PassContext;

// One ORDERED color-attachment slot. Its index in PassRecord::colorAttachments is the shader's
// `layout(location = N) out` index; `writes` carries a mirrored entry for barrier planning.
struct ColorAttachmentDesc {
    TextureHandle handle;
    std::optional<std::array<float, 4>> clearColor;
};

// The array-layer counterpart of ColorAttachmentDesc: "this pass's one attachment is layer N of this array".
// Color vs. depth comes from the array's own TextureArrayDesc::hasDepth.
struct ArrayLayerAttachmentDesc {
    TextureHandle handle;
    std::uint32_t layerIndex = 0;
    std::optional<std::array<float, 4>> clearColor; // meaningful only when the array is color (hasDepth == false)
    std::optional<float> clearDepth;                 // meaningful only when the array is depth (hasDepth == true)
};

// Index of the first color attachment whose extent differs from entry 0, or nullopt if all match
// (always nullopt for 0 or 1 attachments).
std::optional<std::size_t> FindMismatchedColorAttachmentExtent(const std::vector<VkExtent2D>& extents) noexcept;

// Sampling filter of a blit. Engine vocabulary: no Vulkan type in the pass API.
enum class BlitFilter : std::uint8_t { Linear, Nearest };

// Fixed-shape spec of a raw image blit pass (see RenderGraphBuilder::AddBlitPass()).
struct BlitSpec {
    TextureHandle src;
    TextureHandle dst;
    BlitFilter filter = BlitFilter::Linear;
    bool srcIsDepth = false;
    bool dstIsDepth = false;
    // Regions default to the full extent when min and max are both {0,0,0}.
    // Otherwise max must be strictly greater than min on every axis.
    VkOffset3D srcRegionMin{};
    VkOffset3D srcRegionMax{};
    VkOffset3D dstRegionMin{};
    VkOffset3D dstRegionMax{};
};

// Effective blit filter: spec.filter, forced to Nearest when either side is depth (linear is invalid there).
BlitFilter ResolveEffectiveBlitFilter(const BlitSpec& spec) noexcept;

// The Vulkan filter for a blit filter.
constexpr VkFilter ToVkFilter(BlitFilter filter) noexcept
{
    return filter == BlitFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

// One resolved [min, max) 3D region, in the SAME coordinate space
// vkCmdBlitImage's own VkOffset3D srcOffsets[2]/dstOffsets[2] expect.
struct ResolvedBlitRegion {
    VkOffset3D min{};
    VkOffset3D max{};
};

// Expands the all-zero sentinel to the full extent ({0,0,0}..{w,h,1}); any other region passes through unchanged.
ResolvedBlitRegion ResolveBlitRegion(
    VkOffset3D regionMin, VkOffset3D regionMax, VkExtent2D resolvedExtent) noexcept;

// Validates a RESOLVED region: max > min on all 3 axes, inside [0, extent] on x/y and [0, 1] on z.
// Debug-assert use only.
bool IsValidBlitRegion(const ResolvedBlitRegion& region, VkExtent2D resolvedExtent) noexcept;

// Compute-binding vocabulary - declares which descriptor slot a resource occupies. A Bind*() call never
// implies a barrier; the matching ReadTexture()/WriteTexture() call still owns that.
enum class BindingKind : std::uint8_t {
    StorageBuffer,
    UniformBuffer,
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
    // Must be a string literal (static storage): never owned or copied.
    // Never compared for equality; a PassRecord is rebuilt every frame.
    const char* name = nullptr;
    std::vector<ResourceUsage> reads;
    std::vector<ResourceUsage> writes;
    // Set by the compiler when no output is ever read; the executor skips culled passes.
    bool isCulled = false;

    // Descriptive only (Frame Debugger): Compute for AddComputePass(), Graphics otherwise.
    PassKind kind = PassKind::Graphics;

    // Which view this pass belongs to; Shared by default.
    ViewScope viewScope = ViewScope::Shared;

    // Pass body, invoked once per frame for every non-culled pass. May be empty.
    std::function<void(PassContext&)> execute;

    // Optional clear values. nullopt keeps the contents (LOAD); a value becomes LOAD_OP_CLEAR.
    std::optional<std::array<float, 4>> colorClearValue;
    std::optional<float> depthClearValue;

    // category/drawKind/owningFeatureName live on the debug-metadata sink (RenderGraphDebugMetadataSink.h),
    // not on this hot struct.

    // Sort key: Compile() stable-sorts passes by event (ties keep declaration order) and cross-checks it
    // against the declared dependencies.
    RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;

    // Ordered color attachments (index == shader `layout(location = N) out`); only the executor reads this.
    // Size is bounded by kMaxColorAttachments.
    std::vector<ColorAttachmentDesc> colorAttachments;

    // Set only when kind == PassKind::Blit; nullopt for every other pass.
    std::optional<BlitSpec> blitCommand;

    // At most one per pass (WriteArrayLayer() asserts on a second call); nullopt otherwise.
    std::optional<ArrayLayerAttachmentDesc> arrayLayerAttachment;

    // Bind*() state, filled by RenderGraphBuilder::PassBuilder. Empty `bindings` means no descriptor set.
    DescriptorSetLayoutHandle computeBindingLayout = DescriptorSetLayoutHandle::Invalid;
    std::vector<PendingBinding> bindings;

    // Set by the builder when the pass was refused (duplicate binding identity): barriers stay, execute is skipped.
    bool skipExecute = false;
};

} // namespace gte::rg
