#pragma once

// Pure edits of one water body (points, defaults) and ground-aware helpers (lake level, leaks, river heights).

#include "WaterTypes.h"

#include <functional>
#include <optional>
#include <string>

namespace gte::water {

// Ground height in metres at a world position.
using HeightFn = std::function<float(double x, double z)>;

WaterBody MakeBoxLake(const std::string& name, double centerX, double centerZ, double sizeX, double sizeZ, float level);
WaterBody MakeRiverBody(const std::string& name, std::vector<RiverPoint> points);
WaterBody MakeOceanBody(float level);

// Point edits keep the body valid (limits, minimum counts). They return false and set `error` when refused.
bool EditMovePoint(WaterBody& body, int index, double x, double z, std::optional<float> surfaceY, std::optional<float> width, std::string& error);
// `index` is where the new point goes; a negative index appends. A river point takes height and width from its neighbours.
bool EditInsertPoint(WaterBody& body, int index, double x, double z, std::string& error);
bool EditRemovePoint(WaterBody& body, int index, std::string& error);

struct LakeLeakReport {
    int samples = 0;
    int below = 0;         // Outline samples where the ground is lower than the level (the water would spill).
    float lowest = 0.0f;
    float highest = 0.0f;
};
LakeLeakReport CheckLakeLeaks(const WaterBody& lake, const HeightFn& ground);

// A low percentile of the ground along the outline: the level that keeps most of the outline dry.
float SuggestLakeLevel(const WaterBody& lake, const HeightFn& ground, float percentile = 0.1f);

// Puts every river point `freeboard` metres under the ground, then makes the surface never rise downstream.
void AutoRiverLevels(WaterBody& river, const HeightFn& ground, float freeboard);

} // namespace gte::water
