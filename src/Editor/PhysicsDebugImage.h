#pragma once
#include "../Physics/PhysicsDebugReport.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace gte {

// Orthographic view basis (left-handed). right = Cross(up, forward).
struct PhysicsDebugViewBasis {
    Float3 forward{};
    Float3 right{};
    Float3 up{};
};

// "negz", "posz", "posx", "negx" or "top". Returns false for any other name.
bool TryGetPhysicsDebugViewBasis(std::string_view viewName, PhysicsDebugViewBasis& out);

// CPU-rendered picture of a snapshot. No ImGui, no Vulkan.
struct PhysicsDebugImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4.
    PhysicsDebugViewBasis basis;
    Float3 boundsMin{};
    Float3 boundsMax{};
    float pixelsPerUnit = 0.0f;
};

// Projects and rasterizes the snapshot. Colors carry the meaning; there is no text.
PhysicsDebugImage RenderPhysicsDebugSnapshot(const PhysicsDebugSnapshot& snapshot);

} // namespace gte
