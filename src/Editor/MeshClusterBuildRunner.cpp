#include "MeshClusterBuildRunner.h"

#include "../Assets/AtomicFile.h"
#include "../Core/Logging.h"
#include "../Profiling/JobScopeTimer.h"

#include <exception>
#include <utility>

namespace gte {

// Owned until the finished build was polled, so the job never outlives its data.
struct MeshClusterBuildRunner::Payload {
    std::filesystem::path path;
    MeshClusterOptions options;
    std::atomic<bool> cancel{ false };
    std::uint64_t requestId = 0;
    MeshClusterBuildOutcome outcome;
};

namespace {

// Job body: no exception leaves it, a thrown error becomes a failed outcome.
void RunBuildJob(void* raw)
{
    auto& payload = *static_cast<MeshClusterBuildRunner::Payload*>(raw);
    GTE_PROFILE_JOB_SCOPE("MeshClusterBuild");
    try {
        payload.outcome = BuildClustersInGtaFile(payload.path, payload.options, &payload.cancel);
    } catch (const std::exception& e) {
        payload.outcome = MeshClusterBuildOutcome{};
        payload.outcome.message = std::string("cluster build failed: ") + e.what();
    } catch (...) {
        payload.outcome = MeshClusterBuildOutcome{};
        payload.outcome.message = "cluster build failed: unknown error";
    }
}

} // namespace


MeshClusterBuildRunner::MeshClusterBuildRunner() = default;

MeshClusterBuildRunner::~MeshClusterBuildRunner()
{
    if (m_payload) {
        m_payload->cancel.store(true, std::memory_order_relaxed);
        Jobs::JobSystem::Instance().WaitForJobs(m_handle);
    }
}

MeshClusterBuildRunner::StartResult MeshClusterBuildRunner::Start(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, std::uint64_t requestId)
{
    if (m_payload) {
        return StartResult::Busy;
    }

    m_payload = std::make_unique<Payload>();
    m_payload->path = gtaPath;
    m_payload->options = options;
    m_payload->requestId = requestId;
    m_activeKey = WriterRegistryKey(gtaPath);
    m_activeRequestId = requestId;
    m_handle = Jobs::JobHandle{};

    Jobs::JobSystem& jobs = Jobs::JobSystem::Instance();
    if (jobs.WorkerCount() < 2) {
        if (!m_inlineWarned) {
            m_inlineWarned = true;
            GTE_LOG_WARNING("MeshClustering", "Fewer than two job workers: the cluster build runs on the calling thread.");
        }
        RunBuildJob(m_payload.get());
    } else {
        jobs.Schedule(&RunBuildJob, m_payload.get(), m_handle);
    }
    return StartResult::Started;
}

std::optional<MeshClusterFinishedBuild> MeshClusterBuildRunner::Poll()
{
    if (!m_payload || !m_handle.IsComplete()) {
        return std::nullopt;
    }

    MeshClusterFinishedBuild finished;
    finished.path = std::move(m_payload->path);
    finished.requestId = m_payload->requestId;
    finished.outcome = std::move(m_payload->outcome);

    m_payload.reset();
    m_activeKey.clear();
    m_activeRequestId = 0;
    m_last = finished;
    return finished;
}

bool MeshClusterBuildRunner::IsBuildingPath(const std::filesystem::path& gtaPath) const
{
    return m_payload != nullptr && WriterRegistryKey(gtaPath) == m_activeKey;
}

} // namespace gte
