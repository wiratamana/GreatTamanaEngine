#include "RegistrationRunner.h"

#include "../../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../../Renderer/RenderGraph/RenderPassToggleGuard.h"
#include "../Logging.h"

#include <exception>
#include <string>

namespace gte::fx::detail {

namespace {

constexpr const char* kLogCategory = "FeatureKit";

void Invoke(const RunInput& in)
{
    const Registration& registration = *in.registration;
    const View& view = in.view->has_value() ? **in.view : View{};
    switch (registration.stage) {
    case Stage::PreOpaque: {
        PreOpaqueFrame frame(view, *in.scene, in.serial, in.context);
        std::get<PreOpaqueCallback>(registration.callback)(frame);
        break;
    }
    case Stage::Opaque: {
        OpaqueFrame frame(view, *in.scene, in.serial, in.sceneColor, in.context);
        std::get<OpaqueCallback>(registration.callback)(frame);
        break;
    }
    case Stage::PostOpaque:
    case Stage::PostTransparent: {
        PostOpaqueFrame frame(view, *in.scene, in.serial, in.sceneColor, in.sceneDepth, in.context);
        std::get<PostOpaqueCallback>(registration.callback)(frame);
        break;
    }
    case Stage::PostComposite:
    case Stage::PreUI: {
        CompositeFrame frame(
            view, *in.scene, in.serial, in.sceneColor, in.sceneDepth, in.privateTarget, in.extent, in.context);
        std::get<CompositeCallback>(registration.callback)(frame);
        break;
    }
    case Stage::PreOpaqueShared: {
        SharedFrame frame(*in.scene, in.serial, in.context);
        std::get<SharedCallback>(registration.callback)(frame);
        break;
    }
    }
}

} // namespace

bool RunRegistration(const RunInput& in, rg::BoundedOnceSet<32>& failures)
{
    const Registration& registration = *in.registration;
    FrameContext& context = *in.context;

    // Gate first: a disabled pass must not run any side effect.
    const auto isEnabled = [&context](std::string_view pass) {
        return rg::ShouldDeclareBuiltInPassThisFrame(context.toggles, pass);
    };
    if (!GateAllows(registration.gate, registration.name, isEnabled)) {
        return false;
    }

    const bool shared = in.sharedLatch != nullptr;
    if (!ShouldInvokeFeatureCallback(*in.view, shared ? CallbackScope::Shared : CallbackScope::PerView)) {
        return false;
    }
    if (shared && !in.sharedLatch->ShouldRun(in.frameIndex)) {
        return false;
    }

    const rg::RenderFeatureScope scope(*context.builder, context.featureName);
    context.scope = shared ? rg::ViewScope::Shared : (*in.view)->scope;
    context.serial = in.serial;
    // A throw would end the whole offscreen frame; log it once and drop this callback's work.
    const auto fail = [&](const char* what) {
        if (shared) {
            in.sharedLatch->MarkFailed(in.frameIndex);
        }
        const std::string message = "feature '" + std::string(context.featureName) + "', callback '"
            + registration.name + "': " + what;
        if (rg::BoundedOnceSet<32>::ShouldLog(failures.Note(message))) {
            GTE_LOG_ERROR(kLogCategory, message);
        }
        return false;
    };
    try {
        Invoke(in);
    } catch (const std::exception& e) {
        return fail(e.what());
    } catch (...) {
        return fail("unknown exception");
    }
    if (shared) {
        in.sharedLatch->MarkDone(in.frameIndex);
    }
    return true;
}

} // namespace gte::fx::detail
