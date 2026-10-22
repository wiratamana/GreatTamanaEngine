#pragma once

#include "ClusterDrawTypes.h"
#include "GpuBufferRef.h"
#include "GpuReadback.h"
#include "SceneSlot.h"
#include "Texture.h"
#include "View.h"
#include "../../Renderer/RenderGraph/StaticName.h"

#include <cstddef>
#include <cstdint>
#include <limits>
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

// Longest frame time the kit reports; a stall counts as this much.
inline constexpr float kMaxFrameDeltaSeconds = 0.25f;

// NaN, infinite or negative gives 0; above the maximum gives the maximum.
constexpr float ClampFrameDeltaSeconds(double seconds) noexcept
{
    if (!(seconds > 0.0) || seconds > std::numeric_limits<double>::max()) {
        return 0.0f;
    }
    return seconds > static_cast<double>(kMaxFrameDeltaSeconds) ? kMaxFrameDeltaSeconds : static_cast<float>(seconds);
}

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

    // Frame counter; the same for every view scope of one frame.
    std::uint64_t FrameIndex() const noexcept { return m_serial == 0 ? 0 : (m_serial - 1) / rg::kViewScopeCount; }

    // Real seconds since the previous frame, clamped to kMaxFrameDeltaSeconds. 0 without an engine context.
    float DeltaSeconds() const noexcept;

    // True when `shader` is built (builds it now if needed). False while a failed build cools down.
    bool ShaderReady(ComputeShader& shader);

    // True when `texture` was made for this view in this frame, or by a Shared callback of this frame.
    // A copy kept from an earlier frame is refused.
    bool Owns(const Texture& texture) const noexcept
    {
        return texture.IsValid() && (texture.frameSerial == m_serial || texture.frameSerial == SharedSerialOf(m_serial));
    }

    // Stamps a handle with this frame's serial.
    Texture Stamp(rg::TextureHandle handle) const noexcept { return Texture{ handle, m_serial }; }

    // True when `buffer` was made for this view in this frame, or by a Shared callback of this frame.
    bool Owns(const GpuBufferRef& buffer) const noexcept
    {
        return buffer.IsValid() && (buffer.frameSerial == m_serial || buffer.frameSerial == SharedSerialOf(m_serial));
    }

    // Stamps a graph buffer handle with this frame's serial.
    GpuBufferRef StampBuffer(rg::BufferHandle handle, std::uint64_t byteSize, BufferUse use) const noexcept
    {
        return GpuBufferRef{ handle, m_serial, byteSize, use };
    }

    // False while the pass `passName` is toggled off (or the frame has no engine context).
    // Ask it before any side effect (Acquire, resize) that only an enabled pass needs.
    bool PassEnabled(rg::StaticName passName) const;

    // One compute dispatch over `shader`. Declares nothing while its pass is toggled off.
    ComputeChain Compute(ComputeShader& shader, rg::StaticName passName);

    // One fullscreen draw with `shader`. Declares nothing while its pass is toggled off.
    FullscreenChain Fullscreen(FullscreenShader& shader, rg::StaticName passName);

    // Keeps a texture alive even when nothing reads it in this frame.
    void Keep(Texture texture);

    // Copies `src` into `dst` with a scaled blit. False when nothing was declared.
    bool Blit(rg::StaticName passName, Texture src, Texture dst, BlitFilter filter);

    // Fills this frame's device buffer of `readback` with `fillValue`. False when nothing was declared.
    template <typename T>
    bool ClearReadback(rg::StaticName passName, GpuReadback<T>& readback, std::uint32_t fillValue)
    {
        return ClearReadbackCore(passName, detail::ReadbackAccess::Of(readback), fillValue);
    }

    // Copies this frame's device buffer of `readback` to its host buffer. False when nothing was declared.
    template <typename T>
    bool CopyReadbackToHost(rg::StaticName passName, GpuReadback<T>& readback)
    {
        return CopyReadbackCore(passName, detail::ReadbackAccess::Of(readback));
    }

    // Clears the first `buffer.byteSize` bytes of `buffer` (rounded up to 4) with the 32-bit pattern `value`.
    // False when nothing was declared.
    bool FillBuffer(rg::StaticName passName, GpuBufferRef buffer, std::uint32_t value);

    // Marks `produced` (the texture `output.Acquire()` returned in this callback) as this view's output for the
    // editor and the HTTP endpoints. Call it only after the pass that writes the texture was declared.
    void ExposeAsViewOutput(ViewOutput& output, Texture produced);

    // A scene redraw (depth pre-pass, outline, ...). Declares nothing while its pass is toggled off.
    SceneDrawChain SceneDraw(rg::StaticName passName);

    // Instances of cluster-ready meshes in this frame, in stable order. Empty without an engine context, in a
    // shared frame, or when the device cannot run the cluster path. Valid for this frame only.
    std::span<const ClusterInstance> ClusterCandidates() const noexcept;

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
    bool ClearReadbackCore(rg::StaticName passName, detail::ReadbackCore& core, std::uint32_t fillValue);
    bool CopyReadbackCore(rg::StaticName passName, detail::ReadbackCore& core);

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

// During opaque geometry: passes declared here write the view's own color and depth (load, never clear).
class OpaqueFrame : public Frame {
public:
    OpaqueFrame(const fx::View& view, Registry& scene, std::uint64_t serial, rg::TextureHandle target,
        detail::FrameContext* context = nullptr) noexcept
        : Frame(view, scene, serial, context)
        , m_target(target)
    {
    }

    // The view's color + depth target.
    Texture SceneTarget() const noexcept { return Stamp(m_target); }

private:
    rg::TextureHandle m_target;
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

    // A fullscreen draw into the view's own color where nothing was drawn (depth test Equal, no depth write).
    // Add Bind/Push, then Draw(). Declares nothing while its pass is toggled off.
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
