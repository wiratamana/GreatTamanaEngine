#pragma once

// JSON body of GET /render_graph.

#include "../Renderer/RenderGraph/RenderGraphMetadata.h"

#include <nlohmann/json.hpp>

namespace gte::Network {

// Snake_case keys, like the other HTTP bodies.
nlohmann::json RenderGraphMetadataToJson(const rg::RenderGraphMetadata& metadata);

} // namespace gte::Network
