#include "GraphicsHandleRegistry.h"

#include "Vulkan/OpaqueHandleRegistry.h"

namespace gte {

namespace {

// One process-wide registry per native type: the engine owns one device, and CommandBuffer has no Renderer
// to route through, so Native()/Register*() stay free functions.
OpaqueHandleRegistry<VkDescriptorSet>& DescriptorSetRegistry()
{
    static OpaqueHandleRegistry<VkDescriptorSet> registry;
    return registry;
}
OpaqueHandleRegistry<VkSampler>& SamplerRegistry()
{
    static OpaqueHandleRegistry<VkSampler> registry;
    return registry;
}
OpaqueHandleRegistry<VkImageView>& ImageViewRegistry()
{
    static OpaqueHandleRegistry<VkImageView> registry;
    return registry;
}
OpaqueHandleRegistry<VkDescriptorSetLayout>& DescriptorSetLayoutRegistry()
{
    static OpaqueHandleRegistry<VkDescriptorSetLayout> registry;
    return registry;
}
OpaqueHandleRegistry<VkBuffer>& BufferObjectRegistry()
{
    static OpaqueHandleRegistry<VkBuffer> registry;
    return registry;
}

} // namespace

DescriptorSetHandle RegisterDescriptorSet(VkDescriptorSet value) noexcept
{
    return static_cast<DescriptorSetHandle>(DescriptorSetRegistry().Insert(value));
}
VkDescriptorSet Native(DescriptorSetHandle handle) noexcept
{
    return DescriptorSetRegistry().Get(static_cast<std::uint64_t>(handle));
}
void UnregisterDescriptorSet(DescriptorSetHandle handle) noexcept
{
    DescriptorSetRegistry().Remove(static_cast<std::uint64_t>(handle));
}

SamplerHandle RegisterSampler(VkSampler value) noexcept
{
    return static_cast<SamplerHandle>(SamplerRegistry().Insert(value));
}
VkSampler Native(SamplerHandle handle) noexcept
{
    return SamplerRegistry().Get(static_cast<std::uint64_t>(handle));
}
void UnregisterSampler(SamplerHandle handle) noexcept
{
    SamplerRegistry().Remove(static_cast<std::uint64_t>(handle));
}

ImageViewHandle RegisterImageView(VkImageView value) noexcept
{
    return static_cast<ImageViewHandle>(ImageViewRegistry().Insert(value));
}
VkImageView Native(ImageViewHandle handle) noexcept
{
    return ImageViewRegistry().Get(static_cast<std::uint64_t>(handle));
}
void UnregisterImageView(ImageViewHandle handle) noexcept
{
    ImageViewRegistry().Remove(static_cast<std::uint64_t>(handle));
}

DescriptorSetLayoutHandle RegisterDescriptorSetLayout(VkDescriptorSetLayout value) noexcept
{
    return static_cast<DescriptorSetLayoutHandle>(DescriptorSetLayoutRegistry().Insert(value));
}
VkDescriptorSetLayout Native(DescriptorSetLayoutHandle handle) noexcept
{
    return DescriptorSetLayoutRegistry().Get(static_cast<std::uint64_t>(handle));
}
void UnregisterDescriptorSetLayout(DescriptorSetLayoutHandle handle) noexcept
{
    DescriptorSetLayoutRegistry().Remove(static_cast<std::uint64_t>(handle));
}

BufferObjectHandle RegisterBufferObject(VkBuffer value) noexcept
{
    return static_cast<BufferObjectHandle>(BufferObjectRegistry().Insert(value));
}
VkBuffer Native(BufferObjectHandle handle) noexcept
{
    return BufferObjectRegistry().Get(static_cast<std::uint64_t>(handle));
}
void UnregisterBufferObject(BufferObjectHandle handle) noexcept
{
    BufferObjectRegistry().Remove(static_cast<std::uint64_t>(handle));
}

} // namespace gte
