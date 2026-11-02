#pragma once
#include "../../Physics/GlobalPhysicsSettings.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {

using PhysicsSettingPairs = std::vector<std::pair<std::string, std::string>>;

// Strict bool text: "true"|"1" or "false"|"0". Anything else returns false and leaves `out` alone.
bool ParsePhysicsBool(std::string_view text, bool& out) noexcept;

// Applies "key=value" pairs. ALL OR NOTHING: on any error nothing changes, false is returned and
// `errorKey` names the offending key. Strict parsing; unknown or duplicate keys are errors.
bool ApplyPhysicsSettingPairs(GlobalPhysicsSettings& settings, const PhysicsSettingPairs& pairs,
    std::string& errorMessage, std::string& errorKey);

// "default" or "legacy". Returns false for any other name (nothing changes).
bool ApplyPhysicsPreset(GlobalPhysicsSettings& settings, std::string_view presetName);

// key -> value text for every row, in table order (fixed_step_hz prints the rate, not the step length).
PhysicsSettingPairs DescribeSettings(const GlobalPhysicsSettings& settings);

// True for the per-entity keys `rig_enabled` and `rig_frozen` (they are not part of the global table).
bool IsPhysicsEntitySettingKey(std::string_view key);

} // namespace gte
