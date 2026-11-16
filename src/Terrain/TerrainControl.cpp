#include "TerrainControl.h"

#include <cmath>
#include <cstdio>

namespace gte::terrain {

namespace {

void AppendKey(std::string& out, const char* key)
{
    if (out.size() > 1) {
        out += ',';
    }
    out += '"';
    out += key;
    out += "\":";
}

void AddBool(std::string& out, const char* key, bool value)
{
    AppendKey(out, key);
    out += value ? "true" : "false";
}

void AddInt(std::string& out, const char* key, std::uint64_t value)
{
    AppendKey(out, key);
    out += std::to_string(value);
}

void AddInt(std::string& out, const char* key, int value)
{
    AppendKey(out, key);
    out += std::to_string(value);
}

void AddNumber(std::string& out, const char* key, double value)
{
    AppendKey(out, key);
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }
    char buffer[40];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    out.append(buffer, length > 0 ? static_cast<std::size_t>(length) : 0u);
}

void AddText(std::string& out, const char* key, const std::string& value)
{
    AppendKey(out, key);
    out += '"';
    for (const char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) >= 0x20) {
                out += c;
            }
        }
    }
    out += '"';
}

} // namespace

std::string TerrainStatsToJson(const TerrainStats& s)
{
    std::string out = "{";
    AddBool(out, "open", s.open);
    AddBool(out, "livePreview", s.livePreview);
    AddBool(out, "flightActive", s.flightActive);
    AddBool(out, "debugAabb", s.debugAabb);
    AddInt(out, "frame", s.frame);
    AddNumber(out, "cameraX", s.cameraX);
    AddNumber(out, "cameraY", s.cameraY);
    AddNumber(out, "cameraZ", s.cameraZ);

    AddInt(out, "nodesWanted", s.nodesWanted);
    AddInt(out, "nodesInView", s.nodesInView);
    AddInt(out, "nodesResident", s.nodesResident);
    AddInt(out, "nodesPending", s.nodesPending);
    AddInt(out, "uploadQueue", s.uploadQueue);
    AddInt(out, "nodesWrittenThisTick", s.nodesWrittenThisTick);
    AddInt(out, "builtTotal", s.builtTotal);
    AddInt(out, "evictedTotal", s.evictedTotal);
    AddNumber(out, "generationMsAverage", s.generationMsAverage);
    AddNumber(out, "generationMsMax", s.generationMsMax);
    AddNumber(out, "generatedPerSecond", s.generatedPerSecond);
    AddNumber(out, "lodDistanceScale", s.lodDistanceScale);

    AddInt(out, "slotsTotal", s.slotsTotal);
    AddInt(out, "slotsUsed", s.slotsUsed);
    AddInt(out, "slotsEnabled", s.slotsEnabled);
    AddInt(out, "trianglesEnabled", s.trianglesEnabled);
    AddNumber(out, "residentMbCpu", s.residentMbCpu);
    AddNumber(out, "residentMbGpu", s.residentMbGpu);
    AddNumber(out, "uploadMbThisTick", s.uploadMbThisTick);

    AddNumber(out, "tickCpuMs", s.tickCpuMs);
    AddNumber(out, "tickCpuMsMax", s.tickCpuMsMax);
    AddNumber(out, "selectMs", s.selectMs);
    AddNumber(out, "previewBuildMs", s.previewBuildMs);
    AddInt(out, "previewRebuilds", s.previewRebuilds);

    AddBool(out, "gpuCullValid", s.gpuCullValid);
    AddBool(out, "gpuPathActive", s.gpuPathActive);
    AddText(out, "gpuCullMode", s.gpuCullMode);
    AddText(out, "gpuOcclusion", s.gpuOcclusion);
    AddInt(out, "gpuTestedClusters", s.gpuTestedClusters);
    AddInt(out, "gpuDrawnNodes", s.gpuDrawnNodes);
    AddInt(out, "gpuFrustumCulledNodes", s.gpuFrustumCulledNodes);
    AddInt(out, "gpuHiZCulledNodes", s.gpuHiZCulledNodes);
    AddInt(out, "gpuTrianglesDrawn", s.gpuTrianglesDrawn);

    AddText(out, "cameraMode", s.cameraMode);
    AddNumber(out, "cameraYawDegrees", s.cameraYawDegrees);
    AddNumber(out, "cameraPitchDownDegrees", s.cameraPitchDownDegrees);
    AddNumber(out, "cameraSpeed", s.cameraSpeed);
    AddNumber(out, "cameraFovDegrees", s.cameraFovDegrees);
    AddNumber(out, "cameraAspect", s.cameraAspect);
    AddBool(out, "assetBusy", s.assetBusy);
    AddText(out, "assetKind", s.assetKind);
    AddNumber(out, "assetProgress", s.assetProgress);
    AddNumber(out, "assetElapsedMs", s.assetElapsedMs);
    AddText(out, "assetResult", s.assetResult);

    AddInt(out, "carveHash", s.carveHash);
    AddInt(out, "nodesRebuilding", s.nodesRebuilding);
    AddInt(out, "carveDirtyNodes", s.carveDirtyNodes);
    AddNumber(out, "carveRebuildMs", s.carveRebuildMs);
    AddBool(out, "oceanQuadVisible", s.oceanQuadVisible);
    AddInt(out, "seabedCulledNodes", static_cast<std::uint64_t>(s.seabedCulledNodes));

    AddText(out, "lastError", s.lastError);
    out += '}';
    return out;
}

const char* TerrainProfileName(TerrainProfile profile) noexcept
{
    switch (profile) {
    case TerrainProfile::Full: return "full";
    case TerrainProfile::Lean: return "lean";
    case TerrainProfile::GeometryOnly: return "geometry_only";
    }
    return "lean";
}

bool TryParseTerrainProfile(const std::string& name, TerrainProfile& out) noexcept
{
    for (const TerrainProfile candidate : { TerrainProfile::Full, TerrainProfile::Lean, TerrainProfile::GeometryOnly }) {
        if (name == TerrainProfileName(candidate)) {
            out = candidate;
            return true;
        }
    }
    return false;
}

} // namespace gte::terrain
