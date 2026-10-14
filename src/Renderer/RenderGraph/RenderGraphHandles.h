#pragma once

// Vulkan-free render-graph vocabulary: every handle/enum a Project Assembly
// plugin callback needs to NAME, without pulling in Vulkan. RenderGraphTypes.h
// and RenderPipeline.h include this instead of redefining these types.

#include <cstdint>
#include <optional>

namespace gte::rg {

inline constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

// Cheap, POD, generational identifiers minted by the resource owning each
// one - never invented by calling code. Three distinct structs (not one
// shared template) so the compiler catches a mismatched handle at compile time.

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

// Handle to a real 3D (volume) GPU image.
struct VolumeTextureHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const VolumeTextureHandle&, const VolumeTextureHandle&) noexcept = default;
};

// Handle to a multi-layer image (texture array or cubemap).
struct TextureArrayHandle {
    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kInvalidIndex; }

    friend bool operator==(const TextureArrayHandle&, const TextureArrayHandle&) noexcept = default;
};

// Which half of an imported texture a binding targets (an imported view can carry a color and a depth image).
enum class TextureAspect : std::uint8_t {
    Color,
    Depth,
};

// What a pass does with a resource it reads/writes - named after the
// operation, never a raw Vulkan access/stage flag.
enum class ResourceAccess : std::uint8_t {
    ColorAttachmentWrite,
    DepthStencilAttachmentReadWrite,
    ShaderRead,
    TransferSrc,
    TransferDst,
    ComputeShaderRead,
    ComputeShaderWrite,
    IndirectCommandRead,
    // A vertex buffer read by the fixed-function vertex-input assembler -
    // distinct from ShaderRead/ComputeShaderRead (no descriptor set involved).
    VertexBufferRead,
    // A StructuredBuffer read by the vertex SHADER stage itself.
    VertexShaderStorageRead,
};

// Which of ResourceUsage's handle fields is meaningful for a given value.
enum class ResourceKind : std::uint8_t {
    Texture,
    Buffer,
    VolumeTexture,
    TextureArray,
};

// Which real view (if any) a pass belongs to. Shared means "not duplicated
// per view" - stamped once by whichever call site already knows the answer.
enum class ViewScope : std::uint8_t {
    Shared,
    GameView,
    SceneView,
};

// Graphics (dynamic-rendering draw bracket), Compute (vkCmdDispatch), or
// Blit (raw image copy).
enum class PassKind : std::uint8_t {
    Graphics,
    Compute,
    Blit,
};

// Frame Debugger tree-grouping hint only - orthogonal to PassKind/ViewScope.
enum class RenderPassCategory : std::uint8_t {
    General,
    Debug,
    FrameDebuggerInternal,
};

// Frame Debugger child-event label hint for a Graphics-kind pass.
enum class RenderPassDrawKind : std::uint8_t {
    DrawMesh,
    DrawQuad,
    Blit,
};

// Real ordering/sort key RenderGraphCompiler::Compile() stable-sorts passes
// by before computing RAW/WAW dependency edges.
enum class RenderPassEvent : std::uint32_t {
    BeforeEverything = 0,
    PreOpaques = 1000,
    Opaques = 2000,
    AfterOpaques = 2500,
    Transparents = 3000,
    AfterTransparents = 4000,
    AfterEverything = 9000,
};

// Opaque view identifier - Shared() is the only built-in value; Named() lets
// any future named view exist for free (identity is a hashed opaque name,
// never a fixed enum).
class RenderViewId {
public:
    RenderViewId() noexcept = default; // Defaults to the same reserved hash Shared() returns.

    static RenderViewId Shared() noexcept { return RenderViewId{}; }

    // Hashed, opaque - FNV-1a over the name's bytes.
    static RenderViewId Named(const char* name) noexcept
    {
        std::uint64_t hash = 2166136261u;
        if (name != nullptr) {
            for (const char* p = name; *p != '\0'; ++p) {
                hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(*p));
                hash *= 16777619u;
            }
        }
        RenderViewId view;
        // Reserve 0 exclusively for Shared() - nudge a real collision to 1.
        view.m_hash = (hash != 0) ? hash : 1;
        return view;
    }

    bool operator==(const RenderViewId&) const noexcept = default;

    // Needed by SceneServiceBlackboardKey() to fold a view's identity into a
    // per-(slot, view) blackboard key.
    std::uint64_t Hash() const noexcept { return m_hash; }

private:
    std::uint64_t m_hash = 0;
};

// A single declared read/write on a texture, buffer, volume texture, or
// texture array - `kind` says which field is meaningful, every other field
// stays at its default and is never read.
struct ResourceUsage {
    ResourceKind kind = ResourceKind::Texture;
    TextureHandle texture;
    BufferHandle buffer;
    VolumeTextureHandle volumeTexture;
    TextureArrayHandle textureArray;
    ResourceAccess access = ResourceAccess::ShaderRead;
    // Only meaningful when kind == Texture - targets the DEPTH half of an
    // already-imported handle that also carries a color image.
    bool isDepthResource = false;
    // Per-layer/per-face write against a TextureArrayHandle. nullopt means
    // whole-array usage. Only meaningful when kind == TextureArray.
    std::optional<std::uint32_t> arrayLayerIndex;

    static ResourceUsage ForTexture(TextureHandle handle, ResourceAccess access, bool isDepthResource = false) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::Texture;
        usage.texture = handle;
        usage.access = access;
        usage.isDepthResource = isDepthResource;
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

    static ResourceUsage ForVolumeTexture(VolumeTextureHandle handle, ResourceAccess access) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::VolumeTexture;
        usage.volumeTexture = handle;
        usage.access = access;
        return usage;
    }

    static ResourceUsage ForTextureArray(TextureArrayHandle handle, ResourceAccess access) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::TextureArray;
        usage.textureArray = handle;
        usage.access = access;
        return usage;
    }

    static ResourceUsage ForTextureArrayLayer(
        TextureArrayHandle handle, std::uint32_t layerIndex, ResourceAccess access) noexcept
    {
        ResourceUsage usage;
        usage.kind = ResourceKind::TextureArray;
        usage.textureArray = handle;
        usage.access = access;
        usage.arrayLayerIndex = layerIndex;
        return usage;
    }
};

// Forward-declared only - a plugin callback type alias only needs these two
// names to exist, never their full definition (owned by RenderGraphBuilder.h/
// RenderPipeline.h, both Vulkan-bearing).
class RenderGraphBuilder;
class RenderPassBlackboard;

} // namespace gte::rg
