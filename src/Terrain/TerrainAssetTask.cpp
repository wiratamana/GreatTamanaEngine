#include "TerrainAssetTask.h"

#include "../Jobs/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace gte::terrain {

namespace {

std::int64_t NowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

TerrainAssetTask::~TerrainAssetTask()
{
    Cancel();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool TerrainAssetTask::StartSave(const TerrainSettings& settings, const SaveRegion& region, std::filesystem::path path)
{
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }
    Reset();
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_status = AssetTaskStatus{};
        m_status.kind = AssetTaskKind::Save;
        m_status.state = AssetTaskState::Running;
        m_status.path = path.string();
        m_status.total = region.countX * region.countZ;
        m_loaded.reset();
    }
    m_cancel.store(false);
    m_done.store(0);
    m_total.store(region.countX * region.countZ);
    m_startNs = NowNs();
    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this, settings, region, path = std::move(path)]() mutable {
        RunSave(settings, region, std::move(path));
    });
    return true;
}

bool TerrainAssetTask::StartLoad(std::filesystem::path path)
{
    if (m_running.load(std::memory_order_acquire)) {
        return false;
    }
    Reset();
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_status = AssetTaskStatus{};
        m_status.kind = AssetTaskKind::Load;
        m_status.state = AssetTaskState::Running;
        m_status.path = path.string();
        m_loaded.reset();
    }
    m_cancel.store(false);
    m_done.store(0);
    m_total.store(0);
    m_startNs = NowNs();
    m_running.store(true, std::memory_order_release);
    m_thread = std::jthread([this, path = std::move(path)]() mutable { RunLoad(std::move(path)); });
    return true;
}

void TerrainAssetTask::Finish(AssetTaskState state, std::string message)
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_status.state = state;
        m_status.message = std::move(message);
        m_status.progress = state == AssetTaskState::Done ? 1.0f : m_status.progress;
        m_status.elapsedMs = static_cast<double>(NowNs() - m_startNs) * 1.0e-6;
    }
    m_running.store(false, std::memory_order_release);
}

void TerrainAssetTask::RunSave(TerrainSettings settings, SaveRegion region, std::filesystem::path path)
{
    try {
        // Half the workers: the main thread and the GPU driver need cores too.
        const std::size_t workers = std::max<std::size_t>(1, Jobs::JobSystem::Instance().WorkerCount() / 2);
        const BakeProgress progress = [this](std::uint32_t done, std::uint32_t) { m_done.store(done, std::memory_order_relaxed); };
        const std::optional<BakedTerrain> baked = BakeRegion(
            settings, region.firstX, region.firstZ, region.countX, region.countZ, progress, &m_cancel, nullptr, workers);
        if (!baked.has_value()) {
            Finish(m_cancel.load() ? AssetTaskState::Cancelled : AssetTaskState::Failed,
                m_cancel.load() ? "save cancelled" : "baking failed (bad region)");
            return;
        }
        std::string error;
        if (!SaveTerrainAsset(path, *baked, &error)) {
            Finish(AssetTaskState::Failed, error.empty() ? "saving failed" : error);
            return;
        }
        Finish(AssetTaskState::Done, "saved " + std::to_string(region.countX * region.countZ) + " nodes to " + path.string());
    } catch (const std::exception& e) {
        Finish(AssetTaskState::Failed, e.what());
    } catch (...) {
        Finish(AssetTaskState::Failed, "unknown error");
    }
}

void TerrainAssetTask::RunLoad(std::filesystem::path path)
{
    try {
        std::string error;
        std::optional<BakedTerrain> loaded = LoadTerrainAsset(path, &error);
        if (!loaded.has_value()) {
            Finish(AssetTaskState::Failed, error.empty() ? "loading failed" : error);
            return;
        }
        const std::uint32_t nodes = static_cast<std::uint32_t>(loaded->chunks.size());
        m_done.store(nodes);
        m_total.store(nodes);
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_loaded = std::move(loaded);
        }
        Finish(AssetTaskState::Done, "loaded " + path.string());
    } catch (const std::exception& e) {
        Finish(AssetTaskState::Failed, e.what());
    } catch (...) {
        Finish(AssetTaskState::Failed, "unknown error");
    }
}

AssetTaskStatus TerrainAssetTask::Status() const
{
    AssetTaskStatus out;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        out = m_status;
    }
    if (out.state == AssetTaskState::Running) {
        out.nodes = m_done.load(std::memory_order_relaxed);
        out.total = std::max(out.total, m_total.load(std::memory_order_relaxed));
        // The bake is about 90% of a save; the file write is the rest.
        const float bake = out.total > 0 ? static_cast<float>(out.nodes) / static_cast<float>(out.total) : 0.0f;
        out.progress = out.kind == AssetTaskKind::Save ? bake * 0.9f : 0.0f;
        out.elapsedMs = static_cast<double>(NowNs() - m_startNs) * 1.0e-6;
    }
    return out;
}

std::optional<BakedTerrain> TerrainAssetTask::TakeLoaded()
{
    if (m_running.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    std::optional<BakedTerrain> out;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        out = std::move(m_loaded);
        m_loaded.reset();
    }
    Reset();
    return out;
}

void TerrainAssetTask::Reset()
{
    if (m_running.load(std::memory_order_acquire)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_status.state != AssetTaskState::Running) {
        m_status.kind = AssetTaskKind::None;
        m_status.state = AssetTaskState::Idle;
    }
}

} // namespace gte::terrain
