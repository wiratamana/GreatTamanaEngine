#pragma once

#include <cstdint>

namespace gte {

// Which Render Graph regime the panel currently displays. Shared by
// EditorContext, RenderGraphPanel, and the HTTP control route so a human
// toolbar click and a network command always agree on the same value.
enum class RenderGraphRegimeChoice : std::uint8_t { Offscreen, Present };

} // namespace gte
