#include "ClusterStatusText.h"

#include "ClusterToolUiState.h"
#include "../Assets/AtomicFile.h"
#include "../Encoding/Utf8Path.h"

#include <cstdio>

namespace gte {

namespace {

const char* WeldText(const MeshClusterFinishedBuild& finished)
{
    if (finished.weldFellBack) {
        return "Position only (Exact was poor)";
    }
    return finished.weldUsed == ClusterWeldMode::PositionOnly ? "Position only" : "Exact";
}

// "Created <file>: N clusters, avg T tris, weld X, M ms".
std::string FormatCloneStatusLine(const MeshClusterFinishedBuild& finished)
{
    const MeshClusterReport& report = finished.outcome.report;
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%zu clusters, avg %.1f tris, weld %s, %.0f ms", report.clusterCount,
        static_cast<double>(report.avgTrianglesPerCluster), WeldText(finished), report.milliseconds);
    return "Created " + PathToUtf8(finished.createdPath.filename()) + ": " + buffer;
}

} // namespace

ClusterStatus DescribeClusterStatus(const ClusterStatusInput& input)
{
    ClusterStatus status;
    if (input.building) {
        status.text = "Building clusters...";
    } else if (input.readingForClone) {
        status.text = "Creating clustered copy...";
    } else if (input.startRefusedBusy) {
        status.tone = ClusterStatusTone::Warn;
        status.text = "Busy: another cluster build is running.";
    } else if (input.last != nullptr) {
        const MeshClusterFinishedBuild& finished = *input.last;
        const MeshClusterBuildOutcome& outcome = finished.outcome;
        const MeshClusterReport& report = outcome.report;
        if (finished.wasClone) {
            status.tone = outcome.success ? ClusterStatusTone::Good : ClusterStatusTone::Bad;
            status.text = outcome.success ? FormatCloneStatusLine(finished) : "Clone failed: " + outcome.message;
        } else if (outcome.success) {
            status.tone = ClusterStatusTone::Good;
            status.text = FormatClusterStatusLine(report.clusterCount, report.avgTrianglesPerCluster,
                report.vertexCountBefore, report.vertexCountAfter, report.milliseconds);
            if (report.poorFill) {
                status.note = report.weld == ClusterWeldMode::Exact ? "Low cluster fill. Try the Position only weld."
                                                                    : "Low cluster fill.";
            }
        } else {
            status.tone = ClusterStatusTone::Bad;
            status.text = "Cluster build failed: " + outcome.message;
        }
    }
    return status;
}

ClusterStatusInput MakeClusterStatusInput(
    const ClusterToolUiState& ui, const MeshClusterBuildRunner* runner, const std::filesystem::path& path)
{
    ClusterStatusInput input;
    input.startRefusedBusy = ui.startRefusedBusy;
    if (runner == nullptr) {
        return input;
    }
    input.building = runner->IsBuildingPath(path);
    input.readingForClone = runner->IsReadingPath(path);
    const std::optional<MeshClusterFinishedBuild>& last = runner->LastFinished();
    if (last.has_value() && WriterRegistryKey(last->path) == WriterRegistryKey(path)) {
        input.last = &*last;
    }
    return input;
}

} // namespace gte
