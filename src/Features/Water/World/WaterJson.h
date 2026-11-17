#pragma once

// JSON text of water settings and bodies (save files, HTTP routes).

#include "WaterBodySet.h"
#include "WaterSettings.h"

#include <string>
#include <vector>

namespace gte::water {

std::string SettingsToJson(const WaterSettings& settings);
// {"settings": {...}, "bodies": [...]}; the output of this is accepted by ParseWorldJson.
std::string WorldToJson(const WaterBodySet& set);

// Missing keys keep the value already in `settings`. Returns false and sets `error` on malformed text.
bool ParseSettingsJson(const std::string& text, WaterSettings& settings, std::string& error);
bool ParseWorldJson(const std::string& text, WaterSettings& settings, std::vector<WaterBody>& bodies, std::string& error);

// Names of the settings keys, in a fixed order (HTTP set route, tests).
const std::vector<std::string>& SettingsKeyNames();
// Sets one setting by name from text. Returns false with `error` for an unknown key or bad value.
bool SetSettingByName(WaterSettings& settings, const std::string& key, const std::string& value, std::string& error);

} // namespace gte::water
