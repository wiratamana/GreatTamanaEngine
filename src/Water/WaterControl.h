#pragma once

// Control surface of the live water world. The HTTP commands and the editor panel talk only to this interface.
// Every call is made on the main thread.

#include "../Terrain/TerrainControl.h"
#include "WaterBodySet.h"
#include "WaterSettings.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gte::water {

struct WaterResult {
    bool ok = true;
    std::string message;
    std::uint32_t id = 0; // Body the call created or changed.
};

struct WaterStats {
    std::uint64_t revision = 0;        // Grows with every accepted edit.
    int bodyCount = 0;
    std::uint64_t bodiesHash = 0;
    std::uint64_t carveHash = 0;       // Of the carve the terrain was given last.
    bool carvePending = false;         // An edit waits for the debounce before the terrain rebuilds.
    int carveApplies = 0;              // Times a carve was handed to the terrain.
    int carveDirtyRects = 0;           // Rectangles of the last hand-over.
    bool featureConnected = false;     // The Water render feature accepted the last push.
    int featurePushes = 0;
    bool oceanQuadHidden = false;
    std::vector<std::string> issues;   // Bodies the sanitizer dropped or repaired.
    std::string lastError;
};

// Everything the probe route reports about one world position.
struct WaterProbeResult {
    bool terrainKnown = false;
    double baseHeight = 0.0;
    double carvedHeight = 0.0;
    WaterSample water;
    double depth = 0.0;                 // Water surface minus carved ground; positive = under water.
    std::optional<double> shoreDistance; // Metres to where the ground meets the surface, within the search range.
};

class IWaterWorld {
public:
    virtual ~IWaterWorld() = default;

    virtual const WaterSettings& Settings() const = 0;
    virtual WaterResult SetSettings(const WaterSettings& settings) = 0;

    virtual std::shared_ptr<const WaterBodySet> Bodies() const = 0;
    virtual WaterResult AddBody(const WaterBody& body) = 0;
    virtual WaterResult UpdateBody(const WaterBody& body) = 0; // Matched by id; the shape is replaced.
    virtual WaterResult RemoveBody(std::uint32_t id) = 0;
    virtual WaterResult ClearBodies() = 0;
    virtual WaterResult MovePoint(std::uint32_t id, int index, double x, double z, std::optional<float> surfaceY, std::optional<float> width) = 0;
    virtual WaterResult InsertPoint(std::uint32_t id, int index, double x, double z) = 0; // index < 0 appends.
    virtual WaterResult RemovePoint(std::uint32_t id, int index) = 0;

    // Hands a pending carve change to the terrain at once (otherwise it waits for the debounce).
    virtual void FlushCarve() = 0;
    // Seconds an edit must rest before the terrain is rebuilt.
    virtual void SetCarveDebounce(double seconds) = 0;
    virtual double CarveDebounce() const = 0;

    virtual WaterResult SaveFile(const std::filesystem::path& path) = 0;
    virtual WaterResult LoadFile(const std::filesystem::path& path) = 0;

    // Ground height from the live terrain (before and after the carve). Empty when the terrain is not open.
    virtual std::optional<terrain::TerrainHeightProbe> TerrainHeight(double x, double z) const = 0;
    virtual WaterProbeResult Probe(double x, double z) const = 0;
    virtual WaterStats Stats() const = 0;
};

} // namespace gte::water
