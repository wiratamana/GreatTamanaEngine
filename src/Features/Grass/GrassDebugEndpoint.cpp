#include "GrassDebugEndpoint.h"

#include "GrassFeature.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

namespace gte {

namespace {

using SettingMember = std::variant<bool GrassSettings::*, std::uint32_t GrassSettings::*, float GrassSettings::*>;

struct SettingKey {
    std::string_view name;
    SettingMember member;
};

// Every settable key except debugView and the two colours, which have their own handling.
constexpr std::array kSettingKeys{
    SettingKey{ "enabled", &GrassSettings::enabled },
    SettingKey{ "hizCulling", &GrassSettings::hizCulling },
    SettingKey{ "collectStats", &GrassSettings::collectStats },
    SettingKey{ "densityLod0", &GrassSettings::densityLod0 },
    SettingKey{ "densityLod1", &GrassSettings::densityLod1 },
    SettingKey{ "densityLod2", &GrassSettings::densityLod2 },
    SettingKey{ "patchiness", &GrassSettings::patchiness },
    SettingKey{ "patchScaleM", &GrassSettings::patchScaleM },
    SettingKey{ "bareThreshold", &GrassSettings::bareThreshold },
    SettingKey{ "bladeHeight", &GrassSettings::bladeHeight },
    SettingKey{ "bladeWidth", &GrassSettings::bladeWidth },
    SettingKey{ "bladeTaper", &GrassSettings::bladeTaper },
    SettingKey{ "heightVariation", &GrassSettings::heightVariation },
    SettingKey{ "lean", &GrassSettings::lean },
    SettingKey{ "maxBend", &GrassSettings::maxBend },
    SettingKey{ "widthCompensation", &GrassSettings::widthCompensation },
    SettingKey{ "maxWidthScale", &GrassSettings::maxWidthScale },
    SettingKey{ "lod0Segments", &GrassSettings::lod0Segments },
    SettingKey{ "maxSlopeDeg", &GrassSettings::maxSlopeDeg },
    SettingKey{ "slopeFadeDeg", &GrassSettings::slopeFadeDeg },
    SettingKey{ "waterMargin", &GrassSettings::waterMargin },
    SettingKey{ "waterFadeBand", &GrassSettings::waterFadeBand },
    SettingKey{ "sandHeight", &GrassSettings::sandHeight },
    SettingKey{ "alpineHeight", &GrassSettings::alpineHeight },
    SettingKey{ "alpineFade", &GrassSettings::alpineFade },
    SettingKey{ "lod0Distance", &GrassSettings::lod0Distance },
    SettingKey{ "lod1Distance", &GrassSettings::lod1Distance },
    SettingKey{ "lod2Distance", &GrassSettings::lod2Distance },
    SettingKey{ "edgeFade", &GrassSettings::edgeFade },
    SettingKey{ "densityBlend", &GrassSettings::densityBlend },
    SettingKey{ "farHeightFade", &GrassSettings::farHeightFade },
    SettingKey{ "windBend", &GrassSettings::windBend },
    SettingKey{ "flutterSpeed", &GrassSettings::flutterSpeed },
    SettingKey{ "flutterStrength", &GrassSettings::flutterStrength },
    SettingKey{ "gustStrength", &GrassSettings::gustStrength },
    SettingKey{ "windWaveLengthM", &GrassSettings::windWaveLengthM },
    SettingKey{ "windGustLengthM", &GrassSettings::windGustLengthM },
    SettingKey{ "windSpeedFallback", &GrassSettings::windSpeedFallback },
    SettingKey{ "windDirectionDegFallback", &GrassSettings::windDirectionDegFallback },
    SettingKey{ "followCloudWind", &GrassSettings::followCloudWind },
    SettingKey{ "translucency", &GrassSettings::translucency },
    SettingKey{ "rootAo", &GrassSettings::rootAo },
    SettingKey{ "tintVariation", &GrassSettings::tintVariation },
    SettingKey{ "terrainTint", &GrassSettings::terrainTint },
    SettingKey{ "farTerrainBlend", &GrassSettings::farTerrainBlend },
    SettingKey{ "groundNormalBlend", &GrassSettings::groundNormalBlend },
    SettingKey{ "selfShadow", &GrassSettings::selfShadow },
    SettingKey{ "sunVisibility", &GrassSettings::sunVisibility },
    SettingKey{ "receiveSunShadow", &GrassSettings::receiveSunShadow },
    SettingKey{ "receiveCloudShadow", &GrassSettings::receiveCloudShadow },
    SettingKey{ "shadowStrength", &GrassSettings::shadowStrength },
    SettingKey{ "cloudFill", &GrassSettings::cloudFill },
    SettingKey{ "castShadows", &GrassSettings::castShadows },
    SettingKey{ "shadowMaxTexelM", &GrassSettings::shadowMaxTexelM },
    SettingKey{ "shadowBiasTexels", &GrassSettings::shadowBiasTexels },
    SettingKey{ "shadowWidthScale", &GrassSettings::shadowWidthScale },
    SettingKey{ "shadowPadM", &GrassSettings::shadowPadM },
    SettingKey{ "gridCells", &GrassSettings::gridCells },
    SettingKey{ "heightTileTexels", &GrassSettings::heightTileTexels },
    SettingKey{ "heightTileTexelSize", &GrassSettings::heightTileTexelSize },
    SettingKey{ "instanceCapLod0", &GrassSettings::instanceCapLod0 },
    SettingKey{ "instanceCapLod1", &GrassSettings::instanceCapLod1 },
    SettingKey{ "instanceCapLod2", &GrassSettings::instanceCapLod2 },
    SettingKey{ "seed", &GrassSettings::seed },
};

constexpr std::string_view kPresetKey = "preset";
constexpr std::array<std::string_view, 4> kPresetNames{ "Lean", "Medium", "Big", "Huge" };
constexpr std::string_view kDebugViewKey = "debugView";
constexpr std::string_view kColorRootKey = "colorRoot";
constexpr std::string_view kColorTipKey = "colorTip";
constexpr std::array<std::string_view, 5> kDebugViewNames{ "Normal", "LodColor", "Sway", "ChunkBounds", "SunVis" };

struct ScopeName {
    rg::ViewScope scope;
    std::string_view prefix;
};
constexpr std::array kScopes{
    ScopeName{ rg::ViewScope::Primary, "game." },
    ScopeName{ rg::ViewScope::Secondary, "scene." },
};

std::string_view StatusName(GrassFrameStatus status) noexcept
{
    switch (status) {
    case GrassFrameStatus::NotRendered: return "NotRendered";
    case GrassFrameStatus::Ok: return "Ok";
    case GrassFrameStatus::Disabled: return "Disabled";
    case GrassFrameStatus::NoValidCamera: return "NoValidCamera";
    case GrassFrameStatus::NoTile: return "NoTile";
    case GrassFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case GrassFrameStatus::TargetFailed: return "TargetFailed";
    case GrassFrameStatus::PassDisabled: return "PassDisabled";
    }
    return "NotRendered";
}

constexpr std::array<std::string_view, static_cast<std::size_t>(GrassStatsWord::Count)> kStatNames{ "chunksInGrid",
    "chunksCulledDistance", "chunksCulledNoData", "chunksCulledWater", "chunksCulledFrustum", "chunksCulledHiz",
    "chunksLod0", "chunksLod1", "chunksLod2", "candidates", "accepted0", "accepted1", "accepted2", "rejectNoData",
    "rejectLodStep", "rejectWater", "rejectSlope", "rejectNoise", "dropped0", "dropped1", "dropped2", "slot0", "slot1",
    "slot2" };

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

std::int64_t Count(std::uint64_t value) noexcept
{
    return static_cast<std::int64_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::int64_t>::max()));
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

std::string Rgb(const float (&c)[3])
{
    return std::to_string(Shown(c[0])) + "," + std::to_string(Shown(c[1])) + "," + std::to_string(Shown(c[2]));
}

void AddSettings(fx::FeatureDebugResult& out, const GrassSettings& s)
{
    for (const SettingKey& key : kSettingKeys) {
        std::visit(
            [&](auto member) {
                using Value = std::remove_cvref_t<decltype(s.*member)>;
                if constexpr (std::is_same_v<Value, bool>) {
                    Add(out, std::string(key.name), s.*member);
                } else if constexpr (std::is_same_v<Value, float>) {
                    Add(out, std::string(key.name), Shown(s.*member));
                } else {
                    Add(out, std::string(key.name), static_cast<std::int64_t>(s.*member));
                }
            },
            key.member);
    }
    const auto view = std::min<std::size_t>(static_cast<std::size_t>(s.debugView), kDebugViewNames.size() - 1);
    Add(out, std::string(kDebugViewKey), std::string(kDebugViewNames[view]));
    Add(out, std::string(kColorRootKey), Rgb(s.colorRoot));
    Add(out, std::string(kColorTipKey), Rgb(s.colorTip));
}

void AddView(fx::FeatureDebugResult& out, std::string_view prefix, const GrassViewSnapshot& v)
{
    const auto key = [&](std::string_view name) { return std::string(prefix).append(name); };
    Add(out, key("status"), std::string(StatusName(v.status)));
    Add(out, key("frameIndex"), Count(v.frameIndex));
    Add(out, key("extent.w"), static_cast<std::int64_t>(v.extent.width));
    Add(out, key("extent.h"), static_cast<std::int64_t>(v.extent.height));
    Add(out, key("hizUsed"), v.hizUsed);
    Add(out, key("windShared"), v.windShared);
    Add(out, key("wind.dirX"), Shown(v.windDirX));
    Add(out, key("wind.dirZ"), Shown(v.windDirZ));
    Add(out, key("wind.speed"), Shown(v.windSpeed));
    Add(out, key("wind.offsetX"), Shown(v.windOffsetX));
    Add(out, key("wind.offsetZ"), Shown(v.windOffsetZ));
    Add(out, key("gridCells"), static_cast<std::int64_t>(v.gridCells));
    Add(out, key("tileRevision"), Count(v.tileRevision));
    Add(out, key("instanceBytes"), Count(v.instanceBytes));
    Add(out, key("shadow.cascades"), static_cast<std::int64_t>(v.shadowCascades));
    Add(out, key("shadow.cloud"), v.shadowCloud);
    Add(out, key("shadow.castCascades"), static_cast<std::int64_t>(v.shadowCastCascades));
    Add(out, key("shadow.castBlades"), Count(v.shadowCastBlades));
    Add(out, key("stats.valid"), v.statsValid);
    Add(out, key("stats.frame"), Count(v.statsFrame));
    if (v.statsValid) {
        for (std::size_t i = 0; i < kStatNames.size(); ++i) {
            Add(out, key(std::string("stats.") + std::string(kStatNames[i])), static_cast<std::int64_t>(v.stats.words[i]));
        }
        // What is drawn: the slot counter, capped by the region capacity (the cull never writes past it).
        const std::uint32_t drawn0 = v.stats.Get(GrassStatsWord::Slot0);
        const std::uint32_t drawn1 = v.stats.Get(GrassStatsWord::Slot1);
        const std::uint32_t drawn2 = v.stats.Get(GrassStatsWord::Slot2);
        Add(out, key("stats.requested0"), static_cast<std::int64_t>(drawn0));
        Add(out, key("stats.requested1"), static_cast<std::int64_t>(drawn1));
        Add(out, key("stats.requested2"), static_cast<std::int64_t>(drawn2));
    }
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

std::optional<std::array<float, 3>> ParseRgb(std::string_view text) noexcept
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
    return parts;
}

// Parses `text` into the member the key names. False when the text does not fit the member type.
bool AssignSetting(GrassSettings& s, const SettingKey& key, std::string_view text)
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

void AddTileSource(fx::FeatureDebugResult& out, const GrassTileSourceReport& r)
{
    constexpr std::array<std::string_view, static_cast<std::size_t>(GrassTileReason::Count)> kReasonNames{ "initial", "moved",
        "stream", "carve", "water", "settings", "terrain" };
    Add(out, "source.terrainOpen", r.terrainOpen);
    Add(out, "source.building", r.building);
    Add(out, "source.complete", r.complete);
    Add(out, "source.revision", Count(r.revision));
    Add(out, "source.size", static_cast<std::int64_t>(r.size));
    Add(out, "source.texelSize", Shown(r.texelSize));
    Add(out, "source.originX", r.originX);
    Add(out, "source.originZ", r.originZ);
    Add(out, "source.lastBuildMs", r.lastBuildMs);
    Add(out, "source.maxBuildMs", r.maxBuildMs);
    Add(out, "source.lastMainMs", r.lastMainMs);
    Add(out, "source.coverageLevel0", Shown(r.coverageLevel0));
    Add(out, "source.coverageNodes", Shown(r.coverageNodes));
    Add(out, "source.submergedShare", Shown(r.submergedShare));
    Add(out, "source.builds", Count(r.builds));
    Add(out, "source.skippedIncomplete", Count(r.skippedIncomplete));
    for (std::size_t i = 0; i < kReasonNames.size(); ++i) {
        Add(out, "source.reason." + std::string(kReasonNames[i]), Count(r.reasons[i]));
    }
}

void AddProbe(fx::FeatureDebugResult& out, const std::string& prefix, const GrassProbeReport& r)
{
    Add(out, prefix + "ok", r.ok);
    Add(out, prefix + "message", r.message);
    Add(out, prefix + "tileHeight", r.tileHeight);
    Add(out, prefix + "meshHeight", r.meshHeight);
    Add(out, prefix + "noiseHeight", r.noiseHeight);
    Add(out, prefix + "tileMinusMesh", r.tileHeight - r.meshHeight);
    Add(out, prefix + "nodeLevel", static_cast<std::int64_t>(r.nodeLevel));
    Add(out, prefix + "tileSubmerged", r.tileSubmerged);
    Add(out, prefix + "waterHere", r.waterHere);
    Add(out, prefix + "waterSurface", r.waterSurface);
}
} // namespace

fx::FeatureDebugResult GrassDebugEndpoint::GetState() const
{
    fx::FeatureDebugResult out;
    AddSettings(out, m_feature.UserSettings());
    const GrassSettings& effective = m_feature.Settings();
    Add(out, "effective.lod0Distance", Shown(effective.lod0Distance));
    Add(out, "effective.lod1Distance", Shown(effective.lod1Distance));
    Add(out, "effective.lod2Distance", Shown(effective.lod2Distance));
    Add(out, "effective.densityLod1", Shown(effective.densityLod1));
    Add(out, "effective.densityLod2", Shown(effective.densityLod2));
    Add(out, "effective.gridCells", static_cast<std::int64_t>(effective.gridCells));
    Add(out, "effective.heightTileTexels", static_cast<std::int64_t>(effective.heightTileTexels));
    Add(out, "effective.heightTileTexelSize", Shown(effective.heightTileTexelSize));
    Add(out, "instanceMiB", Shown(static_cast<float>(static_cast<double>(GrassInstanceBytes(effective)) / (1024.0 * 1024.0))));
    Add(out, "synthetic.enabled", m_feature.SyntheticTileEnabled());
    const std::shared_ptr<const GrassHeightTile> tile = m_feature.HeightTile();
    Add(out, "tile.present", tile != nullptr);
    if (tile != nullptr) {
        Add(out, "tile.size", static_cast<std::int64_t>(tile->size));
        Add(out, "tile.texelSize", Shown(tile->texelSize));
        Add(out, "tile.originX", tile->originX);
        Add(out, "tile.originZ", tile->originZ);
        Add(out, "tile.seaLevel", Shown(tile->seaLevel));
        Add(out, "tile.revision", Count(tile->revision));
    }
    if (const GrassTileReportFn& hook = m_feature.TileReportHook()) {
        AddTileSource(out, hook());
    }
    const Vec3 eye = m_feature.LastEye();
    Add(out, "eye", std::to_string(Shown(eye.x)) + "," + std::to_string(Shown(eye.y)) + "," + std::to_string(Shown(eye.z)));
    Add(out, "profiler.rows",
        std::string("Grass.Init, Grass.Cull, Grass.Stats.Copy, Grass.Shadow.C0..3.L0..1, Grass.Draw.L0..L2: /profiler/table?view=flat&lane=gpu"));
    for (const ScopeName& scope : kScopes) {
        AddView(out, scope.prefix, m_feature.Snapshot(scope.scope));
    }
    return out;
}

fx::FeatureDebugResult GrassDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    GrassSettings candidate = m_feature.UserSettings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == kPresetKey) {
            const auto found = std::find_if(kPresetNames.begin(), kPresetNames.end(),
                [&](std::string_view name) { return name == arg.value; });
            if (found == kPresetNames.end()) {
                return Fail(arg.key, "preset must be one of Lean, Medium, Big, Huge");
            }
            ApplyGrassPreset(candidate, static_cast<GrassPreset>(found - kPresetNames.begin()));
            continue;
        }
        if (arg.key == kDebugViewKey) {
            const auto found = std::find(kDebugViewNames.begin(), kDebugViewNames.end(), std::string_view(arg.value));
            if (found == kDebugViewNames.end()) {
                return Fail(arg.key, "debugView must be one of Normal, LodColor, Sway, ChunkBounds, SunVis");
            }
            candidate.debugView = static_cast<GrassDebugView>(found - kDebugViewNames.begin());
            continue;
        }
        if (arg.key == kColorRootKey || arg.key == kColorTipKey) {
            const auto rgb = ParseRgb(arg.value);
            if (!rgb) {
                return Fail(arg.key, "colour must be r,g,b");
            }
            float(&target)[3] = arg.key == kColorRootKey ? candidate.colorRoot : candidate.colorTip;
            for (std::size_t i = 0; i < 3; ++i) {
                target[i] = (*rgb)[i];
            }
            continue;
        }
        const auto found = std::find_if(
            kSettingKeys.begin(), kSettingKeys.end(), [&](const SettingKey& key) { return key.name == arg.key; });
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

fx::FeatureDebugResult GrassDebugEndpoint::Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args)
{
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
        m_feature.ApplySettings(GrassSettings{});
        return GetState();
    }
    if (action == "clear_tile") {
        if (!args.empty()) {
            return Fail(args.front().key, "clear_tile takes no arguments");
        }
        m_feature.DisableSyntheticTile();
        m_feature.SubmitHeightTile(nullptr);
        return GetState();
    }
    if (action == "synthetic_tile") {
        const std::initializer_list<std::string_view> kAllowed{ "kind", "baseHeight", "amplitude", "wavelength",
            "seaLevel", "lake", "lakeX", "lakeZ", "lakeRadius", "lakeDepth", "lakeLevel", "cliffX" };
        if (const fx::FeatureDebugArg* bad = checkKeys(kAllowed)) {
            return Fail(bad->key, "unknown argument; allowed: kind=flat|hills|cliff, baseHeight, amplitude, wavelength, "
                                  "seaLevel, lake=true|false, lakeX, lakeZ, lakeRadius, lakeDepth, lakeLevel, cliffX");
        }
        GrassSyntheticSpec spec;
        if (const fx::FeatureDebugArg* kind = FindArg(args, "kind")) {
            if (kind->value == "flat") {
                spec.kind = GrassSyntheticKind::Flat;
            } else if (kind->value == "hills") {
                spec.kind = GrassSyntheticKind::Hills;
            } else if (kind->value == "cliff") {
                spec.kind = GrassSyntheticKind::Cliff;
            } else {
                return Fail("kind", "kind must be flat, hills or cliff");
            }
        }
        if (!ReadFloat(args, "baseHeight", spec.baseHeight, spec.baseHeight) || !ReadFloat(args, "amplitude", spec.amplitude, spec.amplitude)
            || !ReadFloat(args, "wavelength", spec.wavelength, spec.wavelength) || !ReadFloat(args, "seaLevel", spec.seaLevel, spec.seaLevel)
            || !ReadFloat(args, "lakeX", spec.lakeX, spec.lakeX) || !ReadFloat(args, "lakeZ", spec.lakeZ, spec.lakeZ)
            || !ReadFloat(args, "lakeRadius", spec.lakeRadius, spec.lakeRadius) || !ReadFloat(args, "lakeDepth", spec.lakeDepth, spec.lakeDepth)
            || !ReadFloat(args, "lakeLevel", spec.lakeLevel, spec.lakeLevel) || !ReadFloat(args, "cliffX", spec.cliffX, spec.cliffX)) {
            return Fail("", "every numeric argument must be a finite number");
        }
        if (const fx::FeatureDebugArg* lake = FindArg(args, "lake")) {
            const auto parsed = fx::ParseDebugBool(lake->value);
            if (!parsed) {
                return Fail("lake", "lake must be true or false");
            }
            spec.lake = *parsed;
        }
        m_feature.EnableSyntheticTile(spec);
        return GetState();
    }
    if (action == "tile") {
        fx::FeatureDebugResult out;
        const GrassTileReportFn& hook = m_feature.TileReportHook();
        Add(out, "provider", hook != nullptr);
        if (hook) {
            AddTileSource(out, hook());
        }
        return out;
    }
    if (action == "probe" || action == "probe_batch") {
        const GrassProbeFn& hook = m_feature.ProbeHook();
        if (!hook) {
            return Fail("", "no tile provider is connected (open the terrain first)");
        }
        fx::FeatureDebugResult out;
        if (action == "probe") {
            float x = 0.0f;
            float z = 0.0f;
            if (!ReadFloat(args, "x", 0.0f, x) || !ReadFloat(args, "z", 0.0f, z) || FindArg(args, "x") == nullptr
                || FindArg(args, "z") == nullptr) {
                return Fail("x", "probe needs numeric x and z");
            }
            const GrassProbeReport r = hook(x, z);
            AddProbe(out, "", r);
            return out;
        }
        // Random points around the camera: error of the tile height against the drawn mesh.
        float count = 0.0f;
        float radius = 0.0f;
        float seed = 0.0f;
        ReadFloat(args, "n", 32.0f, count);
        ReadFloat(args, "radius", 60.0f, radius);
        ReadFloat(args, "seed", 1.0f, seed);
        const int n = std::clamp(static_cast<int>(count), 1, 500);
        const Vec3 eye = m_feature.LastEye();
        std::uint32_t state = static_cast<std::uint32_t>(seed) * 2654435761u + 12345u;
        const auto next01 = [&state]() {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>(state >> 8) / 16777216.0f;
        };
        std::vector<double> errors;
        std::vector<double> noiseErrors;
        int outside = 0;
        int water = 0;
        double worst = 0.0;
        double worstX = 0.0;
        double worstZ = 0.0;
        for (int i = 0; i < n; ++i) {
            const double x = static_cast<double>(eye.x) + (next01() * 2.0 - 1.0) * radius;
            const double z = static_cast<double>(eye.z) + (next01() * 2.0 - 1.0) * radius;
            const GrassProbeReport r = hook(x, z);
            if (!r.ok) {
                ++outside;
                continue;
            }
            if (r.tileSubmerged) {
                ++water;
            }
            const double err = std::abs(r.tileHeight - r.meshHeight);
            errors.push_back(err);
            noiseErrors.push_back(std::abs(r.noiseHeight - r.meshHeight));
            if (err > worst) {
                worst = err;
                worstX = x;
                worstZ = z;
            }
        }
        std::sort(errors.begin(), errors.end());
        std::sort(noiseErrors.begin(), noiseErrors.end());
        const auto pick = [](const std::vector<double>& v, double q) {
            return v.empty() ? 0.0 : v[std::min(v.size() - 1, static_cast<std::size_t>(q * static_cast<double>(v.size())))];
        };
        double mean = 0.0;
        for (const double e : errors) {
            mean += e;
        }
        mean = errors.empty() ? 0.0 : mean / static_cast<double>(errors.size());
        Add(out, "points", static_cast<std::int64_t>(errors.size()));
        Add(out, "outsideTile", static_cast<std::int64_t>(outside));
        Add(out, "submergedPoints", static_cast<std::int64_t>(water));
        Add(out, "tileVsMesh.meanM", mean);
        Add(out, "tileVsMesh.p50M", pick(errors, 0.5));
        Add(out, "tileVsMesh.p95M", pick(errors, 0.95));
        Add(out, "tileVsMesh.maxM", errors.empty() ? 0.0 : errors.back());
        Add(out, "tileVsMesh.worstAt", std::to_string(worstX) + "," + std::to_string(worstZ));
        Add(out, "noiseVsMesh.p50M", pick(noiseErrors, 0.5));
        Add(out, "noiseVsMesh.p95M", pick(noiseErrors, 0.95));
        Add(out, "noiseVsMesh.maxM", noiseErrors.empty() ? 0.0 : noiseErrors.back());
        return out;
    }
    fx::FeatureDebugResult result;
    result.ok = false;
    result.message = "unknown action; supported: reset_defaults, synthetic_tile, clear_tile, tile, probe, probe_batch";
    return result;
}

} // namespace gte
