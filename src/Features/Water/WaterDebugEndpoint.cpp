#include "WaterDebugEndpoint.h"

#include "WaterFeature.h"
#include "WaterMath.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <initializer_list>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace gte {

namespace {

using SettingMember = std::variant<bool WaterSettings::*, std::uint32_t WaterSettings::*, float WaterSettings::*,
    Vec3 WaterSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView, which is an enum and has its own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "enabled", &WaterSettings::enabled },
    SettingKey{ "seaLevel", &WaterSettings::seaLevel },
    SettingKey{ "waveCount", &WaterSettings::waveCount },
    SettingKey{ "waveHeight", &WaterSettings::waveHeight },
    SettingKey{ "choppiness", &WaterSettings::choppiness },
    SettingKey{ "waveSpeed", &WaterSettings::waveSpeed },
    SettingKey{ "windDirectionDeg", &WaterSettings::windDirectionDeg },
    SettingKey{ "windSpread", &WaterSettings::windSpread },
    SettingKey{ "baseWavelength", &WaterSettings::baseWavelength },
    SettingKey{ "rippleStrength", &WaterSettings::rippleStrength },
    SettingKey{ "rippleScale", &WaterSettings::rippleScale },
    SettingKey{ "waveLodScale", &WaterSettings::waveLodScale },
    SettingKey{ "shallowColor", &WaterSettings::shallowColor },
    SettingKey{ "deepColor", &WaterSettings::deepColor },
    SettingKey{ "foamColor", &WaterSettings::foamColor },
    SettingKey{ "clarity", &WaterSettings::clarity },
    SettingKey{ "absorption", &WaterSettings::absorption },
    SettingKey{ "refractionStrength", &WaterSettings::refractionStrength },
    SettingKey{ "roughness", &WaterSettings::roughness },
    SettingKey{ "sunSpecular", &WaterSettings::sunSpecular },
    SettingKey{ "reflectionStrength", &WaterSettings::reflectionStrength },
    SettingKey{ "shadowStrength", &WaterSettings::shadowStrength },
    SettingKey{ "crestFoam", &WaterSettings::crestFoam },
    SettingKey{ "shoreFoam", &WaterSettings::shoreFoam },
    SettingKey{ "shoreWidth", &WaterSettings::shoreWidth },
    SettingKey{ "ssr", &WaterSettings::ssr },
    SettingKey{ "ssrSteps", &WaterSettings::ssrSteps },
    SettingKey{ "ssrMaxDistance", &WaterSettings::ssrMaxDistance },
    SettingKey{ "wetShore", &WaterSettings::wetShore },
    SettingKey{ "wetBand", &WaterSettings::wetBand },
    SettingKey{ "wetStrength", &WaterSettings::wetStrength },
    SettingKey{ "wetFadeDistance", &WaterSettings::wetFadeDistance },
    SettingKey{ "maxDistance", &WaterSettings::maxDistance },
    SettingKey{ "halfRes", &WaterSettings::halfRes },
};

constexpr std::string_view kDebugViewKey = "debugView";

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

constexpr std::array<std::string_view, kWaterDebugViewCount> kDebugViewNames{ "Off", "Depth", "WaterDepth", "ShoreMask",
    "Foam", "Normal", "WaveHeight", "Reflection", "BodyId", "WetMask" };

std::string_view DebugViewName(WaterDebugView view) noexcept
{
    const auto index = static_cast<std::size_t>(view);
    return index < kDebugViewNames.size() ? kDebugViewNames[index] : kDebugViewNames[0];
}

std::optional<WaterDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (std::size_t i = 0; i < kDebugViewNames.size(); ++i) {
        if (text == kDebugViewNames[i]) {
            return static_cast<WaterDebugView>(i);
        }
    }
    return std::nullopt;
}

std::string_view StatusName(WaterFrameStatus status) noexcept
{
    switch (status) {
    case WaterFrameStatus::NotRendered: return "NotRendered";
    case WaterFrameStatus::Ok: return "Ok";
    case WaterFrameStatus::NoValidCamera: return "NoValidCamera";
    case WaterFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case WaterFrameStatus::TargetFailed: return "TargetFailed";
    case WaterFrameStatus::PassDisabled: return "PassDisabled";
    case WaterFrameStatus::Disabled: return "Disabled";
    case WaterFrameStatus::CameraUnderwater: return "CameraUnderwater";
    case WaterFrameStatus::NoBodies: return "NoBodies";
    }
    return "NotRendered";
}

// A float shown with 6 significant digits, so 0.85f reads 0.85 and not 0.8500000238.
double Shown(float value) noexcept
{
    char text[32];
    const auto written = std::to_chars(text, text + sizeof(text), value, std::chars_format::general, 6);
    double shown = value;
    if (written.ec == std::errc{}) {
        std::from_chars(text, written.ptr, shown);
    }
    return shown;
}

std::string Vec3Text(const Vec3& v)
{
    return std::to_string(Shown(v.x)) + "," + std::to_string(Shown(v.y)) + "," + std::to_string(Shown(v.z));
}

std::int64_t Count(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

void AddSettings(fx::FeatureDebugResult& out, const WaterSettings& s)
{
    for (const SettingKey& key : kSettingKeys) {
        std::visit(
            [&](auto member) {
                using Value = std::remove_cvref_t<decltype(s.*member)>;
                if constexpr (std::is_same_v<Value, bool>) {
                    Add(out, std::string(key.name), s.*member);
                } else if constexpr (std::is_same_v<Value, float>) {
                    Add(out, std::string(key.name), Shown(s.*member));
                } else if constexpr (std::is_same_v<Value, Vec3>) {
                    Add(out, std::string(key.name), Vec3Text(s.*member));
                } else {
                    Add(out, std::string(key.name), static_cast<std::int64_t>(s.*member));
                }
            },
            key.member);
    }
    Add(out, std::string(kDebugViewKey), std::string(DebugViewName(s.debugView)));
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const WaterViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };

    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("extent.w"), static_cast<std::int64_t>(v.extent.width));
    Add(out, key("extent.h"), static_cast<std::int64_t>(v.extent.height));
    Add(out, key("waveCount"), static_cast<std::int64_t>(v.waveCount));
    Add(out, key("bodyCount"), static_cast<std::int64_t>(v.bodyCount));
    Add(out, key("skyValid"), v.skyValid);
    Add(out, key("textureBytes"), Count(v.textureBytes));
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

std::optional<Vec3> ParseVec3(std::string_view text) noexcept
{
    std::array<float, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t comma = text.find(',');
        if ((i < 2) == (comma == std::string_view::npos)) {
            return std::nullopt;
        }
        const auto parsed = fx::ParseDebugFloat(text.substr(0, comma));
        if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
            return std::nullopt;
        }
        parts[i] = static_cast<float>(*parsed);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return Vec3(parts[0], parts[1], parts[2]);
}

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(WaterSettings& s, const SettingKey& key, std::string_view text)
{
    return std::visit(
        [&](auto member) {
            using Value = std::remove_cvref_t<decltype(s.*member)>;
            if constexpr (std::is_same_v<Value, bool>) {
                const auto parsed = fx::ParseDebugBool(text);
                if (parsed) {
                    s.*member = *parsed;
                }
                return parsed.has_value();
            } else if constexpr (std::is_same_v<Value, float>) {
                const auto parsed = fx::ParseDebugFloat(text);
                if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
                    return false;
                }
                s.*member = static_cast<float>(*parsed);
                return true;
            } else if constexpr (std::is_same_v<Value, Vec3>) {
                const auto parsed = ParseVec3(text);
                if (parsed) {
                    s.*member = *parsed;
                }
                return parsed.has_value();
            } else {
                const auto parsed = fx::ParseDebugUnsigned(text);
                if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                s.*member = static_cast<Value>(*parsed);
                return true;
            }
        },
        key.member);
}

const fx::FeatureDebugArg* FindArg(std::span<const fx::FeatureDebugArg> args, std::string_view key) noexcept
{
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == key) {
            return &arg;
        }
    }
    return nullptr;
}

// Reads an optional float argument; `fallback` when absent. False when present but not a number.
bool ReadFloat(std::span<const fx::FeatureDebugArg> args, std::string_view key, float fallback, float& out)
{
    out = fallback;
    const fx::FeatureDebugArg* arg = FindArg(args, key);
    if (arg == nullptr) {
        return true;
    }
    const auto parsed = fx::ParseDebugFloat(arg->value);
    if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
        return false;
    }
    out = static_cast<float>(*parsed);
    return true;
}

// "x,z[,level[,halfWidth]];x,z..." into points. False on any malformed entry.
bool ParsePoints(std::string_view text, float defaultLevel, float defaultHalfWidth, std::vector<WaterBodyPoint>& out)
{
    while (!text.empty()) {
        const std::size_t semi = text.find(';');
        std::string_view item = text.substr(0, semi);
        text = semi == std::string_view::npos ? std::string_view{} : text.substr(semi + 1);
        if (item.empty()) {
            continue;
        }
        std::array<float, 4> v{ 0.0f, 0.0f, defaultLevel, defaultHalfWidth };
        std::size_t count = 0;
        while (!item.empty() && count < 4) {
            const std::size_t comma = item.find(',');
            const auto parsed = fx::ParseDebugFloat(item.substr(0, comma));
            if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
                return false;
            }
            v[count++] = static_cast<float>(*parsed);
            item = comma == std::string_view::npos ? std::string_view{} : item.substr(comma + 1);
        }
        if (count < 2 || !item.empty()) {
            return false;
        }
        WaterBodyPoint p;
        p.xz = Vec2(v[0], v[1]);
        p.level = v[2];
        p.halfWidth = v[3];
        out.push_back(p);
    }
    return true;
}

std::string_view KindName(WaterBodyKind kind) noexcept
{
    switch (kind) {
    case WaterBodyKind::Ocean: return "ocean";
    case WaterBodyKind::LakeBox: return "box";
    case WaterBodyKind::LakePolygon: return "polygon";
    case WaterBodyKind::River: return "river";
    }
    return "ocean";
}

void AddBodies(fx::FeatureDebugResult& out, const std::vector<WaterBodyDesc>& bodies)
{
    Add(out, "bodies.local", static_cast<std::int64_t>(bodies.size()));
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const WaterBodyDesc& b = bodies[i];
        std::string text = std::string(KindName(b.kind)) + " level=" + std::to_string(Shown(b.level))
            + " waveScale=" + std::to_string(Shown(b.waveScale));
        if (b.kind == WaterBodyKind::LakeBox) {
            text += " box=" + std::to_string(Shown(b.boxMin.x)) + "," + std::to_string(Shown(b.boxMin.y)) + ";"
                + std::to_string(Shown(b.boxMax.x)) + "," + std::to_string(Shown(b.boxMax.y));
        } else if (b.kind == WaterBodyKind::River) {
            text += " flow=" + std::to_string(Shown(b.flowSpeed));
        }
        if (!b.points.empty()) {
            text += " points=" + std::to_string(b.points.size());
        }
        Add(out, "bodies." + std::to_string(i), std::move(text));
    }
}

} // namespace

fx::FeatureDebugResult WaterDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.Settings());
    AddBodies(out, m_feature.LocalBodies());
    Add(out, "profiler.rows", std::string("Water.Shade.Dispatch, Water.Composite.Draw: /profiler/table?view=flat&lane=gpu"));
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult WaterDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    WaterSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kDebugViewKey) {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be one of Off, Depth, WaterDepth, ShoreMask, Foam, Normal, "
                                     "WaveHeight, Reflection, BodyId, WetMask");
            }
            candidate.debugView = *view;
            continue;
        }
        const auto found = std::find_if(kSettingKeys.begin(), kSettingKeys.end(),
            [&](const SettingKey& key) { return key.name == arg.key; });
        if (found == kSettingKeys.end()) {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
        if (!AssignSetting(candidate, *found, arg.value)) {
            return Fail(arg.key, "value does not fit this key");
        }
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult WaterDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
{
    const auto unknown = [&](std::string_view key, std::string_view allowed) {
        return Fail(std::string(key), std::string("unknown argument; allowed: ").append(allowed));
    };
    const auto checkKeys = [&](std::initializer_list<std::string_view> allowed) -> const fx::FeatureDebugArg* {
        for (const fx::FeatureDebugArg& arg : args) {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(arg.key)) == allowed.end()) {
                return &arg;
            }
        }
        return nullptr;
    };

    if (action == "reset_defaults") {
        if (!args.empty()) {
            return Fail(args.front().key, "reset_defaults takes no arguments");
        }
        m_feature.ApplySettings(WaterSettings{});
        return GetState();
    }
    if (action == "clear_bodies" || action == "dump_bodies") {
        if (!args.empty()) {
            return Fail(args.front().key, std::string(action) + " takes no arguments");
        }
        if (action == "clear_bodies") {
            m_feature.ClearLocalBodies();
        }
        return GetState();
    }
    if (action == "probe") {
        if (const fx::FeatureDebugArg* bad = checkKeys({ "x", "z" })) {
            return unknown(bad->key, "x, z");
        }
        float x = 0.0f;
        float z = 0.0f;
        if (!ReadFloat(args, "x", 0.0f, x) || !ReadFloat(args, "z", 0.0f, z)) {
            return Fail("x", "probe needs numeric x and z (world metres)");
        }
        const WaterProbe probe = m_feature.Probe(Vec2(x, z));
        fx::FeatureDebugResult out;
        Add(out, "probe.inWater", probe.inWater);
        Add(out, "probe.body", static_cast<std::int64_t>(probe.body));
        Add(out, "probe.level", Shown(probe.level));
        Add(out, "probe.waveScale", Shown(probe.waveScale));
        Add(out, "probe.waveHeight", Shown(probe.waveHeight));
        Add(out, "probe.surfaceY", Shown(probe.surfaceY));
        return out;
    }
    if (action == "remove_body") {
        const fx::FeatureDebugArg* index = FindArg(args, "index");
        const auto parsed = index != nullptr ? fx::ParseDebugUnsigned(index->value) : std::nullopt;
        if (!parsed || args.size() != 1 || !m_feature.RemoveLocalBody(static_cast<std::size_t>(*parsed))) {
            return Fail("index", "remove_body needs index=<existing body number>");
        }
        return GetState();
    }

    WaterBodyDesc body;
    float waveScale = 1.0f;
    float level = 0.0f;
    if (action == "add_ocean") {
        if (const fx::FeatureDebugArg* bad = checkKeys({ "level", "waveScale" })) {
            return unknown(bad->key, "level, waveScale");
        }
        body.kind = WaterBodyKind::Ocean;
    } else if (action == "add_lake") {
        if (const fx::FeatureDebugArg* bad = checkKeys({ "level", "waveScale", "minX", "minZ", "maxX", "maxZ", "points" })) {
            return unknown(bad->key, "level, waveScale, minX, minZ, maxX, maxZ (box) or points=x,z;x,z;... (polygon)");
        }
        const fx::FeatureDebugArg* points = FindArg(args, "points");
        if (points != nullptr) {
            body.kind = WaterBodyKind::LakePolygon;
            if (!ParsePoints(points->value, 0.0f, 0.0f, body.points) || body.points.size() < 3
                || body.points.size() > kWaterMaxLakePoints) {
                return Fail("points", "polygon needs 3..32 points written x,z;x,z;x,z");
            }
        } else {
            body.kind = WaterBodyKind::LakeBox;
            float lo[2];
            float hi[2];
            if (!ReadFloat(args, "minX", -50.0f, lo[0]) || !ReadFloat(args, "minZ", -50.0f, lo[1])
                || !ReadFloat(args, "maxX", 50.0f, hi[0]) || !ReadFloat(args, "maxZ", 50.0f, hi[1])
                || !(hi[0] > lo[0]) || !(hi[1] > lo[1])) {
                return Fail("minX", "box needs numbers with max > min");
            }
            body.boxMin = Vec2(lo[0], lo[1]);
            body.boxMax = Vec2(hi[0], hi[1]);
        }
    } else if (action == "add_river") {
        if (const fx::FeatureDebugArg* bad = checkKeys({ "level", "waveScale", "points", "halfWidth", "flow", "spline", "samples" })) {
            return unknown(bad->key, "points=x,z[,level[,halfWidth]];..., level, halfWidth, flow, spline, samples, waveScale");
        }
        body.kind = WaterBodyKind::River;
        float halfWidth = 8.0f;
        float flow = 1.5f;
        float spline = 0.0f;
        float samples = 6.0f;
        float defaultLevel = 0.0f;
        if (!ReadFloat(args, "halfWidth", 8.0f, halfWidth) || !ReadFloat(args, "flow", 1.5f, flow)
            || !ReadFloat(args, "spline", 0.0f, spline) || !ReadFloat(args, "samples", 6.0f, samples)
            || !ReadFloat(args, "level", 0.0f, defaultLevel)) {
            return Fail("halfWidth", "river numbers must be finite");
        }
        const fx::FeatureDebugArg* points = FindArg(args, "points");
        std::vector<WaterBodyPoint> control;
        if (points == nullptr || !ParsePoints(points->value, defaultLevel, halfWidth, control) || control.size() < 2
            || control.size() > kWaterMaxRiverPoints) {
            return Fail("points", "river needs 2..64 points written x,z[,level[,halfWidth]];...");
        }
        body.points = spline > 0.5f ? WaterSampleSpline(control, static_cast<std::uint32_t>(std::clamp(samples, 1.0f, 32.0f)))
                                    : std::move(control);
        body.flowSpeed = flow;
        float sum = 0.0f;
        for (const WaterBodyPoint& p : body.points) {
            sum += p.level;
        }
        level = sum / static_cast<float>(body.points.size());
        body.level = level;
    } else {
        fx::FeatureDebugResult result;
        result.ok = false;
        result.message = "unknown action; supported: reset_defaults, add_ocean, add_lake, add_river, remove_body, "
                         "clear_bodies, dump_bodies, probe";
        return result;
    }

    if (!ReadFloat(args, "waveScale", 1.0f, waveScale) || !(waveScale >= 0.0f && waveScale <= 2.0f)) {
        return Fail("waveScale", "waveScale must be 0..2");
    }
    body.waveScale = waveScale;
    if (body.kind != WaterBodyKind::River && !ReadFloat(args, "level", 0.0f, body.level)) {
        return Fail("level", "level must be a number");
    }
    if (!m_feature.AddLocalBody(std::move(body))) {
        return Fail("", "body limit reached (16)");
    }
    return GetState();
}

} // namespace gte
