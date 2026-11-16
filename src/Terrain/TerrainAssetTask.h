#pragma once

// Background bake+save or load of a terrain asset: the caller only polls, nothing blocks a frame.

#include "TerrainAsset.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace gte::terrain {

enum class AssetTaskKind : std::uint8_t { None, Save, Load };
enum class AssetTaskState : std::uint8_t { Idle, Running, Done, Failed, Cancelled };

struct AssetTaskStatus {
    AssetTaskKind kind = AssetTaskKind::None;
    AssetTaskState state = AssetTaskState::Idle;
    float progress = 0.0f;   // 0..1, bake progress for a save.
    std::uint32_t nodes = 0; // Nodes baked or loaded so far.
    std::uint32_t total = 0;
    double elapsedMs = 0.0;
    std::string path;
    std::string message;     // Result text once Done or Failed.
};

struct SaveRegion {
    std::int32_t firstX = 0;
    std::int32_t firstZ = 0;
    std::uint32_t countX = 1;
    std::uint32_t countZ = 1;
};

class TerrainAssetTask {
public:
    TerrainAssetTask() = default;
    ~TerrainAssetTask(); // Cancels and joins.
    TerrainAssetTask(const TerrainAssetTask&) = delete;
    TerrainAssetTask& operator=(const TerrainAssetTask&) = delete;

    // False while another task still runs. A finished task must be collected with Reset() or TakeLoaded().
    bool StartSave(const TerrainSettings& settings, const SaveRegion& region, std::filesystem::path path);
    bool StartLoad(std::filesystem::path path);
    void Cancel() noexcept { m_cancel.store(true, std::memory_order_relaxed); }

    AssetTaskStatus Status() const;
    bool Busy() const noexcept { return m_running.load(std::memory_order_acquire); }

    // Done load: moves the asset out once. Also resets the task to Idle.
    std::optional<BakedTerrain> TakeLoaded();
    // Joins a finished task and returns to Idle (no-op while running).
    void Reset();

private:
    void RunSave(TerrainSettings settings, SaveRegion region, std::filesystem::path path);
    void RunLoad(std::filesystem::path path);
    void Finish(AssetTaskState state, std::string message);

    mutable std::mutex m_mutex;
    AssetTaskStatus m_status;
    std::optional<BakedTerrain> m_loaded;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_cancel{ false };
    std::atomic<std::uint32_t> m_done{ 0 };
    std::atomic<std::uint32_t> m_total{ 0 };
    std::int64_t m_startNs = 0;
    std::jthread m_thread;
};

} // namespace gte::terrain
