#pragma once

#include "../Core/UiTextureHandle.h"
#include "../Renderer/Texture2D.h"

#include <memory>
#include <string>

namespace gte {

class Renderer;

// Owns at most one ImGui-sampled texture preview: one Texture2D and one UI texture handle.
// Requesting a new preview releases the old one first; the destructor releases both.
// Must be destroyed before the UI backend shuts down, like every other panel-owned GPU resource.
class RenderGraphTexturePreview {
public:
    RenderGraphTexturePreview() = default;
    ~RenderGraphTexturePreview() { Release(); }

    RenderGraphTexturePreview(const RenderGraphTexturePreview&) = delete;
    RenderGraphTexturePreview& operator=(const RenderGraphTexturePreview&) = delete;

    // `renderer` is captured here, once, and reused by every later Release().
    void Request(Renderer& renderer, std::string resourceName, Texture2D texture);

    // Stalls the GPU only when something is live; releasing an empty preview is free.
    void Release();

    UiTextureHandle Descriptor() const noexcept { return m_descriptor; }
    const std::string& ResourceName() const noexcept { return m_resourceName; }
    int Width() const noexcept { return m_texture ? m_texture->Width() : 0; }
    int Height() const noexcept { return m_texture ? m_texture->Height() : 0; }

private:
    Renderer* m_renderer = nullptr; // Non-owning - captured by Request(), reused by Release().
    std::string m_resourceName;
    std::unique_ptr<Texture2D> m_texture;
    UiTextureHandle m_descriptor = UiTextureHandle::Invalid;
};

} // namespace gte
