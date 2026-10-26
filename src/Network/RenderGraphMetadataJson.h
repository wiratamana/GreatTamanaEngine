#pragma once

// JSON body of GET /render_graph. The "Game"/"Scene" view labels of the HTTP contract are mapped here.

#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <nlohmann/json.hpp>

namespace gte::Network {

// "Shared" / "Game" / "Scene" for a scope text produced by rg::ToString(ViewScope).
const char* RenderGraphViewScopeLabel(const std::string& viewScope);

// Snake_case keys, like the other HTTP bodies.
nlohmann::json RenderGraphMetadataToJson(const rg::RenderGraphMetadata& metadata);

} // namespace gte::Network
