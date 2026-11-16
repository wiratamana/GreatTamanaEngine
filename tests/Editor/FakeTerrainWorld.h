#pragma once

// Records every call; shared by the terrain command and UI bridge tests.

#include "Terrain/TerrainControl.h"

#include <string>
#include <vector>

namespace gte {

class FakeTerrainWorld final : public terrain::ITerrainWorld {
public:
    const terrain::TerrainSettings& Settings() const override { return settings; }
    terrain::TerrainResult SetSettings(const terrain::TerrainSettings& s) override
    {
        settings = s;
        ++settingsCalls;
        return {};
    }
    const terrain::TerrainRuntimeOptions& RuntimeOptions() const override { return runtime; }
    terrain::TerrainResult SetRuntimeOptions(const terrain::TerrainRuntimeOptions& o) override
    {
        runtime = o;
        return {};
    }
    bool IsOpen() const override { return open; }
    terrain::TerrainResult Open(const terrain::TerrainOpenOptions& o) override
    {
        open = true;
        lastOpen = o;
        order += "open;";
        return { true, "opened", terrain::SettingsChange::None };
    }
    terrain::TerrainResult Close() override
    {
        open = false;
        order += "close;";
        return { true, "closed", terrain::SettingsChange::None };
    }
    terrain::TerrainResult SetLivePreview(bool enabled) override
    {
        preview = enabled;
        return {};
    }
    bool LivePreview() const override { return preview; }
    terrain::TerrainResult SetPreviewWindow(const terrain::TerrainPreviewWindow& w) override
    {
        window = w;
        return {};
    }
    terrain::TerrainPreviewWindow PreviewWindow() const override { return window; }
    void SetDebugAabb(bool enabled) override { aabb = enabled; }
    bool DebugAabb() const override { return aabb; }
    void CollectNodes(std::vector<terrain::TerrainNodeInfo>& out) const override
    {
        out.clear();
        if (aabb) {
            out = nodes;
        }
    }
    terrain::TerrainStats Stats() const override { return stats; }
    terrain::TerrainResult SaveAsset(const std::filesystem::path& p) override
    {
        savedPath = p.string();
        order += "save;";
        return {};
    }
    terrain::TerrainResult LoadAsset(const std::filesystem::path&) override { return { false, "no such file", {} }; }
    terrain::TerrainResult StartFlight(const terrain::FlightSpec& s) override
    {
        flight = s;
        flying = true;
        return {};
    }
    void StopFlight() override { flying = false; }
    bool FlightActive() const override { return flying; }
    terrain::TerrainResult SetCameraMode(terrain::CameraMode mode) override
    {
        cameraMode = mode;
        return {};
    }
    terrain::CameraMode GetCameraMode() const override { return cameraMode; }
    void FeedCameraInput(const terrain::CameraInput& input) override
    {
        terrain::AccumulateInput(fed, input);
        ++feedCalls;
    }
    terrain::TerrainResult QueueCameraInput(const terrain::CameraInput& perFrame, int frames) override
    {
        queued = perFrame;
        queuedFrames = frames;
        return {};
    }
    terrain::TerrainResult SetCameraPose(const terrain::TerrainCameraPose& pose) override
    {
        lastPose = pose;
        return {};
    }

    terrain::TerrainSettings settings;
    terrain::TerrainRuntimeOptions runtime;
    terrain::TerrainOpenOptions lastOpen;
    terrain::TerrainPreviewWindow window;
    terrain::FlightSpec flight;
    terrain::CameraMode cameraMode = terrain::CameraMode::Script;
    terrain::CameraInput fed;
    terrain::CameraInput queued;
    terrain::TerrainCameraPose lastPose;
    terrain::TerrainStats stats;
    std::vector<terrain::TerrainNodeInfo> nodes;
    std::string savedPath;
    std::string order;
    int settingsCalls = 0;
    int feedCalls = 0;
    int queuedFrames = 0;
    bool open = false;
    bool preview = false;
    bool aabb = false;
    bool flying = false;
};

} // namespace gte
