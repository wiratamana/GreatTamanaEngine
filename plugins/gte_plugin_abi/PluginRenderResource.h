#pragma once

#include <cstdint>

// editor-core-separation-9 campaign, PHASE1
// (PHASE1_RESOURCE_VOCABULARY_AND_ABI_FOUNDATION.md) - the curated,
// ABI-safe resource vocabulary IPluginRenderPassBuilder_v3.h's setup/
// recording surface is built on. Mirrors src/Renderer/RenderGraph/
// RenderGraphTypes.h's own TextureHandle/BufferHandle/ResourceAccess/
// TextureDesc/BufferDesc shape - deliberately a CURATED SUBSET, never a raw
// 1:1 mirror of every internal enum value (see PHASE0_MASTER_STRATEGY.md,
// Step 2.4). Nothing under plugins/gte_plugin_abi/ may #include any real
// gte_core/gte_editor header (PublicSurface.md's ABI-boundary rule) - this
// file includes only <cstdint>.

namespace gte {

// Mirrors rg::TextureHandle/BufferHandle's own "cheap POD index+generation"
// shape (RenderGraphTypes.h) - deliberately TWO distinct structs, never one
// shared template, so "passed a PluginTextureHandle where a
// PluginBufferHandle was expected" is a compile error (Locked Architecture
// Decision, PHASE0_MASTER_STRATEGY.md Step 2.1/Design Doc R1/R4).
//
// IMPORTANT: a PluginTextureHandle/PluginBufferHandle's `index` is NEVER
// numerically identical to (or interchangeable with) a real
// rg::TextureHandle/rg::BufferHandle - PHASE2's adapter owns a small,
// per-(plugin, view, frame)-scoped translation table where this `index` is
// simply that table's own index (PHASE0_MASTER_STRATEGY.md Step 2.5). A
// plugin can therefore never fabricate a handle to an arbitrary host
// resource by guessing an index (Design Doc R5).
inline constexpr std::uint32_t kPluginInvalidResourceIndex = 0xFFFFFFFFu;

struct PluginTextureHandle {
    std::uint32_t index = kPluginInvalidResourceIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kPluginInvalidResourceIndex; }

    friend bool operator==(const PluginTextureHandle&, const PluginTextureHandle&) noexcept = default;
};

struct PluginBufferHandle {
    std::uint32_t index = kPluginInvalidResourceIndex;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != kPluginInvalidResourceIndex; }

    friend bool operator==(const PluginBufferHandle&, const PluginBufferHandle&) noexcept = default;
};

// A curated SUBSET of rg::ResourceAccess (RenderGraphTypes.h) - only the
// values a plugin could ever legitimately need are exposed; anything
// fixed-function-only or internal-engine-only (DepthStencilAttachmentReadWrite/
// TransferSrc/TransferDst/IndirectCommandRead/VertexBufferRead/
// VertexShaderStorageRead) is deliberately NOT exposed here (PublicSurface.md's
// curated-boundary rule - never a 1:1 mirror of every internal enum value).
// Grown additively later ONLY when a real plugin use case needs a new value
// - never speculatively (PHASE0_MASTER_STRATEGY.md Step 3.1).
enum class PluginResourceAccess : std::uint8_t {
    ColorAttachmentWrite,
    ShaderRead,
    ComputeShaderRead,
    ComputeShaderWrite,
};

// Physical-shape-only descriptors - deliberately NO "what is this for"
// field and NO debugName field (a resource's human-readable name is always
// a separate parameter to CreateTexture()/CreateBuffer(), never part of the
// desc itself) - mirrors rg::TextureDesc/rg::BufferDesc's own "standing
// rule" (RenderGraphTypes.h, ~line 259) and its own documented past bug (a
// debugName field with pointer-identity operator== silently broke resource
// pooling) - do not repeat that mistake here.
struct PluginTextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    // A small, closed, curated format enum (NOT a raw VkFormat) - grown
    // additively later only when a real plugin use case needs a new format
    // (PHASE0_MASTER_STRATEGY.md Step 3.1). PluginRenderResourceTranslation.h/.cpp
    // (gte_core-internal) is the ONLY place that maps this enum to a real
    // VkFormat.
    enum class Format : std::uint8_t {
        Rgba8Unorm,
        Rgba16Float,
        R32Float,
    };

    Format format = Format::Rgba8Unorm;
};

struct PluginBufferDesc {
    std::uint64_t sizeBytes = 0;
};

} // namespace gte
