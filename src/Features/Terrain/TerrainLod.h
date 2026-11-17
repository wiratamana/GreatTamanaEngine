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
    int passes = 1;                // Selection passes run (more than 1 when a cap lowered the detail).
    double distanceScale = 1.0;    // Factor multiplier used for the returned leaves.
    bool overBudget = false;       // Still above a cap at the lowest allowed detail.
};

// Caps on the number of leaves; the selector lowers lodDistanceFactor (never below 1.5) to stay under them.
struct LodBudget {
    int maxInView = 0; // 0 = no cap.
    int maxTotal = 0;  // In view plus prefetch; 0 = no cap.
};

class LodSelector {
public:
    explicit LodSelector(const TerrainSettings& settings);

    // Resets the hysteresis memory.
    void SetSettings(const TerrainSettings& settings);

    // New culling and distance values only: keeps the hysteresis memory and the measured node heights.
    void SetOptimization(const TerrainSettings& settings) { m_settings = settings; }

    void SetBudget(const LodBudget& budget) noexcept { m_budget = budget; }
    const LodBudget& Budget() const noexcept { return m_budget; }
    double DistanceScale() const noexcept { return m_distanceScale; }

    // Remembers the real vertical extent of a built node so distance tests stay tight.
    void SetNodeHeightRange(const NodeKey& key, float minY, float maxY);

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

    // One quadtree walk with `factor`; fills m_splitNext.
    void SelectPass(const LodView& view, double factor, std::vector<WantedNode>& out, LodSelectionStats& local);

    TerrainSettings m_settings;
    TerrainLayout m_layout;
    LodBudget m_budget;
    double m_distanceScale = 1.0;
    std::unordered_set<NodeKey, NodeKeyHash> m_split;     // Nodes split by the previous call.
    std::unordered_set<NodeKey, NodeKeyHash> m_splitNext;
    std::unordered_map<NodeKey, HeightRange, NodeKeyHash> m_heightRanges;
    std::vector<StackEntry> m_stack;
};

} // namespace gte::terrain
