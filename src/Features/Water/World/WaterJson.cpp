#include "WaterJson.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace gte::water {

namespace {

using Json = nlohmann::json;

// Calls v(name, field) for every setting, in a fixed order.
template <typename S, typename V>
void VisitSettings(S& s, V&& v)
{
    v("seaLevel", s.seaLevel);
    v("waveHeight", s.waveHeight);
    v("choppiness", s.choppiness);
    v("waveSpeed", s.waveSpeed);
    v("windDirectionDeg", s.windDirectionDeg);
    v("waveLength", s.waveLength);
    v("shallowColor", s.shallowColor);
    v("deepColor", s.deepColor);
    v("foamColor", s.foamColor);
    v("clarity", s.clarity);
    v("foamAmount", s.foamAmount);
    v("wetBand", s.wetBand);
    v("wetDarken", s.wetDarken);
    v("ssrEnabled", s.ssrEnabled);
    v("waveCount", s.waveCount);
    v("quality", s.quality);
    v("carveTerrain", s.carveTerrain);
    v("hideOceanQuad", s.hideOceanQuad);
    v("cullDeepSeabed", s.cullDeepSeabed);
    v("seabedCullDepth", s.seabedCullDepth);
    v("halfRes", s.halfRes);
}

Json ColorJson(const float (&c)[3]) { return Json::array({ c[0], c[1], c[2] }); }

bool ReadColor(const Json& j, float (&c)[3])
{
    if (!j.is_array() || j.size() != 3) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (!j[static_cast<std::size_t>(i)].is_number()) {
            return false;
        }
        c[i] = j[static_cast<std::size_t>(i)].get<float>();
    }
    return true;
}

Json SettingsObject(const WaterSettings& s)
{
    Json j = Json::object();
    VisitSettings(s, [&](const char* name, const auto& field) {
        using T = std::remove_cv_t<std::remove_reference_t<decltype(field)>>;
        if constexpr (std::is_same_v<T, WaterQuality>) {
            j[name] = WaterQualityName(field);
        } else if constexpr (std::is_array_v<T>) {
            j[name] = ColorJson(field);
        } else {
            j[name] = field;
        }
    });
    return j;
}

bool ApplySettings(const Json& j, WaterSettings& s, std::string& error)
{
    if (!j.is_object()) {
        error = "settings must be an object";
        return false;
    }
    bool ok = true;
    VisitSettings(s, [&](const char* name, auto& field) {
        const auto it = j.find(name);
        if (it == j.end() || !ok) {
            return;
        }
        using T = std::remove_cv_t<std::remove_reference_t<decltype(field)>>;
        if constexpr (std::is_same_v<T, WaterQuality>) {
            ok = it->is_string() && TryParseWaterQuality(it->get<std::string>(), field);
        } else if constexpr (std::is_array_v<T>) {
            ok = ReadColor(*it, field);
        } else if constexpr (std::is_same_v<T, bool>) {
            ok = it->is_boolean();
            if (ok) { field = it->get<bool>(); }
        } else {
            ok = it->is_number();
            if (ok) { field = it->get<T>(); }
        }
        if (!ok) {
            error = std::string("bad value for ") + name;
        }
    });
    return ok;
}

Json BodyObject(const WaterBody& b)
{
    Json j = Json::object();
    j["id"] = b.id;
    j["name"] = b.name;
    j["kind"] = WaterBodyKindName(b.kind);
    j["level"] = b.level;
    j["depth"] = b.depth;
    j["shoreWidth"] = b.shoreWidth;
    j["bankWidth"] = b.bankWidth;
    j["rimHeight"] = b.rimHeight;
    j["carveTerrain"] = b.carveTerrain;
    j["sealRim"] = b.sealRim;
    Json poly = Json::array();
    for (const Point2& p : b.polygon) {
        poly.push_back(Json::array({ p.x, p.z }));
    }
    j["polygon"] = poly;
    Json river = Json::array();
    for (const RiverPoint& p : b.river) {
        river.push_back(Json::array({ p.x, p.surfaceY, p.z, p.width }));
    }
    j["river"] = river;
    Json look = Json::object();
    look["overrideColors"] = b.look.overrideColors;
    look["shallowColor"] = ColorJson(b.look.shallowColor);
    look["deepColor"] = ColorJson(b.look.deepColor);
    look["foamColor"] = ColorJson(b.look.foamColor);
    look["waveScale"] = b.look.waveScale;
    j["look"] = look;
    return j;
}

template <typename T>
bool ReadNumber(const Json& j, const char* key, T& out)
{
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    if (!it->is_number()) {
        return false;
    }
    out = it->get<T>();
    return true;
}

bool ReadBool(const Json& j, const char* key, bool& out)
{
    const auto it = j.find(key);
    if (it == j.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool ApplyBody(const Json& j, WaterBody& b, std::string& error)
{
    if (!j.is_object()) {
        error = "body must be an object";
        return false;
    }
    bool ok = ReadNumber(j, "id", b.id) && ReadNumber(j, "level", b.level) && ReadNumber(j, "depth", b.depth)
        && ReadNumber(j, "shoreWidth", b.shoreWidth) && ReadNumber(j, "bankWidth", b.bankWidth)
        && ReadNumber(j, "rimHeight", b.rimHeight) && ReadBool(j, "carveTerrain", b.carveTerrain)
        && ReadBool(j, "sealRim", b.sealRim);
    if (!ok) {
        error = "bad number or flag in body";
        return false;
    }
    if (const auto it = j.find("name"); it != j.end() && it->is_string()) {
        b.name = it->get<std::string>();
    }
    if (const auto it = j.find("kind"); it != j.end()) {
        if (!it->is_string() || !TryParseWaterBodyKind(it->get<std::string>(), b.kind)) {
            error = "unknown body kind";
            return false;
        }
    }
    if (const auto it = j.find("polygon"); it != j.end()) {
        if (!it->is_array()) {
            error = "polygon must be an array";
            return false;
        }
        b.polygon.clear();
        for (const Json& p : *it) {
            if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
                error = "polygon corner must be [x, z]";
                return false;
            }
            b.polygon.push_back({ p[0].get<double>(), p[1].get<double>() });
        }
    }
    if (const auto it = j.find("river"); it != j.end()) {
        if (!it->is_array()) {
            error = "river must be an array";
            return false;
        }
        b.river.clear();
        for (const Json& p : *it) {
            if (!p.is_array() || p.size() != 4 || !p[0].is_number() || !p[1].is_number() || !p[2].is_number() || !p[3].is_number()) {
                error = "river point must be [x, y, z, width]";
                return false;
            }
            b.river.push_back({ p[0].get<double>(), p[2].get<double>(), p[1].get<float>(), p[3].get<float>() });
        }
    }
    if (const auto it = j.find("look"); it != j.end() && it->is_object()) {
        const Json& l = *it;
        if (!ReadBool(l, "overrideColors", b.look.overrideColors) || !ReadNumber(l, "waveScale", b.look.waveScale)) {
            error = "bad look";
            return false;
        }
        const std::pair<const char*, float*> colors[3] = { { "shallowColor", b.look.shallowColor },
            { "deepColor", b.look.deepColor }, { "foamColor", b.look.foamColor } };
        for (const auto& [key, dst] : colors) {
            if (const auto c = l.find(key); c != l.end()) {
                float tmp[3];
                if (!ReadColor(*c, tmp)) {
                    error = std::string("bad ") + key;
                    return false;
                }
                dst[0] = tmp[0]; dst[1] = tmp[1]; dst[2] = tmp[2];
            }
        }
    }
    return true;
}

bool ParseText(const std::string& text, Json& out, std::string& error)
{
    out = Json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = "malformed JSON";
        return false;
    }
    return true;
}

bool ParseNumberText(const std::string& v, double& out)
{
    char* end = nullptr;
    out = std::strtod(v.c_str(), &end);
    return end != v.c_str() && *end == '\0' && std::isfinite(out);
}

} // namespace

std::string SettingsToJson(const WaterSettings& s) { return SettingsObject(s).dump(); }

std::string WorldToJson(const WaterBodySet& set)
{
    Json j = Json::object();
    j["settings"] = SettingsObject(set.Settings());
    Json arr = Json::array();
    for (const WaterBody& b : set.Bodies()) {
        arr.push_back(BodyObject(b));
    }
    j["bodies"] = arr;
    return j.dump();
}

bool ParseSettingsJson(const std::string& text, WaterSettings& settings, std::string& error)
{
    Json j;
    return ParseText(text, j, error) && ApplySettings(j, settings, error);
}

bool ParseWorldJson(const std::string& text, WaterSettings& settings, std::vector<WaterBody>& bodies, std::string& error)
{
    Json j;
    if (!ParseText(text, j, error) || !j.is_object()) {
        error = error.empty() ? "world must be an object" : error;
        return false;
    }
    if (const auto it = j.find("settings"); it != j.end() && !ApplySettings(*it, settings, error)) {
        return false;
    }
    bodies.clear();
    if (const auto it = j.find("bodies"); it != j.end()) {
        if (!it->is_array()) {
            error = "bodies must be an array";
            return false;
        }
        for (const Json& jb : *it) {
            WaterBody b;
            if (!ApplyBody(jb, b, error)) {
                return false;
            }
            bodies.push_back(std::move(b));
        }
    }
    return true;
}

const std::vector<std::string>& SettingsKeyNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        WaterSettings s;
        VisitSettings(s, [&](const char* name, auto&) { out.emplace_back(name); });
        return out;
    }();
    return names;
}

bool SetSettingByName(WaterSettings& settings, const std::string& key, const std::string& value, std::string& error)
{
    bool found = false;
    bool ok = true;
    VisitSettings(settings, [&](const char* name, auto& field) {
        if (key != name) {
            return;
        }
        found = true;
        using T = std::remove_cv_t<std::remove_reference_t<decltype(field)>>;
        if constexpr (std::is_same_v<T, WaterQuality>) {
            ok = TryParseWaterQuality(value, field);
        } else if constexpr (std::is_array_v<T>) {
            std::stringstream ss(value);
            std::string part;
            float tmp[3] = {};
            int n = 0;
            while (std::getline(ss, part, ',') && n < 3) {
                double d = 0.0;
                if (!ParseNumberText(part, d)) {
                    ok = false;
                    return;
                }
                tmp[n++] = static_cast<float>(d);
            }
            ok = n == 3;
            if (ok) {
                field[0] = tmp[0]; field[1] = tmp[1]; field[2] = tmp[2];
            }
        } else if constexpr (std::is_same_v<T, bool>) {
            ok = value == "true" || value == "false" || value == "1" || value == "0";
            if (ok) { field = value == "true" || value == "1"; }
        } else {
            double d = 0.0;
            ok = ParseNumberText(value, d);
            if (ok) { field = static_cast<T>(d); }
        }
    });
    if (!found) {
        error = "unknown setting: " + key;
        return false;
    }
    if (!ok) {
        error = "bad value for " + key + ": " + value;
    }
    return ok;
}

} // namespace gte::water
