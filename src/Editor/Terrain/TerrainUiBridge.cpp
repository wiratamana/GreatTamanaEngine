#include "TerrainUiBridge.h"

#include <utility>

namespace gte {

namespace {

void Note(TerrainUiSnapshot& snapshot, const terrain::TerrainResult& result)
{
    if (!result.message.empty() || !result.ok) {
        snapshot.message = result.message;
        snapshot.messageIsError = !result.ok;
    }
}

} // namespace

void ApplyTerrainUiRequest(terrain::ITerrainWorld& world, TerrainUiBridge& bridge)
{
    TerrainUiSnapshot& snapshot = bridge.snapshot;

    // A queued save or load runs now: the panel showed the busy text during the last frame.
    if (bridge.pendingSave) {
        const std::string path = std::move(*bridge.pendingSave);
        bridge.pendingSave.reset();
        Note(snapshot, world.SaveAsset(path));
        snapshot.busy.clear();
    }
    if (bridge.pendingLoad) {
        const std::string path = std::move(*bridge.pendingLoad);
        bridge.pendingLoad.reset();
        Note(snapshot, world.LoadAsset(path));
        snapshot.busy.clear();
    }

    TerrainUiRequest request = std::move(bridge.request);
    bridge.request = TerrainUiRequest{};
    if (request.Empty()) {
        return;
    }
    if (request.close) {
        Note(snapshot, world.Close());
    }
    if (request.runtime) {
        Note(snapshot, world.SetRuntimeOptions(*request.runtime));
    }
    // Settings go in before Open, so a closed world opens with what the sliders show.
    if (request.settings) {
        Note(snapshot, world.SetSettings(*request.settings));
    }
    if (request.open) {
        Note(snapshot, world.Open(*request.open));
    }
    if (request.livePreview) {
        Note(snapshot, world.SetLivePreview(*request.livePreview));
    }
    if (request.debugAabb) {
        world.SetDebugAabb(*request.debugAabb);
    }
    if (request.cameraMode) {
        Note(snapshot, world.SetCameraMode(*request.cameraMode));
    }
    if (request.stopFlight) {
        world.StopFlight();
    }
    if (request.flight) {
        Note(snapshot, world.StartFlight(*request.flight));
    }
    if (request.hasCameraInput) {
        world.FeedCameraInput(request.cameraInput);
    }
    if (request.savePath) {
        bridge.pendingSave = std::move(request.savePath);
        snapshot.busy = "Baking and saving " + *bridge.pendingSave + " ...";
    }
    if (request.loadPath) {
        bridge.pendingLoad = std::move(request.loadPath);
        snapshot.busy = "Loading " + *bridge.pendingLoad + " ...";
    }
}

void PublishTerrainSnapshot(terrain::ITerrainWorld& world, TerrainUiBridge& bridge, const Mat4* gameViewProjection)
{
    TerrainUiSnapshot& snapshot = bridge.snapshot;
    snapshot.valid = true;
    snapshot.open = world.IsOpen();
    snapshot.stats = world.Stats();
    snapshot.settings = world.Settings();
    snapshot.runtime = world.RuntimeOptions();
    snapshot.cameraMode = world.GetCameraMode();
    snapshot.livePreview = world.LivePreview();
    snapshot.debugAabb = world.DebugAabb();
    snapshot.hasViewProjection = gameViewProjection != nullptr;
    if (gameViewProjection != nullptr) {
        snapshot.viewProjection = *gameViewProjection;
    }
    world.CollectNodes(snapshot.nodes); // Empty unless the overlay flag is on.
}

} // namespace gte
