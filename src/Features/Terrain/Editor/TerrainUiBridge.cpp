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
    // Save and load run in the background: the call returns at once and the stats carry the progress.
    if (request.savePath) {
        Note(snapshot, world.SaveAsset(*request.savePath));
    }
    if (request.loadPath) {
        Note(snapshot, world.LoadAsset(*request.loadPath));
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
    snapshot.busy.clear();
    if (snapshot.stats.assetBusy) {
        snapshot.busy = std::string(snapshot.stats.assetKind == "load" ? "Loading" : "Baking and saving") + " ... "
            + std::to_string(static_cast<int>(snapshot.stats.assetProgress * 100.0)) + "%";
    } else if (!snapshot.stats.assetResult.empty() && snapshot.stats.assetResult != bridge.lastAssetResult) {
        // A finished background task: show its text once.
        bridge.lastAssetResult = snapshot.stats.assetResult;
        snapshot.message = bridge.lastAssetResult;
        snapshot.messageIsError = bridge.lastAssetResult.rfind("saved", 0) != 0 && bridge.lastAssetResult.rfind("loaded", 0) != 0;
    }
    snapshot.livePreview = world.LivePreview();
    snapshot.debugAabb = world.DebugAabb();
    snapshot.hasViewProjection = gameViewProjection != nullptr;
    if (gameViewProjection != nullptr) {
        snapshot.viewProjection = *gameViewProjection;
    }
    world.CollectNodes(snapshot.nodes); // Empty unless the overlay flag is on.
}

} // namespace gte
