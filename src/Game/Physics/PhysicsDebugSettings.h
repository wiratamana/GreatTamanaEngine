#pragma once
#include "../../Physics/GlobalPhysicsSettings.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {

using PhysicsSettingPairs = std::vector<std::pair<std::string, std::string>>;

// Applies "key=value" pairs to `settings`. ALL OR NOTHING: on any error nothing changes and false is returned
// with `errorMessage` set and `errorKey` naming the offending key. Strict parsing: no "12abc", no leading '+',
// finite numbers only, bool = "true"|"false"|"1"|"0", enum text is case-exact, unknown or duplicate key = error.
bool ApplyPhysicsSettingPairs(GlobalPhysicsSettings& settings, const PhysicsSettingPairs& pairs,
    std::string& errorMessage, std::string& errorKey);

// "default" or "legacy". Returns false for any other name (nothing changes).
bool ApplyPhysicsPreset(GlobalPhysicsSettings& settings, std::string_view presetName);

// key -> value text for every row, in table order (fixed_step_hz prints the rate, not the step length).
PhysicsSettingPairs DescribeSettings(const GlobalPhysicsSettings& settings);

// True for the per-entity keys `rig_enabled` and `rig_frozen` (they are not part of the global table).
bool IsPhysicsEntitySettingKey(std::string_view key);

} // namespace gte
