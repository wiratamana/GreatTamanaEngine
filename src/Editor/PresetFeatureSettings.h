#pragma once

#include "../Core/FeatureKit/FeatureDebug.h"

// Helpers preset scenes use to read, switch and restore render feature settings.

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte {

class RenderFeatureCompositor;

// The value as the text a debug Set() call takes back.
std::string FeatureDebugValueText(const fx::FeatureDebugValue& value);

// The fields of `endpoint` named in `keys`, as arguments that restore them through Set().
std::vector<fx::FeatureDebugArg> SnapshotFeatureSettings(
    const fx::IFeatureDebugEndpoint& endpoint, std::span<const std::string_view> keys);

// Sets the enabled override of the registration `name`. False when it is unknown or the call threw.
bool TrySetFeatureEnabled(RenderFeatureCompositor& compositor, const std::string& name, bool enabled) noexcept;

} // namespace gte
