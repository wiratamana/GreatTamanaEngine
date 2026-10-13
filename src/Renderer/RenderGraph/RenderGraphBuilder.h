#pragma once

// Declarative builder API to record frame render passes and resources.

#include "RenderGraphTypes.h"
#include "RenderGraphDebugMetadataSink.h"
#include "RenderGraphPersistentResourceCache.h"
#include "RenderPassToggleRegistry.h"
#include "../RenderTarget.h"
#include "../VolumeTarget.h"
#include "../TextureArrayTarget.h"
#include "../../Core/ImageLayout.h"

#include <volk.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte::rg {

// Forward declaration to avoid circular include with RenderGraph.h.
enum class ExecuteTimingMode : std::uint8_t;

// Import metadata for textures owned outside the render graph.
struct TextureImportInfo {
    bool isImported = false;
    RenderTarget externalTarget{};
    ImageLayout currentLayout = ImageLayout::Undefined;
    // Engine-native opaque handles (Core/GraphicsHandles.h) - never a raw
    // VkSampler. See RenderTexture::SamplerObjectHandle()/
    // DepthSamplerObjectHandle() for the usual way a caller builds one of
    // these; Renderer/GraphicsHandleRegistry.h's Native() is how this
    // engine ever unwraps one back to a real VkSampler (see RenderGraph.cpp's
    // EnsureTextureResolved()).
    SamplerHandle colorSampler = SamplerHandle::Invalid;
    SamplerHandle depthSampler = SamplerHandle::Invalid;
    RenderTexture* trackedOwner = nullptr; // Optional owner to receive final layout state.
};

// Import metadata for buffers owned outside the render graph.
struct BufferImportInfo {
    bool isImported = false;
    // Engine-native opaque handle (Core/GraphicsHandles.h) - never a raw
    // VkBuffer. See Buffer::ObjectHandle() for the usual way a caller builds
    // one of these; Renderer/GraphicsHandleRegistry.h's Native() is how this
    // engine ever unwraps one back to a real VkBuffer (see RenderGraph.cpp's
    // EnsureBufferResolved()).
    BufferObjectHandle externalBuffer = BufferObjectHandle::Invalid;
    VkDeviceSize size = 0;
};

// Import metadata for 3D volume textures owned outside the render graph.
struct VolumeTextureImportInfo {
    bool isImported = false;
    VolumeTarget externalTarget{};
    ImageLayout currentLayout = ImageLayout::Undefined;
};

// Import metadata for texture arrays owned outside the render graph.
struct TextureArrayImportInfo {
    bool isImported = false;
    TextureArrayTarget externalTarget{};
    ImageLayout currentLayout = ImageLayout::Undefined;
};

// Resource slot entries storing descriptor, debug name, and import data together.
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

// Uncompiled frame description passed to the compiler.
struct CompiledGraphInput {
    std::vector<PassRecord> passes;

    std::vector<TextureSlot> textures;
    std::vector<BufferSlot> buffers;
    std::vector<VolumeTextureSlot> volumeTextures;
    std::vector<TextureArraySlot> textureArrays;

    // Output roots that must not be culled during compilation.
    std::vector<VolumeTextureHandle> finalVolumeTextureOutputs;
    std::vector<BufferHandle> finalBufferOutputs;
    std::vector<TextureArrayHandle> finalTextureArrayOutputs;
    std::vector<TextureHandle> persistentCacheTextures;
};

// Records passes and resources for a single frame.
class RenderGraphBuilder {
public:
    // Helper to declare reads, writes, and bindings during pass setup.
    class PassBuilder {
    public:
        explicit PassBuilder(PassRecord& pass) noexcept
            : m_pass(pass)
        {
        }

        void ReadTexture(TextureHandle handle, ResourceAccess access = ResourceAccess::ShaderRead,
            bool isDepthResource = false);

        // Adds a color attachment write. Can be called multiple times for MRT (up to 8).
        void WriteColorAttachment(TextureHandle handle, const std::optional<std::array<float, 4>>& clearColor = std::nullopt);

        // Sets the depth/stencil attachment write for this pass.
        void WriteDepthStencilAttachment(TextureHandle handle, std::optional<float> clearDepth = std::nullopt);

        // General texture write (e.g. compute storage image or transfer dst).
        void WriteTexture(TextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite,
            bool isDepthResource = false);

        void ReadBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteBuffer(BufferHandle handle, ResourceAccess access = ResourceAccess::TransferDst);

        void ReadVolumeTexture(VolumeTextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderRead);
        void WriteVolumeTexture(VolumeTextureHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite);

        void ReadTextureArray(TextureArrayHandle handle, ResourceAccess access = ResourceAccess::ShaderRead);
        void WriteTextureArray(TextureArrayHandle handle, ResourceAccess access = ResourceAccess::ComputeShaderWrite);

        // Declares an attachment write to a single layer of a texture array.
        void WriteArrayLayer(TextureArrayHandle handle, std::uint32_t layerIndex, ResourceAccess access,
            std::optional<std::array<float, 4>> clearColor = std::nullopt, std::optional<float> clearDepth = std::nullopt);

        // --- Compute Bindings ---

        // Must be called before binding descriptor slots for this pass.
        void BindDescriptorSetLayout(DescriptorSetLayoutHandle layout);

        void BindStorageBuffer(std::uint32_t binding, BufferHandle handle, VkDeviceSize offset = 0,
            VkDeviceSize range = VK_WHOLE_SIZE);

        // Binds an untracked, externally managed buffer.
        void BindStorageBuffer(std::uint32_t binding, BufferObjectHandle handle, VkDeviceSize offset = 0,
            VkDeviceSize range = VK_WHOLE_SIZE);

        // If sampler is Invalid, uses the texture's own sampler.
        void BindCombinedImageSampler(
            std::uint32_t binding, TextureHandle handle, SamplerHandle sampler = SamplerHandle::Invalid);

        // Sampler is required for volume textures.
        void BindCombinedImageSampler(std::uint32_t binding, VolumeTextureHandle handle, SamplerHandle sampler);

        // Binds an untracked external image view with a sampler.
        void BindCombinedImageSampler(std::uint32_t binding, ImageViewHandle view, SamplerHandle sampler);

        void BindStorageImage(std::uint32_t binding, TextureHandle handle);
        void BindStorageImage(std::uint32_t binding, VolumeTextureHandle handle);

    private:
        PassRecord& m_pass;
    };

    // Creates a transient resource managed by the graph resource pool.
    TextureHandle CreateTexture(const char* name, const TextureDesc& desc);
    BufferHandle CreateBuffer(const char* name, const BufferDesc& desc);
    TextureArrayHandle CreateTextureArray(const char* name, const TextureArrayDesc& desc);

    // Imports an externally managed resource into the graph. colorSampler/
    // depthSampler are engine-native SamplerHandle opaque handles (Core/
    // GraphicsHandles.h), never a raw VkSampler - see
    // RenderTexture::SamplerObjectHandle()/DepthSamplerObjectHandle().
    TextureHandle ImportTexture(const char* name, const RenderTarget& externalTarget, ImageLayout currentLayout,
        SamplerHandle colorSampler = SamplerHandle::Invalid, SamplerHandle depthSampler = SamplerHandle::Invalid,
        RenderTexture* trackedOwner = nullptr);

    // externalBuffer is an engine-native BufferObjectHandle opaque handle
    // (Core/GraphicsHandles.h), never a raw VkBuffer - see
    // Buffer::ObjectHandle().
    BufferHandle ImportBuffer(const char* name, BufferObjectHandle externalBuffer, VkDeviceSize size);

    VolumeTextureHandle ImportVolumeTexture(
        const char* name, const VolumeTarget& externalVolumeTarget, ImageLayout currentLayout);

    TextureArrayHandle ImportTextureArray(
        const char* name, const TextureArrayTarget& externalTarget, ImageLayout currentLayout);

    // Marks resources as graph roots to prevent them from being culled.
    void KeepTextureOutput(TextureHandle handle);
    const std::vector<TextureHandle>& FinalTextureOutputs() const noexcept { return m_finalTextureOutputs; }

    void KeepVolumeTextureOutput(VolumeTextureHandle handle);
    void KeepBufferOutput(BufferHandle handle);
    void KeepTextureArrayOutput(TextureArrayHandle handle);

    // Declares a standard render pass with immediate setup and deferred execution.
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

    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().viewScope = viewScope;
    }

    // Declares a compute pass.
    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().kind = PassKind::Compute;
    }

    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddComputePass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        m_passes.back().viewScope = viewScope;
    }

    // Main entry point for declaring passes with debugging and toggle support.
    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, ViewScope viewScope, RenderPassCategory category,
        SetupFn&& setup, ExecuteFn&& execute, RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques)
    {
        if (kind == PassKind::Compute) {
            AddComputePass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        } else {
            AddPass(name, viewScope, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
        }
        m_passes.back().renderPassEvent = renderPassEvent;

        const std::string_view owner = CurrentOwningFeatureName();
        if (m_debugMetadataSink != nullptr) {
            m_debugMetadataSink->OnPassDeclared(m_passes.size() - 1, category, drawKind, owner);
        }
        if (m_passToggleRegistry != nullptr && name != nullptr && name[0] != '\0') {
            m_passToggleRegistry->NoteDeclaredWithOwner(name, owner);
        }
    }

    template <typename SetupFn, typename ExecuteFn>
    void AddRenderPass(const char* name, PassKind kind, SetupFn&& setup, ExecuteFn&& execute,
        RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques)
    {
        AddRenderPass(name, kind, ViewScope::Shared, RenderPassCategory::General,
            std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute), drawKind, renderPassEvent);
    }

    // Declares an image copy/blit pass.
    void AddBlitPass(const char* name, const BlitSpec& spec,
        RenderPassEvent renderPassEvent = RenderPassEvent::Opaques,
        ViewScope viewScope = ViewScope::Shared,
        RenderPassCategory category = RenderPassCategory::General);

    void SetDebugMetadataSink(IPassDebugMetadataSink* sink) noexcept { m_debugMetadataSink = sink; }

    void SetPersistentResourceCache(
        RenderGraphPersistentResourceCache* cache, ExecuteTimingMode timingMode, std::uint64_t currentFrame) noexcept
    {
        m_persistentCache = cache;
        m_persistentCacheTimingMode = timingMode;
        m_persistentCacheCurrentFrame = currentFrame;
    }

    // Retrieves or allocates a persistent cross-frame texture.
    TextureHandle GetOrCreatePersistentTexture(const char* owner, const char* name, const TextureDesc& desc);

    // Fast-path lookup for persistent textures using a cached token.
    TextureHandle GetOrCreatePersistentTexture(
        PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc);

    // Pass inspection queries.
    std::size_t DeclaredPassCount() const noexcept { return m_passes.size(); }

    RenderPassEvent PassEventAt(std::size_t index) const
    {
        assert(index < m_passes.size() && "RenderGraphBuilder::PassEventAt() - index out of range");
        return m_passes[index].renderPassEvent;
    }

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

    bool PassReadsTextureAsDepth(std::size_t index, TextureHandle handle) const
    {
        assert(index < m_passes.size() && "RenderGraphBuilder::PassReadsTextureAsDepth() - index out of range");
        for (const ResourceUsage& usage : m_passes[index].reads) {
            if (usage.kind == ResourceKind::Texture && usage.texture == handle && usage.isDepthResource) {
                return true;
            }
        }
        return false;
    }

    struct ImportedTextureSamplers {
        VkSampler colorSampler = VK_NULL_HANDLE;
        VkImageView depthImageView = VK_NULL_HANDLE;
        VkSampler depthSampler = VK_NULL_HANDLE;
    };

    // Resolves samplers for an imported texture at declaration time.
    ImportedTextureSamplers ResolveImportedTextureSamplers(TextureHandle handle) const noexcept;

    // Finalizes the builder and returns the compiled graph input.
    CompiledGraphInput Finish();

    // Feature scope tracking for pass categorization and debugging.
    void PushOwningFeatureScope(std::string_view name) { m_owningFeatureScope.push_back(std::string(name)); }
    void PopOwningFeatureScope() noexcept { m_owningFeatureScope.pop_back(); }

    std::string_view CurrentOwningFeatureName() const noexcept
    {
        static constexpr std::string_view kEngineUnowned = "ENGINE_UNOWNED";
        return m_owningFeatureScope.empty() ? kEngineUnowned : std::string_view(m_owningFeatureScope.back());
    }

    void SetPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept { m_passToggleRegistry = registry; }

private:
    TextureHandle MintPersistentHandle(const RenderGraphPersistentResourceCache::ResolvedTexture& resolved);

    std::vector<PassRecord> m_passes;
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    std::vector<std::string> m_owningFeatureScope;
    RenderPassToggleRegistry* m_passToggleRegistry = nullptr;

    std::vector<TextureSlot> m_textures;
    std::vector<BufferSlot> m_buffers;
    std::vector<VolumeTextureSlot> m_volumeTextures;
    std::vector<TextureArraySlot> m_textureArrays;

    std::vector<TextureHandle> m_finalTextureOutputs;
    std::vector<VolumeTextureHandle> m_finalVolumeTextureOutputs;
    std::vector<BufferHandle> m_finalBufferOutputs;
    std::vector<TextureArrayHandle> m_finalTextureArrayOutputs;
    std::vector<TextureHandle> m_persistentCacheTextures;

    RenderGraphPersistentResourceCache* m_persistentCache = nullptr;
    ExecuteTimingMode m_persistentCacheTimingMode{};
    std::uint64_t m_persistentCacheCurrentFrame = 0;
};

} // namespace gte::rg
