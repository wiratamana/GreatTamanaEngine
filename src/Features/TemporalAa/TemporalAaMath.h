#pragma once

#include "TemporalAaTypes.h"
#include "../../Core/Extent.h"
#include "../../Core/FeatureKit/ScreenSpaceView.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec2.h"

#include <cstdint>
#include <string_view>

namespace gte {

// Clamps every field into its valid range. A non-finite float becomes the default; feedbackMax is lifted to feedbackMin.
TemporalAaSettings SanitizeTemporalAaSettings(const TemporalAaSettings& settings) noexcept;

// Radical inverse of `index` in `base`, in [0, 1). Zero for a base below 2.
float TemporalAaHalton(std::uint32_t index, std::uint32_t base) noexcept;

// Sub-pixel offset of frame `frameIndex` in pixels: Halton (2, 3) centered on 0, times jitterScale.
// Exactly (0, 0) when jitterScale is 0. `safe` must be sanitized.
Vec2 TemporalAaJitterPixels(std::uint64_t frameIndex, const TemporalAaSettings& safe) noexcept;

// Pixel offset to NDC (2 * pixels / size). Zero for an empty extent.
Vec2 TemporalAaJitterNdc(Vec2 pixels, Extent2D extent) noexcept;

// First failing check wins, in the order of the TemporalAaJitterDecision values after Apply.
TemporalAaJitterDecision DecideTemporalAaJitter(const TemporalAaJitterInputs& inputs) noexcept;

// History share from the luma gap between current and history; CPU reference of TemporalAaResolve.comp.
float TemporalAaFeedback(float lumaCurrent, float lumaHistory, float feedbackMin, float feedbackMax) noexcept;

// `safe` must be sanitized.
TemporalAaParamsGpu MakeTemporalAaParams(const fx::CameraBasis& basis, const fx::TemporalHistory& previous,
    bool historyValid, const TemporalAaSettings& safe, const Mat4& invViewProjection, Extent2D extent) noexcept;

// Stable text of each enum value, shown by the endpoint.
std::string_view TemporalAaStatusName(TemporalAaFrameStatus status) noexcept;
std::string_view TemporalAaJitterDecisionName(TemporalAaJitterDecision decision) noexcept;
std::string_view TemporalAaDebugViewName(TemporalAaDebugView view) noexcept;

} // namespace gte
