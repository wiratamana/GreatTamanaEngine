#pragma once

#include "ToonCharacterTypes.h"
#include "../../Core/Extent.h"
#include "../../Game/Lighting/ToonClass.h"
#include "../../Game/Lighting/ToonLook.h"

#include <cstdint>

namespace gte {

// Outline width is given in pixels at this view height and scales with the real height.
inline constexpr float kToonOutlineReferenceHeightPx = 720.0f;

// A shade tone never drops below this share of the lit luminance.
inline constexpr float kToonMinShadeLuminance = 0.35f;

struct ToonRgb {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// Per-class multipliers on the lit color (linear). CPU reference of the tables in ToonSurface.frag and ToonOutline.frag.
struct ToonClassTint {
    ToonRgb shade;   // Tone below the band edge.
    ToonRgb deep;    // Extra tone below the deep edge; white when the class has no deep band.
    ToonRgb outline; // Outline color multiplier on the albedo.
};

// Clamps every field into its valid range. A non-finite float becomes its default.
ToonCharacterSettings SanitizeToonCharacterSettings(const ToonCharacterSettings& settings) noexcept;

// `safe` must be sanitized. The outline thickness is in NDC y units of the view.
ToonLook PackToonLook(const ToonCharacterSettings& safe, Extent2D viewExtent) noexcept;

// Tint of `shadeClass`; an unknown id gives the cloth tint.
const ToonClassTint& ToonTintOf(ToonClass shadeClass) noexcept;

// Classes drawn without lighting.
bool ToonClassIsUnlit(ToonClass shadeClass) noexcept;

// Classes with a third, darker tone.
bool ToonClassHasDeepBand(ToonClass shadeClass) noexcept;

// Rec. 709 luminance of a linear color.
float ToonLuminance(const ToonRgb& color) noexcept;

// The shade tone of `shadeClass` (`deepBand` multiplies the deep tint in), lifted toward white when its
// luminance would fall below kToonMinShadeLuminance.
ToonRgb ToonShadeTone(ToonClass shadeClass, bool deepBand) noexcept;

} // namespace gte
