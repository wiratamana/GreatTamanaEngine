#include "RenderFeatureCompositorImpl.h"

#include "RenderFeatureBlendPush.h"
#include "RenderFeatureCameraData.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Application/RenderPassViewData.h"
#include "../../Renderer/RenderGraph/ComputePass.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../../Renderer/RenderGraph/RenderPassTagValidation.h"

#include <algorithm>
#include <cassert>
#include <string>

namespace gte {

namespace {

constexpr const char* kTargetOwner = "RenderFeatureCompositor";

} // namespace

rg::TextureHandle RenderFeatureCompositor::Impl::AcquireTarget(
    rg::RenderGraphBuilder& builder, const char* internedName, Extent2D extent)
{
    const rg::RenderFeatureScope owner(builder, kTargetOwner);
    return targets[internedName].Acquire(builder, internedName, extent, PixelFormat::RGBA16_Float);
}

bool RenderFeatureCompositor::Impl::DispatchBlend(rg::RenderGraphBuilder& builder, rg::TextureHandle dstIn,
    rg::TextureHandle srcIn, rg::TextureHandle destination, rg::StaticName passName, rg::ViewScope scope,
    Extent2D extent, RenderFeatureBlendMode blendMode, std::string_view groupHeading)
{
    const rg::RenderFeatureScope heading(builder, groupHeading);
    return rg::ComputePass(builder, blendShader, passName, scope, rg::RenderPassEvent::AfterEverything)
        .Bind("dstIn", dstIn)
        .Bind("srcIn", srcIn)
        .Bind("destinationImage", destination)
        .Push(MakeRenderFeatureBlendPush(blendMode))
        .Dispatch(extent);
}

void RenderFeatureCompositor::Impl::ContributeRenderGraphPasses(
    const rg::RenderPassFrameContext& frame, std::vector<rg::RenderPassDesc>&)
{
    std::vector<Entry> combinedList;
    combinedList.reserve(postComposite.size() + preUi.size());
    combinedList.insert(combinedList.end(), postComposite.begin(), postComposite.end());
    combinedList.insert(combinedList.end(), preUi.begin(), preUi.end());

    combinedList.erase(std::remove_if(combinedList.begin(), combinedList.end(),
        [this](const Entry& entry) {
            return !IsEffectivelyEnabled(entry.descriptor.name, entry.enabledOverride);
        }), combinedList.end());
    if (combinedList.empty()) {
        return;
    }

    const std::optional<Core::PluginRenderFeatureTargetInfo> resolved = core.FindPluginRenderFeatureTarget(frame);
    if (!resolved.has_value()) {
        return;
    }

    // Views other than the game view share the scene view's identity.
    const bool isGameView = TranslateLegacyViewScope(frame.currentView) == rg::ViewScope::GameView;
    const rg::ViewScope scope = isGameView ? rg::ViewScope::GameView : rg::ViewScope::SceneView;
    const std::string viewName = isGameView ? "Game" : "Scene";
    const Extent2D extent{ resolved->extent.width, resolved->extent.height };
    frame.finalTextureOutputs.push_back(resolved->target);

    const rg::TextureHandle seed = AcquireTarget(frame.builder, namePool.SeedName(viewName), extent);
    if (!seed.IsValid()
        || !DispatchBlend(frame.builder, resolved->target, resolved->target, seed,
            rg::StaticName::Interned(namePool.SeedCopyPassName(viewName)), scope, extent,
            RenderFeatureBlendMode::Replace, "Post Processing")) {
        return;
    }

    rg::TextureHandle currentInput = seed;
    bool anyBlended = false;
    bool wroteTarget = false;
    for (std::size_t i = 0; i < combinedList.size(); ++i) {
        const Entry& entry = combinedList[i];
        const std::string featureName = entry.descriptor.name;
        const std::string gpuStateKey = entry.projectCallback
            ? ("ProjectFeatureSlot" + std::to_string(entry.projectFeatureSlot))
            : featureName;
        const bool isLast = (i + 1 == combinedList.size());

        const rg::TextureHandle privateTarget =
            AcquireTarget(frame.builder, namePool.PrivateName(gpuStateKey, viewName), extent);
        if (!privateTarget.IsValid()) {
            return;
        }

        bool declaredPasses = true;
        if (entry.projectCallback) {
            const rg::RenderFeatureScope featureScope(frame.builder, featureName);
            const RenderPassViewData* viewData = core.FindRenderPassViewDataNoCopy(frame.currentView);
            ScenePassReadHandles handles;
            handles.colorHandle = currentInput;
            bool viewHasSampleableDepth = false;
            if (viewData != nullptr) {
                handles.depthHandle = viewData->colorTarget;
                viewHasSampleableDepth = frame.builder.HasSampleableDepth(viewData->colorTarget);
            }

            const RenderFeatureCameraData cameraData = ResolveRenderFeatureCameraData(frame.currentView, viewData);

            const std::size_t before = frame.builder.DeclaredPassCount();
            entry.projectCallback(
                frame.builder, frame.blackboard, frame.currentView, privateTarget, extent, handles, cameraData);
            const std::size_t after = frame.builder.DeclaredPassCount();
            declaredPasses = after != before;

            // Kit features declare every sample through named bindings; only raw project callbacks can forget.
            if (viewHasSampleableDepth && entry.owner == RenderFeatureOwner::Project
                && FindMissingDeclaredDepthReadForResolve(frame.builder, before, after, handles.depthHandle)) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    "Render feature '" + featureName + "' declared a pass but never declared a depth "
                    "ReadTexture() on the depth handle it was given; sampling it anyway is an unbarriered read.");
                assert(false && "A render feature declared a pass without declaring a matching depth ReadTexture()");
            }

            if (const std::vector<std::size_t> mistagged =
                    FindPassesNotTaggedAfterEverything(frame.builder, before, after);
                !mistagged.empty()) {
                GTE_LOG_WARNING("RenderFeatureCompositor",
                    "Render feature '" + featureName + "' declared " + std::to_string(mistagged.size())
                    + " pass(es) not tagged RenderPassEvent::AfterEverything; every PostComposite/PreUI pass "
                    "must use that tag.");
                assert(false && "A render feature declared a pass with a RenderPassEvent other than AfterEverything");
            }
        }

        // A callback that declared nothing did not write its private target: its pixels are stale.
        if (!declaredPasses) {
            continue;
        }

        // The last blend writes the view target itself; every other blend writes an accumulator.
        const rg::TextureHandle output = isLast
            ? resolved->target
            : AcquireTarget(frame.builder, namePool.AccumName(gpuStateKey, viewName), extent);
        if (!output.IsValid()
            || !DispatchBlend(frame.builder, currentInput, privateTarget, output,
                rg::StaticName::Interned(namePool.BlendPassName(gpuStateKey, viewName)), scope, extent,
                entry.descriptor.blendMode, entry.projectCallback ? "Project Features" : "Post Processing")) {
            return;
        }
        currentInput = output;
        anyBlended = true;
        wroteTarget = isLast;
    }

    // The last entry may have been skipped: the view target still needs the running result.
    if (anyBlended && !wroteTarget) {
        DispatchBlend(frame.builder, currentInput, currentInput, resolved->target,
            rg::StaticName::Interned(namePool.FinalCopyPassName(viewName)), scope, extent,
            RenderFeatureBlendMode::Replace, "Post Processing");
    }
}

} // namespace gte
