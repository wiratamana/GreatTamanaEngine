#include "ClusterViewerLoad.h"

namespace gte {

ClusterViewerLoadDecision DecideClusterViewerLoad(const ClusterViewerLoadInput& input)
{
    ClusterViewerLoadDecision out;

    if (!input.pathSupported) {
        out.banner = ClusterViewerBanner::NoDeviceSupport;
        out.text = "The cluster path is not supported on this device.";
        return out;
    }
    if (!input.decodeOk) {
        out.banner = ClusterViewerBanner::DecodeFailed;
        out.text = input.decodeError.empty() ? std::string("The mesh could not be decoded.") : input.decodeError;
        return out;
    }
    if (input.vertexCount == 0 || input.indexCount == 0) {
        out.banner = ClusterViewerBanner::EmptyMesh;
        out.text = "The mesh is empty. Nothing to show.";
        return out;
    }
    if (!input.indicesValid) {
        out.banner = ClusterViewerBanner::BadIndices;
        out.text = "The index list points outside the vertex array. The mesh is not shown.";
        return out;
    }

    out.showMesh = true;
    if (input.clusterCount == 0 && input.clusterTailRejection.empty()) {
        out.banner = ClusterViewerBanner::NoTable;
        out.text = "This mesh has no cluster table. Cluster modes are unavailable.";
        out.offerCreateCopy = true;
        return out;
    }
    if (!input.clusterTailRejection.empty() || !input.tableValid) {
        const std::string& why = !input.clusterTailRejection.empty() ? input.clusterTailRejection : input.tableWhy;
        out.banner = ClusterViewerBanner::DamagedTable;
        out.text = "The cluster table is damaged: " + (why.empty() ? std::string("unknown reason") : why)
            + ". Cluster modes are unavailable.";
        return out;
    }
    if (input.clusterCount > input.maxClusters) {
        out.banner = ClusterViewerBanner::TooManyClusters;
        out.text = "too many clusters for cluster modes (" + std::to_string(input.clusterCount) + " > "
            + std::to_string(input.maxClusters) + ")";
        return out;
    }

    out.clusterModes = true;
    return out;
}

} // namespace gte
