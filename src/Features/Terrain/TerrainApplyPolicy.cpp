#include "TerrainApplyPolicy.h"

namespace gte::terrain {

bool ShouldApplyNow(const EditState& state, const ApplyPolicy& policy) noexcept
{
    if (state.pending == SettingsChange::None) {
        return false;
    }
    const bool settled = !state.dragging || state.secondsSinceEdit >= policy.idleSeconds;
    // A layout change reopens the scene: only after the slider is released.
    if (HasChange(state.pending, SettingsChange::Geometry)) {
        return !state.dragging;
    }
    if (HasChange(state.pending, SettingsChange::Height)) {
        // The preview chunk is cheap to rebuild; the streamed world is not.
        return state.livePreview || settled;
    }
    return true;
}

} // namespace gte::terrain
