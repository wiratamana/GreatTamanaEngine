#include "PresentRecoveryPlan.h"

namespace gte {

bool PresentRecoveryPlan::Contains(PresentRecoveryStep step) const noexcept
{
    for (const PresentRecoveryStep s : *this) {
        if (s == step) {
            return true;
        }
    }
    return false;
}

PresentRecoveryPlan PlanPresentRecovery(const PresentRecoveryInput& in) noexcept
{
    PresentRecoveryPlan plan;
    const auto add = [&plan](PresentRecoveryStep step) noexcept { plan.steps[plan.count++] = step; };

    if (in.capturePending) {
        add(PresentRecoveryStep::AbandonCapture);
    }
    if (in.deviceLost || !in.imageAcquired) {
        return plan;
    }

    add(PresentRecoveryStep::ResetCommandBuffer);
    add(PresentRecoveryStep::TransitionToPresent);
    if (!in.fenceReset) {
        add(PresentRecoveryStep::ResetFence);
    }
    add(PresentRecoveryStep::SubmitAndSignal);
    add(PresentRecoveryStep::Present);
    add(PresentRecoveryStep::AdvanceFrame);
    return plan;
}

} // namespace gte
