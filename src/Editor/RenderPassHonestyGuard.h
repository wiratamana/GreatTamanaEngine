#pragma once

#include "RenderGraphConsistencyGuard.h"
#include "RenderPassHonestyChecker.h"

#include <string>

// Logger-aware singleton wrapper around the pure
// DetectRenderPassHonestyMismatches() (RenderPassHonestyChecker.h). Logs
// "RenderPassHonesty" exactly once per pass name newly found both disabled
// and still executed, since the last Frame Debugger capture.
namespace gte {

struct RenderPassHonestyGuardTag {
    static constexpr const char* LogCategory() noexcept { return "RenderPassHonesty"; }
    static std::string FormatMessage(const std::string& passName);
};

using RenderPassHonestyGuard = ConsistencyGuard<RenderPassHonestyGuardTag>;

} // namespace gte
