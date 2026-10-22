#include "HizPyramid.h"

#include "ClusterNamePool.h"
#include "ComputeChain.h"
#include "FrameContext.h"
#include "../Core.h"
#include "../PixelFormat.h"

#include <string>

namespace gte::fx {

namespace {

// Must match the push block of HizBuild.comp.
struct HizBuildPush {
    std::uint32_t levelInfo[4]; // level, scene depth texels per level texel, atlas origin x and y.
    std::uint32_t sizes[4];     // level width and height, scene depth width and height.
};
static_assert(sizeof(HizBuildPush) == 32);

} // namespace

HizPyramid::HizPyramid(std::string_view baseName)
    : m_base(baseName)
{
    if (m_base.empty()) {
        m_error = "HizPyramid needs a base name";
        return;
    }
    for (std::uint32_t level = 0; level < kHizLevelCount; ++level) {
        m_levelNames[level] = InternClusterName(m_base + ".Level" + std::to_string(level));
        if (m_levelNames[level] == nullptr) {
            m_error = "HizPyramid '" + m_base + "': the pass name table is full";
            return;
        }
    }
}

std::vector<std::string> HizPyramid::AllPassNames() const
{
    std::vector<std::string> names;
    if (!Valid()) {
        return names;
    }
    names.reserve(m_levelNames.size());
    for (const char* name : m_levelNames) {
        names.emplace_back(name);
    }
    return names;
}

void HizPyramid::Reset(Frame& frame)
{
    m_views.For(frame.Scope()).validity = HizValidity{};
}

bool HizPyramid::Build(PostOpaqueFrame& frame)
{
    HizViewState& view = m_views.For(frame.Scope());
    const fx::View& v = frame.View();

    // The next frame moves its movement tracking on only while a pyramid is being built.
    if (detail::FrameContext* context = frame.Context(); context != nullptr && context->core != nullptr) {
        context->core->RequestOcclusionTracking();
    }

    const auto restart = [&]() {
        view.validity = HizValidity{};
        return false;
    };
    if (!Valid() || frame.Scope() == rg::ViewScope::Shared) {
        return restart();
    }
    const HizLayout layout = HizLayoutFor(v.extent.width, v.extent.height);
    if (!layout.IsValid() || !CameraIsFinite(v.viewProjection, v.eye)) {
        return restart();
    }

    // Ask every toggle before the first side effect.
    for (const char* name : m_levelNames) {
        if (!frame.PassEnabled(rg::StaticName::Interned(name))) {
            return restart();
        }
    }
    if (!frame.ShaderReady(m_shader)) {
        return restart();
    }

    const Texture atlas = view.atlas[frame.FrameIndex() & 1u].Acquire(
        frame, Extent2D{ layout.atlasWidth, layout.atlasHeight }, PixelFormat::R32_Float);
    if (!atlas.IsValid()) {
        return restart();
    }

    // Every level reads the scene depth and writes only its own region of the atlas.
    for (std::uint32_t level = 0; level < kHizLevelCount; ++level) {
        const HizLevel& info = layout.levels[level];
        const HizBuildPush push{ { level, info.blockSize, info.originX, info.originY },
            { info.width, info.height, v.extent.width, v.extent.height } };
        const bool declared = frame.Compute(m_shader, rg::StaticName::Interned(m_levelNames[level]))
                                  .Push(push)
                                  .Bind("sceneDepth", Depth(frame.SceneDepth()))
                                  .Bind("hizOutput", atlas)
                                  .Dispatch(Extent2D{ info.width, info.height });
        if (!declared) {
            frame.ReportOnce(std::string("Pass '") + m_levelNames[level] + "' was refused; occlusion culling is off");
            return restart();
        }
    }

    const bool cut = view.validity.hasBuilt && HizCameraCut(view.viewProjection, view.eye, v.viewProjection, v.eye);
    view.validity = AdvanceHizValidity(view.validity, frame.FrameIndex(), v.extent.width, v.extent.height, cut);
    view.layout = layout;
    view.viewProjection = v.viewProjection;
    view.eye = v.eye;
    return true;
}

HizPrevious HizPyramid::Previous(Frame& frame)
{
    HizPrevious result;
    if (!Valid() || frame.Scope() == rg::ViewScope::Shared) {
        return result;
    }
    HizViewState& view = m_views.For(frame.Scope());
    const fx::View& v = frame.View();
    if (!HizUsableAt(view.validity, frame.FrameIndex(), v.extent.width, v.extent.height)
        || HizCameraCut(view.viewProjection, view.eye, v.viewProjection, v.eye)) {
        return result;
    }

    // The atlas of the other parity was written last frame; this frame's build writes this parity.
    const Texture atlas = view.atlas[(frame.FrameIndex() & 1u) ^ 1u].Acquire(
        frame, Extent2D{ view.layout.atlasWidth, view.layout.atlasHeight }, PixelFormat::R32_Float);
    if (!atlas.IsValid()) {
        return result;
    }
    result.usable = true;
    result.atlas = atlas;
    result.viewProjection = view.viewProjection;
    result.baseWidth = view.layout.baseWidth;
    result.baseHeight = view.layout.baseHeight;
    return result;
}

} // namespace gte::fx
