#pragma once

// Status line of the Inspector cluster tool. Pure text logic, no ImGui.

#include "MeshClusterBuildRunner.h"

#include <filesystem>
#include <string>

namespace gte {

struct ClusterToolUiState;

enum class ClusterStatusTone { Quiet, Warn, Good, Bad };

struct ClusterStatus {
    ClusterStatusTone tone = ClusterStatusTone::Quiet;
    std::string text; // Empty when there is nothing to report.
    std::string note; // Optional second line, always a warning.
};

// Everything the status of one asset path depends on.
struct ClusterStatusInput {
    bool building = false;          // A job writes this path now.
    bool readingForClone = false;   // A clone job reads this path now.
    bool startRefusedBusy = false;  // The last click found another job running.
    const MeshClusterFinishedBuild* last = nullptr; // Last finished job of this path, if any.
};

// A finished clone shows the clone text on its source path, never the in-place text or its weld note.
ClusterStatus DescribeClusterStatus(const ClusterStatusInput& input);

// Reads the runner state for `path`; `runner` may be null.
ClusterStatusInput MakeClusterStatusInput(
    const ClusterToolUiState& ui, const MeshClusterBuildRunner* runner, const std::filesystem::path& path);

} // namespace gte
