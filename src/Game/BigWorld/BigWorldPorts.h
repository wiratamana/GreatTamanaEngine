#pragma once

// The three small interfaces the Big World demo drives. They use plain data only, so the Game layer does not
// include any feature header; an editor-side adapter implements them on top of the live worlds.
// Every call happens on the main thread.

#include "BigWorldPlan.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gte::bigworld {

struct PortResult {
    bool ok = true;
    std::string message;
    std::uint32_t id = 0; // Water body the call created.
};

struct TerrainOpenRequest {
    std::string profile = "lean";
    bool renderBudget = true;
    bool flatOceanQuad = true;
};

struct CameraPlacement {
    double x = 0.0;
    double z = 0.0;
    double heightAboveGround = 6.0;
    double headingDegrees = 0.0;
    double lookDownDegrees = 0.0;
    bool freeCamera = true;
};

struct TerrainFacts {
    bool available = false; // The terrain world exists in this editor.
    bool open = false;
    int nodesResident = 0;
    int nodesPending = 0;
    int uploadQueue = 0;
    int nodesRebuilding = 0; // Nodes still showing an old mesh after a carve change.
    std::string lastError;
};

class ITerrainPort {
public:
    virtual ~ITerrainPort() = default;
    virtual PortResult Open(const TerrainOpenRequest& request) = 0;
    virtual PortResult PlaceCamera(const CameraPlacement& placement) = 0;
    virtual PortResult Close() = 0;
    virtual TerrainFacts Facts() const = 0;
    // Ground height under a point in metres; empty when unknown.
    virtual std::optional<double> GroundHeight(double /*x*/, double /*z*/) const { return std::nullopt; }
};

struct LakeRequest {
    std::vector<PlanPoint> outline; // Closed outline, 3..32 corners.
    double bankWidth = 30.0;
};

struct RiverRequest {
    std::vector<PlanPoint> points;
    double width = 12.0;
};

struct WaterFacts {
    bool available = false; // The water world exists in this editor.
    bool featureConnected = false;
    int bodyCount = 0;
    bool hasOcean = false;
    double oceanLevel = 0.0;
    bool carvePending = false;
    int carveApplies = 0;
    std::string lastError;
};

class IWaterPort {
public:
    virtual ~IWaterPort() = default;
    // Levels come from the ground under the body.
    virtual PortResult AddLake(const LakeRequest& request) = 0;
    virtual PortResult AddRiver(const RiverRequest& request) = 0;
    virtual PortResult RemoveBody(std::uint32_t id) = 0;
    virtual WaterFacts Facts() const = 0;
};

class IFeaturePort {
public:
    virtual ~IFeaturePort() = default;
    virtual bool Has(std::string_view feature) const = 0;
    // Remembers the current value of `keys`; RestoreAll() sets them back.
    virtual PortResult Snapshot(std::string_view feature, std::span<const std::string> keys) = 0;
    virtual PortResult Set(std::string_view feature, std::span<const KeyValue> values) = 0;
    virtual PortResult RestoreAll() = 0;
    // One value per key, in order; empty when the feature or the key is unknown.
    virtual std::vector<std::optional<std::string>> Read(std::string_view feature, std::span<const std::string_view> keys) const = 0;
};

struct BigWorldPorts {
    ITerrainPort* terrain = nullptr;
    IWaterPort* water = nullptr;
    IFeaturePort* features = nullptr;
};

} // namespace gte::bigworld
