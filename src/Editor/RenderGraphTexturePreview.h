#pragma once

#include "../Core/UiTextureHandle.h"
#include "../Renderer/Texture2D.h"

#include <optional>
#include <string>

namespace gte {

class Renderer;

// Owns at most one ImGui-sampled texture preview: one Texture2D, one opaque
// UiTextureHandle. Requesting a new preview always releases the old one
// first. Destructor releases both - no caller-side cleanup required.
//
// Must be destroyed before ImGui_ImplVulkan_Shutdown() runs, like every
// other panel-owned GPU resource in this editor.
class RenderGraphTexturePreview {
public:
    RenderGraphTexturePreview() = default;
    ~RenderGraphTexturePreview() { Release(); }

    RenderGraphTexturePreview(const RenderGraphTexturePreview&) = delete;
    RenderGraphTexturePreview& operator=(const RenderGraphTexturePreview&) = delete;

    // `renderer` is captured here, once, and reused by every later Release().
    void Request(Renderer& renderer, std::string resourceName, Texture2D texture);

    // Waits for the GPU only if there is actually something live to
    // release - an empty Release() (e.g. at shutdown, after an already-
    // empty preview) is a free no-op, never a redundant stall.
    void Release();

    UiTextureHandle Descriptor() const noexcept { return m_descriptor; }
    const std::string& ResourceName() const noexcept { return m_resourceName; }
    int Width() const noexcept { return m_texture.has_value() ? m_texture->Width() : 0; }
    int Height() const noexcept { return m_texture.has_value() ? m_texture->Height() : 0; }

private:
    Renderer* m_renderer = nullptr; // Non-owning - captured by Request(), reused by Release().
    std::string m_resourceName;
    std::optional<Texture2D> m_texture;
    UiTextureHandle m_descriptor = UiTextureHandle::Invalid;
};

} // namespace gte
