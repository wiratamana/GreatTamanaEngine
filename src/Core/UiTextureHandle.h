#pragma once

#include <cstdint>

namespace gte {

// Opaque, trivially-copyable, backend-agnostic handle to a texture
// registered for display by an immediate-mode UI (ImGui::Image()). See
// Renderer::GetUiTextureHandle()/ReleaseUiTextureHandle() - never crosses
// paths with a raw VkDescriptorSet outside src/Renderer.
enum class UiTextureHandle : std::uint64_t { Invalid = 0 };

} // namespace gte
