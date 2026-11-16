#pragma once

// When a slider edit is sent to the world. Cheap changes go at once; rebuilds of the whole world wait for the
// slider to be released or to rest, so dragging does not restart the streaming every frame.

#include "TerrainSettings.h"

namespace gte::terrain {

struct ApplyPolicy {
    double idleSeconds = 0.35; // A held slider that has not moved for this long counts as settled.
};

struct EditState {
    SettingsChange pending = SettingsChange::None; // ClassifyChange(applied settings, edited settings).
    bool dragging = false;                         // A slider is held right now.
    bool livePreview = false;                      // The single preview chunk is on screen.
    double secondsSinceEdit = 0.0;
};

// True when the edited settings should be sent to the world this frame.
bool ShouldApplyNow(const EditState& state, const ApplyPolicy& policy = {}) noexcept;

} // namespace gte::terrain
