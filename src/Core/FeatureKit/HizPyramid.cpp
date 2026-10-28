#include "HizPyramid.h"

#include "ClusterNamePool.h"
#include "ComputeChain.h"
#include "FrameContext.h"
#include "../Core.h"
#include "../PixelFormat.h"

#include <string>

namespace gte::fx {

HizPyramid::HizPyramid(std::string_view baseName)
    : m_base(baseName)
{
    if (m_base.empty()) {
        m_error = "HizPyramid needs a base name";
        return;
    }
    constexpr const char* kSuffixes[kHizPassCount] = { kHizBasePassSuffix, kHizTopPassSuffix };
    for (std::uint32_t i = 0; i < kHizPassCount; ++i) {
        m_passNames[i] = InternClusterName(m_base + kSuffixes[i]);
        if (m_passNames[i] == nullptr) {
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
    names.reserve(m_passNames.size());
    for (const char* name : m_passNames) {
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
    const HizBuildPlan plan = HizBuildPlanFor(layout);

    // Ask every toggle before the first side effect.
    for (const char* name : m_passNames) {
        if (!frame.PassEnabled(rg::StaticName::Interned(name))) {
            return restart();
        }
    }
    if (!frame.ShaderReady(m_baseShader) || (plan.runTop && !frame.ShaderReady(m_topShader))) {
        return restart();
    }

    const Texture atlas = view.atlas[frame.FrameIndex() & 1u].Acquire(
        frame, Extent2D{ layout.atlasWidth, layout.atlasHeight }, PixelFormat::R32_Float);
    if (!atlas.IsValid()) {
        return restart();
    }

    // The base pass reduces the depth into levels 0..4, the top pass finishes the remaining levels.
    const HizBuildPush push = MakeHizBuildPush(v.extent.width, v.extent.height, layout);
    const bool baseDeclared = frame.Compute(m_baseShader, rg::StaticName::Interned(m_passNames[0]))
                                  .Push(push)
                                  .Bind("sceneDepth", Depth(frame.SceneDepth()))
                                  .Bind("pyramid", atlas)
                                  .Dispatch(Extent2D{ plan.baseThreadsX, plan.baseThreadsY });
    if (!baseDeclared) {
        frame.ReportOnce(std::string("Pass '") + m_passNames[0] + "' was refused; occlusion culling is off");
        return restart();
    }
    if (plan.runTop) {
        const bool topDeclared = frame.Compute(m_topShader, rg::StaticName::Interned(m_passNames[1]))
                                     .Push(push)
                                     .Bind("pyramid", atlas)
                                     .Dispatch(Extent2D{ plan.topThreadsX, 1 });
        if (!topDeclared) {
            frame.ReportOnce(std::string("Pass '") + m_passNames[1] + "' was refused; occlusion culling is off");
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
    if (v.extent.width != 0 && v.extent.height != 0 && !HizLayoutFor(v.extent.width, v.extent.height).IsValid()) {
        result.reason = HizReason::TooLarge;
        return result;
    }
    if (const HizReason reason = HizUnusableReason(view.validity, frame.FrameIndex(), v.extent.width, v.extent.height);
        reason != HizReason::None) {
        result.reason = reason;
        return result;
    }
    if (HizCameraCut(view.viewProjection, view.eye, v.viewProjection, v.eye)) {
        result.reason = HizReason::CameraCut;
        return result;
    }

    // The atlas of the other parity was written last frame; this frame's build writes this parity.
    const Texture atlas = view.atlas[(frame.FrameIndex() & 1u) ^ 1u].Acquire(
        frame, Extent2D{ view.layout.atlasWidth, view.layout.atlasHeight }, PixelFormat::R32_Float);
    if (!atlas.IsValid()) {
        return result;
    }
    result.usable = true;
    result.reason = HizReason::None;
    result.atlas = atlas;
    result.viewProjection = view.viewProjection;
    result.layout = view.layout;
    return result;
}

} // namespace gte::fx
