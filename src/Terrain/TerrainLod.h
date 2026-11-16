#pragma once

// Quadtree LOD selection: which nodes the camera wants, nearest detail first. Pure CPU, no allocation per call
// once the output vectors have grown.

#include "TerrainSettings.h"
#include "TerrainTypes.h"

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gte::terrain {

// Six clip planes in camera-relative space: a point p is inside when dot(n, p) + d >= 0 for every plane.
struct FrustumPlanes {
    struct Plane {
        float nx = 0.0f;
        float ny = 0.0f;
        float nz = 0.0f;
        float d = 0.0f;
    };
    std::array<Plane, 6> planes{};
    bool valid = false;
};

// Planes of an eye-relative view-projection (camera at the origin), column-major, zero-to-one depth.
FrustumPlanes ExtractFrustumPlanes(const float* viewProjection16) noexcept;

// True when the camera-relative box lies fully outside one plane (conservative).
bool IsBoxOutside(const FrustumPlanes& frustum, const float* boxMin3, const float* boxMax3) noexcept;

struct LodView {
    double cameraX = 0.0;
    double cameraY = 0.0;
    double cameraZ = 0.0;
    FrustumPlanes frustum; // Ignored when invalid.
};

struct WantedNode {
    NodeKey key;
    double distance = 0.0; // Metres from the camera to the node box.
    bool inView = true;    // False: only inside the prefetch margin, load it but do not draw it.
};

struct LodSelectionStats {
    int visited = 0;
    int leaves = 0;
    int inView = 0;
    int rejectedDistance = 0;
    int rejectedFrustum = 0;
};

class LodSelector {
public:
    explicit LodSelector(const TerrainSettings& settings);

    // Resets the hysteresis memory.
    void SetSettings(const TerrainSettings& settings);

    // Remembers the real vertical extent of a built node so distance tests stay tight.
    void SetNodeHeightRange(const NodeKey& key, float minY, float maxY);
    void ForgetNodeHeightRange(const NodeKey& key);
    void ClearHeightRanges() { m_heightRanges.clear(); }

    // Replaces `out` with the wanted leaf nodes. The leaves never overlap.
    void Select(const LodView& view, std::vector<WantedNode>& out, LodSelectionStats* stats = nullptr);

private:
    struct HeightRange {
        float minY;
        float maxY;
    };
    struct StackEntry {
        NodeKey key;
        HeightRange range;
        bool measured; // True when `range` came from a built node (own or ancestor).
    };

    TerrainSettings m_settings;
    TerrainLayout m_layout;
    std::unordered_set<NodeKey, NodeKeyHash> m_split;     // Nodes split by the previous call.
    std::unordered_set<NodeKey, NodeKeyHash> m_splitNext;
    std::unordered_map<NodeKey, HeightRange, NodeKeyHash> m_heightRanges;
    std::vector<StackEntry> m_stack;
};

} // namespace gte::terrain
