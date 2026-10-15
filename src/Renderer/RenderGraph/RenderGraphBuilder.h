#pragma once

// Declarative builder API to record frame render passes and resources.

#include "GpuBuffer.h"
#include "OnceErrorLog.h"
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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {
class Renderer;
}

namespace gte::rg {

// Forward declaration to avoid circular include with RenderGraph.h.
enum class ExecuteTimingMode : std::uint8_t;

// Import metadata for textures owned outside the graph; the TextureDesc::dimension picks which
// external target (2D, volume or array) is meaningful.
struct TextureImportInfo {
    bool isImported = false;
    RenderTarget externalTarget{};
    VolumeTarget externalVolumeTarget{};
    TextureArrayTarget externalArrayTarget{};
    ImageLayout currentLayout = ImageLayout::Undefined;
    // Opaque sampler handles (never a raw VkSampler), usually from RenderTexture::SamplerObjectHandle()/DepthSamplerObjectHandle().
    SamplerHandle colorSampler = SamplerHandle::Invalid;
    SamplerHandle depthSampler = SamplerHandle::Invalid;
    RenderTexture* trackedOwner = nullptr; // Optional owner to receive final layout state.
};

// Import metadata for buffers owned outside the render graph.
struct BufferImportInfo {
    bool isImported = false;
    // Opaque handle (never a raw VkBuffer) - see Buffer::ObjectHandle().
    BufferObjectHandle externalBuffer = BufferObjectHandle::Invalid;
    uint64_t size = 0;
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

// A persistent-cache texture used this frame, with the slot its final layout is recorded against.
struct PersistentTextureUse {
    TextureHandle handle;
    PersistentSlotRef slot;
};

// Uncompiled frame description passed to the compiler.
struct CompiledGraphInput {
    std::vector<PassRecord> passes;

    std::vector<TextureSlot> textures;
    std::vector<BufferSlot> buffers;

    // Output roots that must not be culled during compilation.
    std::vector<TextureHandle> finalTextureOutputs;
    std::vector<BufferHandle> finalBufferOutputs;
    std::vector<PersistentTextureUse> persistentCacheTextures; // Also roots.
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

        // Declares an attachment write to a single layer of an array or cube.
        void WriteArrayLayer(TextureHandle handle, std::uint32_t layerIndex, ResourceAccess access,
            std::optional<std::array<float, 4>> clearColor = std::nullopt, std::optional<float> clearDepth = std::nullopt);

        // --- Compute Bindings ---

        // Must be called before binding descriptor slots for this pass.
        void BindDescriptorSetLayout(DescriptorSetLayoutHandle layout);

        void BindStorageBuffer(std::uint32_t binding, BufferHandle handle, uint64_t offset = 0,
            uint64_t range = VK_WHOLE_SIZE);

        // Binds an untracked, externally managed buffer.
        void BindStorageBuffer(std::uint32_t binding, BufferObjectHandle handle, uint64_t offset = 0,
            uint64_t range = VK_WHOLE_SIZE);

        // Binds an untracked, externally managed uniform buffer.
        void BindUniformBuffer(std::uint32_t binding, BufferObjectHandle handle, uint64_t offset = 0,
            uint64_t range = VK_WHOLE_SIZE);

        // If sampler is Invalid, uses the texture's own sampler (a volume has none: pass one).
        void BindCombinedImageSampler(
            std::uint32_t binding, TextureHandle handle, SamplerHandle sampler = SamplerHandle::Invalid);

        // Binds one aspect (color or depth) of an imported texture with that aspect's own sampler.
        void BindCombinedImageSampler(std::uint32_t binding, TextureHandle handle, TextureAspect aspect);

        // Binds an untracked external image view with a sampler.
        void BindCombinedImageSampler(std::uint32_t binding, ImageViewHandle view, SamplerHandle sampler);

        void BindStorageImage(std::uint32_t binding, TextureHandle handle);

        // --- Fused declarations: access (barrier) and binding in one call ---

        // Samples a color texture in a shader (read + combined image sampler).
        void SampleTexture(std::uint32_t binding, TextureHandle handle, SamplerHandle sampler = SamplerHandle::Invalid);

        // Samples the depth aspect of a texture (read + depth combined image sampler).
        void SampleDepth(std::uint32_t binding, TextureHandle handle);

        // Writes a texture through a storage image (compute write + storage image binding).
        void WriteStorageImage(std::uint32_t binding, TextureHandle handle);

        // Externally owned buffer: binding only, the graph does not track it.
        void ReadStorageBuffer(std::uint32_t binding, BufferObjectHandle handle);

    private:
        PassRecord& m_pass;
    };

    // Creates a transient resource managed by the graph resource pool (any dimension the pool supports:
    // Tex2D, Array2D, Cube - see TextureDesc).
    TextureHandle CreateTexture(const char* name, const TextureDesc& desc);
    BufferHandle CreateBuffer(const char* name, const BufferDesc& desc);

    // Imports an externally managed resource into the graph; samplers are engine handles, never raw VkSampler.
    TextureHandle ImportTexture(const char* name, const RenderTarget& externalTarget, ImageLayout currentLayout,
        SamplerHandle colorSampler = SamplerHandle::Invalid, SamplerHandle depthSampler = SamplerHandle::Invalid,
        RenderTexture* trackedOwner = nullptr);

    // Imports a GpuBuffer. `graphName` (static storage) defaults to the buffer's own name.
    BufferHandle Import(const GpuBuffer& buffer, const char* graphName = nullptr);

    // Low-level form of Import(): an engine handle (Core/GraphicsHandles.h) plus its size.
    BufferHandle ImportBuffer(const char* name, BufferObjectHandle externalBuffer, uint64_t size);

    // A volume has no sampler of its own: pass one to bind it by name as a sampled texture.
    TextureHandle ImportVolumeTexture(const char* name, const VolumeTarget& externalVolumeTarget,
        ImageLayout currentLayout, SamplerHandle sampler = SamplerHandle::Invalid);

    TextureHandle ImportTextureArray(
        const char* name, const TextureArrayTarget& externalTarget, ImageLayout currentLayout);

    // Marks resources as graph roots to prevent them from being culled (any texture dimension).
    void KeepTextureOutput(TextureHandle handle);
    const std::vector<TextureHandle>& FinalTextureOutputs() const noexcept { return m_finalTextureOutputs; }

    void KeepBufferOutput(BufferHandle handle);

    // Declares a standard render pass with immediate setup and deferred execution.
    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPassImpl(name, ViewScope::Shared, PassKind::Graphics, std::forward<SetupFn>(setup),
            std::forward<ExecuteFn>(execute));
    }

    template <typename SetupFn, typename ExecuteFn>
    void AddPass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPassImpl(name, viewScope, PassKind::Graphics, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
    }

    // Declares a compute pass.
    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPassImpl(name, ViewScope::Shared, PassKind::Compute, std::forward<SetupFn>(setup),
            std::forward<ExecuteFn>(execute));
    }

    template <typename SetupFn, typename ExecuteFn>
    void AddComputePass(const char* name, ViewScope viewScope, SetupFn&& setup, ExecuteFn&& execute)
    {
        AddPassImpl(name, viewScope, PassKind::Compute, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
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

    // True when frames in flight overlap, so CPU-written buffers and resizes are unsafe.
    bool IsPersistentRegimePipelined() const noexcept;

    // Real-frame counter of this graph execution (0 on a bare builder).
    std::uint64_t FrameIndex() const noexcept { return m_persistentCacheCurrentFrame; }

    // Only code that calls OwningRenderer() needs a renderer; a bare builder has none.
    void SetRenderer(Renderer* renderer) noexcept { m_renderer = renderer; }
    Renderer* RendererOrNull() const noexcept { return m_renderer; }
    Renderer& OwningRenderer() const
    {
        assert(m_renderer != nullptr && "RenderGraphBuilder has no renderer installed");
        return *m_renderer;
    }

    // Physical desc (extent, format, depth) of a declared texture; imported ones report the real image.
    std::optional<TextureDesc> DescOf(TextureHandle handle) const;

    // Retrieves or allocates a persistent cross-frame texture. The token is the fast path across frames.
    TextureHandle GetOrCreatePersistentTexture(
        PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc);

    // Same, owned by the open RenderFeatureScope. Refused (invalid handle) when no scope is open.
    TextureHandle GetOrCreatePersistentTexture(
        PersistentTextureCacheToken& token, const char* name, const TextureDesc& desc);

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

    // True iff the pass declared WriteDepthStencilAttachment() on `handle`. A depth test reads the attachment,
    // so this counts as a real dependency without a fake ShaderRead (which would demand a conflicting layout).
    bool PassWritesTextureAsDepthReadWrite(std::size_t index, TextureHandle handle) const
    {
        assert(index < m_passes.size() && "RenderGraphBuilder::PassWritesTextureAsDepthReadWrite() - index out of range");
        for (const ResourceUsage& usage : m_passes[index].writes) {
            if (usage.kind == ResourceKind::Texture && usage.texture == handle
                && usage.access == ResourceAccess::DepthStencilAttachmentReadWrite) {
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
    bool HasOwningFeatureScope() const noexcept { return !m_owningFeatureScope.empty(); }

    std::string_view CurrentOwningFeatureName() const noexcept
    {
        static constexpr std::string_view kEngineUnowned = "ENGINE_UNOWNED";
        return m_owningFeatureScope.empty() ? kEngineUnowned : std::string_view(m_owningFeatureScope.back());
    }

    void SetPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept { m_passToggleRegistry = registry; }

    // Installs the graph's once-log, so an error repeated every frame is logged once for the graph's lifetime.
    void SetOnceLog(OnceErrorLog* log) noexcept { m_onceLog = log; }
    void ReportOnce(std::string_view category, std::string_view text)
    {
        (m_onceLog != nullptr ? *m_onceLog : m_ownOnceErrorLog).Error(category, text);
    }

private:
    TextureHandle MintPersistentHandle(const RenderGraphPersistentResourceCache::ResolvedTexture& resolved);

    // Declares the pass, runs setup, then claims its (name, ViewScope) binding identity.
    template <typename SetupFn, typename ExecuteFn>
    void AddPassImpl(const char* name, ViewScope viewScope, PassKind kind, SetupFn&& setup, ExecuteFn&& execute)
    {
        assert(name != nullptr && name[0] != '\0' &&
            "RenderGraphBuilder::AddPass requires a non-empty, static-storage-duration pass name");

        m_passes.push_back(PassRecord{});
        PassRecord& pass = m_passes.back();
        pass.name = name;
        pass.viewScope = viewScope;
        pass.kind = kind;

        PassBuilder passBuilder(pass);
        setup(passBuilder);
        ClaimBindingIdentity(m_passes.size() - 1);

        pass.execute = std::function<void(PassContext&)>(std::forward<ExecuteFn>(execute));
    }

    // Two passes with the same (name, ViewScope) and a binding layout would share one descriptor set.
    // The later one loses its bindings and is marked skipExecute.
    void ClaimBindingIdentity(std::size_t passIndex);

    std::vector<PassRecord> m_passes;
    std::vector<std::size_t> m_bindingPassIndices; // Passes that own a binding identity this frame.
    IPassDebugMetadataSink* m_debugMetadataSink = nullptr;
    std::vector<std::string> m_owningFeatureScope;
    RenderPassToggleRegistry* m_passToggleRegistry = nullptr;
    OnceErrorLog* m_onceLog = nullptr;
    OnceErrorLog m_ownOnceErrorLog; // Used when no graph installed one (a bare builder).

    std::vector<TextureSlot> m_textures;
    std::vector<BufferSlot> m_buffers;

    std::vector<TextureHandle> m_finalTextureOutputs;
    std::vector<BufferHandle> m_finalBufferOutputs;
    std::vector<PersistentTextureUse> m_persistentCacheTextures;

    RenderGraphPersistentResourceCache* m_persistentCache = nullptr;
    ExecuteTimingMode m_persistentCacheTimingMode{};
    std::uint64_t m_persistentCacheCurrentFrame = 0;
    Renderer* m_renderer = nullptr;
};

} // namespace gte::rg
