#pragma once

// Name table of every TerrainSettings field, shared by the HTTP routes and the editor panel.

#include "TerrainSettings.h"

#include <string>
#include <string_view>
#include <vector>

namespace gte::terrain {

// Sets one field by its C++ name from text. Returns false and fills `error` for an unknown name or bad value.
bool SetSettingByName(TerrainSettings& settings, std::string_view name, std::string_view value, std::string* error);

// Every field as one flat JSON object (name -> number or bool).
std::string SettingsToJson(const TerrainSettings& settings);

// All field names in declaration order.
std::vector<std::string_view> SettingNames();

} // namespace gte::terrain
