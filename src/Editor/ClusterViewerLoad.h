#pragma once

// Decides what the cluster viewer shows after it loaded a mesh file. Pure: no ImGui, no GPU.

#include <cstddef>
#include <cstdint>
#include <string>

namespace gte {

// What the window learned while loading. Plain data, filled by the loader.
struct ClusterViewerLoadInput {
    bool pathSupported = true; // the device can run the cluster path
    bool decodeOk = false;
    std::string decodeError;
    std::size_t vertexCount = 0;
    std::size_t indexCount = 0;
    bool indicesValid = false;
    std::string clusterTailRejection; // why the file's cluster tail was dropped, empty when it was not
    std::size_t clusterCount = 0;
    std::size_t maxClusters = 0;
    bool tableValid = false;
    std::string tableWhy;
};

enum class ClusterViewerBanner : std::uint8_t {
    None,
    NoDeviceSupport,
    DecodeFailed,
    EmptyMesh,
    BadIndices,
    NoTable,
    DamagedTable,
    TooManyClusters,
};

struct ClusterViewerLoadDecision {
    bool showMesh = false;
    bool clusterModes = false;
    ClusterViewerBanner banner = ClusterViewerBanner::None;
    std::string text;              // one line for the banner, empty for None
    bool offerCreateCopy = false;  // the window offers "Create Clustered Copy"
};

// The first applicable banner wins. Normal mode works in every case except no device support,
// a failed decode, an empty mesh and bad indices.
ClusterViewerLoadDecision DecideClusterViewerLoad(const ClusterViewerLoadInput& input);

} // namespace gte
