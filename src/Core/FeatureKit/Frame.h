#pragma once

#include "SceneSlot.h"
#include "Texture.h"
#include "View.h"
#include "../../Renderer/RenderGraph/StaticName.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace gte {
class Registry;
} // namespace gte

namespace gte::fx {

class ComputeChain;
class ComputeShader;
class FullscreenChain;
class FullscreenShader;
class SceneDrawChain;
class ViewOutput;

namespace detail {
struct FrameContext;
} // namespace detail

// Per-callback context. Textures it hands out carry the serial of this view in this frame (see FrameSerial).
// A frame made without an engine context (tests) turns every verb into a logged no-op.
class Frame {
public:
    Frame(const fx::View& view, Registry& scene, std::uint64_t serial, detail::FrameContext* context = nullptr) noexcept
        : m_view(view)
        , m_scene(&scene)
        , m_serial(serial)
        , m_context(context)
    {
    }

    const fx::View& View() const noexcept { return m_view; }
    rg::ViewScope Scope() const noexcept { return m_view.scope; }
    Registry& Scene() const noexcept { return *m_scene; }
    std::uint64_t Serial() const noexcept { return m_serial; }

    // True when `texture` was made for this view in this frame, or by a Shared callback of this frame.
    // A copy kept from an earlier frame is refused.
    bool Owns(const Texture& texture) const noexcept
    {
        return texture.IsValid() && (texture.frameSerial == m_serial || texture.frameSerial == SharedSerialOf(m_serial));
    }

    // Stamps a handle with this frame's serial.
    Texture Stamp(rg::TextureHandle handle) const noexcept { return Texture{ handle, m_serial }; }

    // One compute dispatch over `shader`. Declares nothing while its pass is toggled off.
    ComputeChain Compute(ComputeShader& shader, rg::StaticName passName);

    // One fullscreen draw with `shader`. Declares nothing while its pass is toggled off.
    FullscreenChain Fullscreen(FullscreenShader& shader, rg::StaticName passName);

    // Keeps a texture alive even when nothing reads it in this frame.
    void Keep(Texture texture);

    // Copies `src` into `dst` with a scaled blit. False when nothing was declared.
    bool Blit(rg::StaticName passName, Texture src, Texture dst, BlitFilter filter);

    // Marks `produced` (the texture `output.Acquire()` returned in this callback) as this view's output for the
    // editor and the HTTP endpoints. Call it only after the pass that writes the texture was declared.
    void ExposeAsViewOutput(ViewOutput& output, Texture produced);

    // A scene redraw (depth pre-pass, outline, ...). Declares nothing while its pass is toggled off.
    SceneDrawChain SceneDraw(rg::StaticName passName);

    // Makes `texture` the content of `slot` for the scene's material shaders. A logged no-op for a bad slot or texture.
    void PublishToScene(SceneSlot slot, Texture texture);

    // Writes this view's scene uniform block (read by material shaders).
    template <typename T>
    void SetSceneUniforms(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "scene uniforms are copied as raw bytes");
        static_assert(sizeof(T) <= kMaxSceneUniformBytes, "scene uniforms must fit the engine's uniform block");
        SetSceneUniformBytes(std::as_bytes(std::span<const T, 1>(&value, 1)));
    }

    // Engine context behind the verbs; null for a frame made outside a render callback.
    detail::FrameContext* Context() const noexcept { return m_context; }

    // Logs `text` as an error once per distinct text (once per frame object without a context).
    void ReportOnce(std::string_view text) const;

protected:
    // The depth a fullscreen draw acknowledges by default (invalid for frames without scene depth).
    void SetAcknowledgedDepth(rg::TextureHandle depth) noexcept { m_acknowledgedDepth = depth; }

private:
    void SetSceneUniformBytes(std::span<const std::byte> bytes);

    fx::View m_view;
    Registry* m_scene;
    std::uint64_t m_serial;
    detail::FrameContext* m_context;
    rg::TextureHandle m_acknowledgedDepth{};
    mutable bool m_noContextLogged = false;
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
        rg::TextureHandle depth, detail::FrameContext* context = nullptr) noexcept
        : Frame(view, scene, serial, context)
        , m_color(color)
        , m_depth(depth)
    {
        SetAcknowledgedDepth(depth);
    }

    Texture SceneColor() const noexcept { return Stamp(m_color); }
    Texture SceneDepth() const noexcept { return Stamp(m_depth); }

    // A fullscreen draw into the view's own color, only where the depth test passes `Equal` (nothing was drawn
    // there), without writing depth. In the Game view the Frame Debugger can replay it. Declares nothing while its
    // pass is toggled off. Add Bind/Push as for any fullscreen draw, then Draw().
    FullscreenChain ViewBackground(FullscreenShader& shader, rg::StaticName passName);

private:
    rg::TextureHandle m_color;
    rg::TextureHandle m_depth;
};

// Near the end of the frame: adds a private target that is blended onto the screen afterwards.
class CompositeFrame : public PostOpaqueFrame {
public:
    CompositeFrame(const fx::View& view, Registry& scene, std::uint64_t serial, rg::TextureHandle color,
        rg::TextureHandle depth, rg::TextureHandle privateTarget, Extent2D extent,
        detail::FrameContext* context = nullptr) noexcept
        : PostOpaqueFrame(view, scene, serial, color, depth, context)
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
    SharedFrame(Registry& scene, std::uint64_t serial, detail::FrameContext* context = nullptr) noexcept
        : Frame(fx::View{}, scene, serial, context)
    {
    }
};

} // namespace gte::fx
