#pragma once

// Vulkan-free render-graph vocabulary: the handles and enums plugin callbacks need to name.
// RenderGraphTypes.h and RenderPipeline.h include this instead of redefining them.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace gte::rg {

inline constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

// Cheap, POD, generational identifiers minted by the resource owning each one - never invented by calling code.
// TextureHandle names any image (2D, 3D, array, cube); its shape lives in TextureDesc::dimension.
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

// What a resource is. A ResourceUsage only ever carries Texture or Buffer; the snapshot/UI additionally
// classify a texture by its dimension (VolumeTexture = Tex3D, TextureArray = Array2D/Cube).
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

// Number of ViewScope enumerators. Enumerators are contiguous from 0, so a scope is also its array index.
inline constexpr std::size_t kViewScopeCount = 3;
static_assert(kViewScopeCount == static_cast<std::size_t>(ViewScope::SceneView) + 1,
    "kViewScopeCount must track the ViewScope enumerators");

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

// Opaque view identifier: a hashed name. Shared() is the only built-in value.
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

// A single declared read/write on a texture (any dimension) or a buffer. `kind` says which handle is meaningful.
struct ResourceUsage {
    ResourceKind kind = ResourceKind::Texture;
    TextureHandle texture;
    BufferHandle buffer;
    ResourceAccess access = ResourceAccess::ShaderRead;
    // Texture only - targets the DEPTH half of an imported handle that also carries a color image.
    bool isDepthResource = false;
    // Texture only - per-layer/per-face access to an array or cube. nullopt means the whole image.
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

    static ResourceUsage ForTextureLayer(TextureHandle handle, std::uint32_t layerIndex, ResourceAccess access) noexcept
    {
        ResourceUsage usage = ForTexture(handle, access);
        usage.arrayLayerIndex = layerIndex;
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

// Forward declarations only; the full types live in the Vulkan-bearing RenderGraphBuilder.h / RenderPipeline.h.
class RenderGraphBuilder;
class RenderPassBlackboard;

} // namespace gte::rg
