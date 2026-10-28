#include "HizPyramid.h"

#include "ClusterNamePool.h"
#include "ComputeChain.h"
#include "FrameContext.h"
#include "../PixelFormat.h"

#include <string>

namespace gte::fx {

namespace {

// The pool recycles targets by exact size; rounding keeps a window drag from creating a new one per pixel.
constexpr std::uint32_t kAtlasSizeStep = 256;

constexpr std::uint32_t RoundUpToStep(std::uint32_t value) noexcept
{
    return (value + kAtlasSizeStep - 1) / kAtlasSizeStep * kAtlasSizeStep;
}

} // namespace

HizPyramid::HizPyramid(std::string_view baseName)
    : m_base(baseName)
    , m_atlas(std::string(baseName) + ".Atlas")
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

HizPlan HizPyramid::Plan(Frame& frame, Texture sceneDepth)
{
    HizPlan plan;
    if (!Valid() || frame.Scope() == rg::ViewScope::Shared || !frame.Owns(sceneDepth)) {
        return plan;
    }
    const fx::View& view = frame.View();
    plan.layout = HizLayoutFor(view.extent.width, view.extent.height);
    if (!plan.layout.IsValid()) {
        plan.reason = view.extent.width != 0 && view.extent.height != 0 ? HizReason::TooLarge : HizReason::Unavailable;
        return plan;
    }
    plan.dispatches = HizBuildPlanFor(plan.layout);

    // Ask every toggle and shader before the first side effect.
    for (const char* name : m_passNames) {
        if (!frame.PassEnabled(rg::StaticName::Interned(name))) {
            return plan;
        }
    }
    if (!frame.ShaderReady(m_baseShader) || (plan.dispatches.runTop && !frame.ShaderReady(m_topShader))) {
        return plan;
    }
    plan.reason = HizReason::None;
    return plan;
}

Texture HizPyramid::Build(Frame& frame, const HizPlan& plan, Texture sceneDepth)
{
    if (!Valid() || !plan.Usable() || !frame.Owns(sceneDepth)) {
        return Texture{};
    }
    const fx::View& view = frame.View();
    const Texture atlas = m_atlas.Acquire(frame,
        Extent2D{ RoundUpToStep(plan.layout.atlasWidth), RoundUpToStep(plan.layout.atlasHeight) }, PixelFormat::R32_Float);
    if (!atlas.IsValid()) {
        return Texture{};
    }

    // The base pass reduces the depth into levels 0..4, the top pass finishes the remaining levels.
    const HizBuildPush push = MakeHizBuildPush(view.extent.width, view.extent.height, plan.layout);
    const bool baseDeclared = frame.Compute(m_baseShader, rg::StaticName::Interned(m_passNames[0]))
                                  .Push(push)
                                  .Bind("sceneDepth", Depth(sceneDepth))
                                  .Bind("pyramid", atlas)
                                  .Dispatch(Extent2D{ plan.dispatches.baseThreadsX, plan.dispatches.baseThreadsY });
    if (!baseDeclared) {
        frame.ReportOnce(std::string("Pass '") + m_passNames[0] + "' was refused; occlusion culling is off");
        return Texture{};
    }
    if (plan.dispatches.runTop) {
        const bool topDeclared = frame.Compute(m_topShader, rg::StaticName::Interned(m_passNames[1]))
                                     .Push(push)
                                     .Bind("pyramid", atlas)
                                     .Dispatch(Extent2D{ plan.dispatches.topThreadsX, 1 });
        if (!topDeclared) {
            frame.ReportOnce(std::string("Pass '") + m_passNames[1] + "' was refused; occlusion culling is off");
            return Texture{};
        }
    }
    return atlas;
}

} // namespace gte::fx
