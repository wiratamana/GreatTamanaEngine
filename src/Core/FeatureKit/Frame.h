#pragma once

#include "Texture.h"
#include "View.h"

#include <cstdint>

namespace gte {
class Registry;
} // namespace gte

namespace gte::fx {

// Per-callback context. One serial per callback invocation; textures it hands out carry that serial.
class Frame {
public:
    Frame(const fx::View& view, Registry& scene, std::uint64_t serial) noexcept
        : m_view(view)
        , m_scene(&scene)
        , m_serial(serial)
    {
    }

    const fx::View& View() const noexcept { return m_view; }
    rg::ViewScope Scope() const noexcept { return m_view.scope; }
    Registry& Scene() const noexcept { return *m_scene; }
    std::uint64_t Serial() const noexcept { return m_serial; }

    // True when `texture` was produced by this callback. A copy kept from an earlier frame is refused.
    bool Owns(const Texture& texture) const noexcept { return texture.IsValid() && texture.frameSerial == m_serial; }

    // Stamps a handle with this frame's serial.
    Texture Stamp(rg::TextureHandle handle) const noexcept { return Texture{ handle, m_serial }; }

private:
    fx::View m_view;
    Registry* m_scene;
    std::uint64_t m_serial;
};

// Before any opaque geometry, once per active view.
class PreOpaqueFrame : public Frame {
public:
    using Frame::Frame;
};

// After opaque (or transparent) geometry: the view's own color and depth can be read.
class PostOpaqueFrame : public Frame {
public:
    PostOpaqueFrame(const fx::View& view, Registry& scene, std::uint64_t serial, rg::TextureHandle color,
        rg::TextureHandle depth) noexcept
        : Frame(view, scene, serial)
        , m_color(color)
        , m_depth(depth)
    {
    }

    Texture SceneColor() const noexcept { return Stamp(m_color); }
    Texture SceneDepth() const noexcept { return Stamp(m_depth); }

private:
    rg::TextureHandle m_color;
    rg::TextureHandle m_depth;
};

// Near the end of the frame: adds a private target that is blended onto the screen afterwards.
class CompositeFrame : public PostOpaqueFrame {
public:
    CompositeFrame(const fx::View& view, Registry& scene, std::uint64_t serial, rg::TextureHandle color,
        rg::TextureHandle depth, rg::TextureHandle privateTarget, Extent2D extent) noexcept
        : PostOpaqueFrame(view, scene, serial, color, depth)
        , m_privateTarget(privateTarget)
        , m_extent(extent)
    {
    }

    Texture PrivateTarget() const noexcept { return Stamp(m_privateTarget); }
    Extent2D Extent() const noexcept { return m_extent; }

private:
    rg::TextureHandle m_privateTarget;
    Extent2D m_extent;
};

// Once per frame, not per view; the scope is always Shared.
class SharedFrame : public Frame {
public:
    SharedFrame(Registry& scene, std::uint64_t serial) noexcept
        : Frame(fx::View{}, scene, serial)
    {
    }
};

} // namespace gte::fx
