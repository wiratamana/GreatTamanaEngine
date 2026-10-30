#include "ProjectScanRunner.h"

#include "../Core/Logging.h"
#include "../Jobs/JobSystem.h"
#include "../Profiling/JobScopeTimer.h"

#include <atomic>
#include <exception>
#include <utility>

namespace gte {

// Owned until the finished job was polled, so the job never outlives its data.
struct ProjectScanRunner::Payload {
    ProjectScanRequest request;
    ProjectScanResult result; // Written only by the job.
    std::atomic<bool> cancel{ false };
};

namespace {

// Job body: no exception leaves it; an error becomes a failed result.
void RunScanJob(void* raw)
{
    auto& payload = *static_cast<ProjectScanRunner::Payload*>(raw);
    ProjectScanResult& result = payload.result;
    const auto cancelled = [&payload] { return payload.cancel.load(std::memory_order_relaxed); };
    try {
        {
            GTE_PROFILE_JOB_SCOPE("Project.EnsureRoot");
            result.rootExists = EnsureProjectRootExists(payload.request.rootPath);
        }
        if (result.rootExists && !cancelled()) {
            GTE_PROFILE_JOB_SCOPE("Project.ScanTree");
            result.tree = ScanProjectDirectory(payload.request.rootPath, &payload.cancel);
        }
        if (!payload.request.linkedFolderName.empty() && !cancelled()) {
            GTE_PROFILE_JOB_SCOPE("Project.LinkedFolder");
            result.linkedFileNames = ListFileNames(payload.request.linkedFolderPath);
            result.linkedFolderName = payload.request.linkedFolderName;
        }
    } catch (const std::exception& e) {
        result = ProjectScanResult{};
        result.failed = true;
        result.errorMessage = e.what();
    } catch (...) {
        result = ProjectScanResult{};
        result.failed = true;
        result.errorMessage = "unknown error";
    }
}

} // namespace

ProjectScanRunner::ProjectScanRunner() = default;

ProjectScanRunner::~ProjectScanRunner()
{
    if (m_payload) {
        m_payload->cancel.store(true, std::memory_order_relaxed);
        Jobs::JobSystem::Instance().WaitForJobs(m_handle);
    }
}

bool ProjectScanRunner::IsDue(Clock::time_point now) const noexcept
{
    return !m_payload && (m_dirty || (now - m_lastCompleted) >= kProjectRescanInterval);
}

bool ProjectScanRunner::Start(ProjectScanRequest request)
{
    if (m_payload) {
        return false;
    }
#if !GTE_ENABLE_JOB_SYSTEM
    if (!m_inlineWarned) {
        m_inlineWarned = true;
        GTE_LOG_WARNING("ProjectPanel", "Job system is off: the Project scan runs on the main thread.");
    }
#endif
    m_payload = std::make_unique<Payload>();
    m_payload->request = std::move(request);
    m_dirty = false;
    Jobs::JobSystem::Instance().Schedule(&RunScanJob, m_payload.get(), m_handle);
    return true;
}

std::optional<ProjectScanResult> ProjectScanRunner::Poll(Clock::time_point now)
{
    if (!m_payload || !m_handle.IsComplete()) {
        return std::nullopt;
    }
    ProjectScanResult result = std::move(m_payload->result);
    m_payload.reset();
    m_lastCompleted = now;
    return result;
}

} // namespace gte
