#pragma once

#include "../Renderer/OffscreenFrameTicket.h"
#include "Time.h"

#include <optional>

namespace gte {

// Per-frame state the simulation layer needs. Owned by Application, advanced once per frame and
// passed to Game::Update() by const reference. Add fields one justified field at a time.
struct EngineContext {
    Time time;

    // The frame slot this update may write GPU-visible memory for. Empty when no slot is free: write nothing.
    // The default is a synchronous frame, which nothing can overlap.
    std::optional<OffscreenFrameTicket> gpuFrame = OffscreenFrameTicket{};
};

} // namespace gte
