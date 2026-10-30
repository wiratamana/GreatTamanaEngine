#pragma once

#include "ProjectPanelData.h"
#include "../Jobs/JobTypes.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gte {

inline constexpr std::chrono::milliseconds kProjectRescanInterval{ 500 };

// Plain input of one scan, filled on the main thread.
struct ProjectScanRequest {
    std::filesystem::path rootPath;
    std::string linkedFolderName;           // Empty = no linked folder.
    std::filesystem::path linkedFolderPath; // One-level listing, display only.
};

// Plain output of one scan, moved into the panel on the main thread.
struct ProjectScanResult {
    bool rootExists = false;
    bool failed = false;
    std::string errorMessage;
    std::vector<ProjectEntry> tree;
    std::string linkedFolderName;
    std::vector<std::string> linkedFileNames;
};

// Runs one Project folder scan at a time on the job system.
// Main thread only, except the job body, which calls ProjectPanelData free functions.
class ProjectScanRunner {
public:
    using Clock = std::chrono::steady_clock;

    struct Payload; // Opaque job data; named here so the job function in the .cpp can reach it.

    ProjectScanRunner();
    ~ProjectScanRunner();
    ProjectScanRunner(const ProjectScanRunner&) = delete;
    ProjectScanRunner& operator=(const ProjectScanRunner&) = delete;

    // Asks for a fresh scan; the mark stays set while a scan runs.
    void MarkDirty() noexcept { m_dirty = true; }

    // True when no scan runs and (dirty, or the interval passed since the last result).
    bool IsDue(Clock::time_point now) const noexcept;

    // Clears the dirty mark at start, so a mark set during the scan survives it.
    // Returns false while a scan runs.
    bool Start(ProjectScanRequest request);

    // Returns the finished result exactly once.
    std::optional<ProjectScanResult> Poll(Clock::time_point now);

private:
    std::unique_ptr<Payload> m_payload;
    Jobs::JobHandle m_handle; // Reused for every scan.
    Clock::time_point m_lastCompleted{};
    bool m_dirty = true;
#if !GTE_ENABLE_JOB_SYSTEM
    bool m_inlineWarned = false;
#endif
};

} // namespace gte
