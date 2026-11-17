#include "TerrainSettingsKeys.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace gte::terrain {

namespace {

// One generic visitor per direction; the list of fields lives in VisitFields only.
template <class Visitor>
void VisitFields(TerrainSettings& s, const Visitor& v)
{
    v("seed", s.seed);
    v("heightScale", s.heightScale);
    v("baseFrequency", s.baseFrequency);
    v("domainWarpStrength", s.domainWarpStrength);
    v("domainWarpFrequency", s.domainWarpFrequency);
    v("octaves", s.octaves);
    v("gain", s.gain);
    v("lacunarity", s.lacunarity);
    v("continentalFrequencyScale", s.continentalFrequencyScale);
    v("continentalSeaLevel", s.continentalSeaLevel);
    v("continentalShaping", s.continentalShaping);
    v("peaksFrequencyScale", s.peaksFrequencyScale);
    v("peaksWeight", s.peaksWeight);
    v("detailFrequencyScale", s.detailFrequencyScale);
    v("detailAmplitude", s.detailAmplitude);
    v("detailOctaves", s.detailOctaves);
    v("erosionEnabled", s.erosionEnabled);
    v("erosionDropletCount", s.erosionDropletCount);
    v("erosionRate", s.erosionRate);
    v("depositionRate", s.depositionRate);
    v("sedimentCapacity", s.sedimentCapacity);
    v("erosionInertia", s.erosionInertia);
    v("erosionEvaporation", s.erosionEvaporation);
    v("erosionGravity", s.erosionGravity);
    v("erosionMaxLifetime", s.erosionMaxLifetime);
    v("erosionBrushRadius", s.erosionBrushRadius);
    v("erosionMaxLevel", s.erosionMaxLevel);
    v("erosionEdgeFalloff", s.erosionEdgeFalloff);
    v("erosionStrength", s.erosionStrength);
    v("grassMaxSlopeDeg", s.grassMaxSlopeDeg);
    v("rockMinSlopeDeg", s.rockMinSlopeDeg);
    v("snowMinHeightFraction", s.snowMinHeightFraction);
    v("snowSlopeLimitDeg", s.snowSlopeLimitDeg);
    v("sandMaxHeight", s.sandMaxHeight);
    v("chunkQuads", s.chunkQuads);
    v("lod0Spacing", s.lod0Spacing);
    v("lodLevelCount", s.lodLevelCount);
    v("lodDistanceFactor", s.lodDistanceFactor);
    v("worldHalfExtentChunks", s.worldHalfExtentChunks);
    v("frustumCulling", s.frustumCulling);
    v("hiZCulling", s.hiZCulling);
    v("maxViewDistance", s.maxViewDistance);
}

bool ParseDouble(std::string_view text, double& out)
{
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    out = std::strtod(copy.c_str(), &end);
    return end != copy.c_str() && *end == '\0' && errno == 0 && std::isfinite(out);
}

bool ParseBool(std::string_view text, bool& out)
{
    if (text == "true" || text == "1") {
        out = true;
        return true;
    }
    if (text == "false" || text == "0") {
        out = false;
        return true;
    }
    return false;
}

struct Setter {
    std::string_view name;
    std::string_view value;
    mutable bool matched = false;
    mutable bool valid = false;

    void operator()(const char* fieldName, float& field) const
    {
        if (fieldName != name) {
            return;
        }
        matched = true;
        double parsed = 0.0;
        if (ParseDouble(value, parsed)) {
            field = static_cast<float>(parsed);
            valid = true;
        }
    }
    void operator()(const char* fieldName, std::int32_t& field) const
    {
        if (fieldName != name) {
            return;
        }
        matched = true;
        double parsed = 0.0;
        if (ParseDouble(value, parsed)) {
            field = static_cast<std::int32_t>(std::llround(parsed));
            valid = true;
        }
    }
    void operator()(const char* fieldName, std::uint32_t& field) const
    {
        if (fieldName != name) {
            return;
        }
        matched = true;
        double parsed = 0.0;
        if (ParseDouble(value, parsed) && parsed >= 0.0) {
            field = static_cast<std::uint32_t>(std::llround(parsed));
            valid = true;
        }
    }
    void operator()(const char* fieldName, bool& field) const
    {
        if (fieldName != name) {
            return;
        }
        matched = true;
        valid = ParseBool(value, field);
    }
};

struct JsonWriter {
    std::string* out;

    void Key(const char* name) const
    {
        if (out->size() > 1) {
            *out += ',';
        }
        *out += '"';
        *out += name;
        *out += "\":";
    }
    void operator()(const char* name, float& v) const
    {
        Key(name);
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(v));
        *out += buffer;
    }
    void operator()(const char* name, std::int32_t& v) const
    {
        Key(name);
        *out += std::to_string(v);
    }
    void operator()(const char* name, std::uint32_t& v) const
    {
        Key(name);
        *out += std::to_string(v);
    }
    void operator()(const char* name, bool& v) const
    {
        Key(name);
        *out += v ? "true" : "false";
    }
};

struct NameCollector {
    std::vector<std::string_view>* names;
    template <class T>
    void operator()(const char* name, T&) const
    {
        names->push_back(name);
    }
};

} // namespace

bool SetSettingByName(TerrainSettings& settings, std::string_view name, std::string_view value, std::string* error)
{
    const Setter setter{ name, value };
    VisitFields(settings, setter);
    if (!setter.matched) {
        if (error != nullptr) {
            *error = "unknown setting: " + std::string(name);
        }
        return false;
    }
    if (!setter.valid) {
        if (error != nullptr) {
            *error = "bad value for " + std::string(name) + ": " + std::string(value);
        }
        return false;
    }
    return true;
}

std::string SettingsToJson(const TerrainSettings& settings)
{
    TerrainSettings copy = settings;
    std::string out = "{";
    VisitFields(copy, JsonWriter{ &out });
    out += '}';
    return out;
}

std::vector<std::string_view> SettingNames()
{
    TerrainSettings scratch;
    std::vector<std::string_view> names;
    VisitFields(scratch, NameCollector{ &names });
    return names;
}

} // namespace gte::terrain
