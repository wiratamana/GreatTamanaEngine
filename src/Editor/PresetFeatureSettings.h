#pragma once

#include "../Core/FeatureKit/FeatureDebug.h"

// Helpers preset scenes use to read, switch and restore render feature settings.

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

class RenderFeatureCompositor;

// Settings of one feature saved before a preset overwrote them.
struct SavedFeatureSettings {
    std::string feature;
    std::vector<fx::FeatureDebugArg> args;
};

// The value as the text a debug Set() call takes back.
std::string FeatureDebugValueText(const fx::FeatureDebugValue& value);

// The fields of `endpoint` named in `keys`, as arguments that restore them through Set().
std::vector<fx::FeatureDebugArg> SnapshotFeatureSettings(
    const fx::IFeatureDebugEndpoint& endpoint, std::span<const std::string_view> keys);

// Sets the enabled override of the registration `name`. False when it is unknown or the call threw.
bool TrySetFeatureEnabled(RenderFeatureCompositor& compositor, const std::string& name, bool enabled) noexcept;

// Switches the `turnOn` registrations on and the `turnOff` ones off. A failure is logged under `category` and the
// rest still runs. False when anything failed, or when names are given but `compositor` is null.
bool SetFeaturesEnabled(RenderFeatureCompositor* compositor, const char* category, std::span<const std::string> turnOn,
    std::span<const std::string> turnOff) noexcept;

// Sets `saved` back through `endpoint` (named `label` in the log). A failure is logged under `category`.
// True when `saved` is empty; false when the endpoint is null, refuses the values or throws.
bool RestoreFeatureSettings(fx::IFeatureDebugEndpoint* endpoint, const char* category, std::string_view label,
    std::span<const fx::FeatureDebugArg> saved) noexcept;

} // namespace gte
