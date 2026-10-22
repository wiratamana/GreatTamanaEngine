#pragma once

#include "../Assets/MeshClusterPersistence.h"
#include "../Jobs/JobSystem.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gte {

// One finished cluster build, handed out once by MeshClusterBuildRunner::Poll().
struct MeshClusterFinishedBuild {
    std::filesystem::path path;
    std::uint64_t requestId = 0; // 0 = started from the UI.
    MeshClusterBuildOutcome outcome;
};

// Runs one cluster build at a time on the job system. Main thread only, except the job itself,
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

    // Returns the finished build exactly once, after the job completed.
    std::optional<MeshClusterFinishedBuild> Poll();

    bool Busy() const noexcept { return m_payload != nullptr; }

    // True from Start() until Poll() has returned the result of that build.
    bool IsBuildingPath(const std::filesystem::path& gtaPath) const;

    std::uint64_t ActiveRequestId() const noexcept { return m_activeRequestId; }

    // Last finished build, kept for the UI status line until the next one replaces it.
    const std::optional<MeshClusterFinishedBuild>& LastFinished() const noexcept { return m_last; }

private:
    std::unique_ptr<Payload> m_payload;
    Jobs::JobHandle m_handle;
    std::string m_activeKey;
    std::uint64_t m_activeRequestId = 0;
    std::optional<MeshClusterFinishedBuild> m_last;
    bool m_inlineWarned = false;
};

} // namespace gte
