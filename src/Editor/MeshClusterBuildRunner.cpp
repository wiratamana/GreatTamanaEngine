#include "MeshClusterBuildRunner.h"

#include "../Assets/AtomicFile.h"
#include "../Core/Logging.h"
#include "../Profiling/JobScopeTimer.h"

#include <exception>
#include <utility>

namespace gte {

// Owned until the finished job was polled, so the job never outlives its data.
struct MeshClusterBuildRunner::Payload {
    enum class Kind : std::uint8_t { InPlace, Clone };

    Kind kind = Kind::InPlace;
    std::filesystem::path path;        // The source asset.
    std::filesystem::path destination; // Clone only.
    MeshClusterCloneOptions options;   // InPlace uses options.cluster only.
    bool openViewerAfterClone = false;
    std::atomic<bool> cancel{ false };
    std::uint64_t requestId = 0;
    MeshClusterFinishedBuild result;   // Written by the job; path and request fields are set by Poll().
};

namespace {

// Job body: no exception leaves it, a thrown error becomes a failed outcome.
void RunBuildJob(void* raw)
{
    auto& payload = *static_cast<MeshClusterBuildRunner::Payload*>(raw);
    GTE_PROFILE_JOB_SCOPE("MeshClusterBuild");
    const bool clone = payload.kind == MeshClusterBuildRunner::Payload::Kind::Clone;
    try {
        MeshClusterFinishedBuild& result = payload.result;
        if (clone) {
            MeshClusterCloneOutcome made = CloneMeshAssetWithClusters(
                payload.path, payload.destination, payload.options, &payload.cancel);
            result.outcome.success = made.success;
            result.outcome.message = std::move(made.message);
            result.outcome.report = made.report;
            result.createdPath = std::move(made.createdPath);
            result.guid = made.guid;
            result.weldUsed = made.weldUsed;
            result.weldFellBack = made.weldFellBack;
        } else {
            result.outcome = BuildClustersInGtaFile(payload.path, payload.options.cluster, &payload.cancel);
        }
    } catch (const std::exception& e) {
        payload.result = MeshClusterFinishedBuild{};
        payload.result.outcome.message =
            std::string(clone ? "cluster clone failed: " : "cluster build failed: ") + e.what();
    } catch (...) {
        payload.result = MeshClusterFinishedBuild{};
        payload.result.outcome.message =
            clone ? "cluster clone failed: unknown error" : "cluster build failed: unknown error";
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

MeshClusterBuildRunner::StartResult MeshClusterBuildRunner::Launch(std::unique_ptr<Payload> payload, std::string activeKey)
{
    m_payload = std::move(payload);
    m_activeKey = std::move(activeKey);
    m_activeRequestId = m_payload->requestId;
    m_handle = Jobs::JobHandle{};

    Jobs::JobSystem& jobs = Jobs::JobSystem::Instance();
    if (jobs.WorkerCount() < 2) {
        if (!m_inlineWarned) {
            m_inlineWarned = true;
            GTE_LOG_WARNING("MeshClustering", "Fewer than two job workers: the cluster job runs on the calling thread.");
        }
        RunBuildJob(m_payload.get());
    } else {
        jobs.Schedule(&RunBuildJob, m_payload.get(), m_handle);
    }
    return StartResult::Started;
}

MeshClusterBuildRunner::StartResult MeshClusterBuildRunner::Start(
    const std::filesystem::path& gtaPath, const MeshClusterOptions& options, std::uint64_t requestId)
{
    if (m_payload) {
        return StartResult::Busy;
    }

    auto payload = std::make_unique<Payload>();
    payload->path = gtaPath;
    payload->options.cluster = options;
    payload->requestId = requestId;
    m_readKey.clear();
    return Launch(std::move(payload), WriterRegistryKey(gtaPath));
}

MeshClusterBuildRunner::StartResult MeshClusterBuildRunner::StartClone(const std::filesystem::path& source,
    const std::filesystem::path& destination, const MeshClusterCloneOptions& options, bool openViewerAfterClone,
    std::uint64_t requestId)
{
    if (m_payload) {
        return StartResult::Busy;
    }

    auto payload = std::make_unique<Payload>();
    payload->kind = Payload::Kind::Clone;
    payload->path = source;
    payload->destination = destination;
    payload->options = options;
    payload->openViewerAfterClone = openViewerAfterClone;
    payload->requestId = requestId;
    m_readKey = WriterRegistryKey(source);
    return Launch(std::move(payload), WriterRegistryKey(destination));
}

std::optional<MeshClusterFinishedBuild> MeshClusterBuildRunner::Poll()
{
    if (!m_payload || !m_handle.IsComplete()) {
        return std::nullopt;
    }

    MeshClusterFinishedBuild finished = std::move(m_payload->result);
    finished.path = std::move(m_payload->path);
    finished.requestId = m_payload->requestId;
    finished.wasClone = m_payload->kind == Payload::Kind::Clone;
    finished.openViewerAfterClone = finished.wasClone && m_payload->openViewerAfterClone;

    m_payload.reset();
    m_activeKey.clear();
    m_readKey.clear();
    m_activeRequestId = 0;
    m_last = finished;
    ++m_finishedCount;
    return finished;
}

bool MeshClusterBuildRunner::IsBuildingPath(const std::filesystem::path& gtaPath) const
{
    return m_payload != nullptr && WriterRegistryKey(gtaPath) == m_activeKey;
}

bool MeshClusterBuildRunner::IsReadingPath(const std::filesystem::path& gtaPath) const
{
    return m_payload != nullptr && !m_readKey.empty() && WriterRegistryKey(gtaPath) == m_readKey;
}

} // namespace gte
