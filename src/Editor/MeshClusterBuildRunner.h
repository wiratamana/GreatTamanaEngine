#pragma once

#include "../Assets/MeshClusterClone.h"
#include "../Assets/MeshClusterPersistence.h"
#include "../Jobs/JobSystem.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

// One finished cluster build or clone, handed out once by MeshClusterBuildRunner::Poll().
struct MeshClusterFinishedBuild {
    std::filesystem::path path; // The source asset.
    std::uint64_t requestId = 0; // 0 = started from the UI.
    MeshClusterBuildOutcome outcome;

    // Clone only: the new file and how it was made.
    bool wasClone = false;
    bool openViewerAfterClone = false;
    std::filesystem::path createdPath;
    Guid guid;
    ClusterWeldMode weldUsed = ClusterWeldMode::Exact;
    bool weldFellBack = false;
};

// Runs one cluster build or clone at a time on the job system. Main thread only, except the job itself,
// which calls free functions of src/Assets and touches nothing else.
class MeshClusterBuildRunner {
public:
    enum class StartResult : std::uint8_t { Started, Busy };

    // Opaque job data; named here only so the job function in the .cpp can reach it.
    struct Payload;

    MeshClusterBuildRunner();
    ~MeshClusterBuildRunner();

    MeshClusterBuildRunner(const MeshClusterBuildRunner&) = delete;
    MeshClusterBuildRunner& operator=(const MeshClusterBuildRunner&) = delete;

    // Runs inline when the pool has fewer than two workers (the build must not queue behind skinning).
    StartResult Start(const std::filesystem::path& gtaPath, const MeshClusterOptions& options, std::uint64_t requestId);

    // Writes a new clustered file at `destination`; the source is only read.
    StartResult StartClone(const std::filesystem::path& source, const std::filesystem::path& destination,
        const MeshClusterCloneOptions& options, bool openViewerAfterClone, std::uint64_t requestId);

    // Returns the finished job exactly once, after it completed.
    std::optional<MeshClusterFinishedBuild> Poll();

    bool Busy() const noexcept { return m_payload != nullptr; }

    // True from Start() until Poll() has returned the result. For a clone this is the DESTINATION file.
    bool IsBuildingPath(const std::filesystem::path& gtaPath) const;

    // True while a running clone reads `gtaPath` as its source.
    bool IsReadingPath(const std::filesystem::path& gtaPath) const;

    std::uint64_t ActiveRequestId() const noexcept { return m_activeRequestId; }

    // Last finished job, kept for the UI status line until the next one replaces it.
    const std::optional<MeshClusterFinishedBuild>& LastFinished() const noexcept { return m_last; }

private:
    StartResult Launch(std::unique_ptr<Payload> payload, std::string activeKey);

    std::unique_ptr<Payload> m_payload;
    Jobs::JobHandle m_handle;
    std::string m_activeKey;
    std::string m_readKey;
    std::uint64_t m_activeRequestId = 0;
    std::optional<MeshClusterFinishedBuild> m_last;
    bool m_inlineWarned = false;
};

} // namespace gte
