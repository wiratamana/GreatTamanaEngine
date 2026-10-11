#include "UiTextureBackend.h"

#include <mutex>

namespace gte {

namespace {
std::mutex g_mutex;
std::function<VkDescriptorSet(VkSampler, VkImageView)> g_addFn;
std::function<void(VkDescriptorSet)> g_removeFn;
} // namespace

void SetUiTextureBackend(
    std::function<VkDescriptorSet(VkSampler, VkImageView)> addFn, std::function<void(VkDescriptorSet)> removeFn)
{
    std::lock_guard lock(g_mutex);
    g_addFn = std::move(addFn);
    g_removeFn = std::move(removeFn);
}

VkDescriptorSet AddUiTexture(VkSampler sampler, VkImageView view)
{
    std::lock_guard lock(g_mutex);
    return g_addFn ? g_addFn(sampler, view) : VK_NULL_HANDLE;
}

void RemoveUiTexture(VkDescriptorSet set)
{
    std::lock_guard lock(g_mutex);
    if (g_removeFn) {
        g_removeFn(set);
    }
}

} // namespace gte
