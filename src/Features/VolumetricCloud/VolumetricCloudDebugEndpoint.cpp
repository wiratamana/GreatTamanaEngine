#include "VolumetricCloudDebugEndpoint.h"

#include "VolumetricCloudFeature.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace gte {

namespace {

constexpr std::array kDebugViews{
    std::pair{ VolumetricCloudDebugView::Off, std::string_view{ "Off" } },
    std::pair{ VolumetricCloudDebugView::ShapeSlice, std::string_view{ "ShapeSlice" } },
    std::pair{ VolumetricCloudDebugView::DetailSlice, std::string_view{ "DetailSlice" } },
    std::pair{ VolumetricCloudDebugView::WeatherMap, std::string_view{ "WeatherMap" } },
    std::pair{ VolumetricCloudDebugView::BlueNoise, std::string_view{ "BlueNoise" } },
};

std::string_view DebugViewName(VolumetricCloudDebugView view) noexcept
{
    for (const auto& [value, name] : kDebugViews) {
        if (value == view) {
            return name;
        }
    }
    return "Off";
}

std::optional<VolumetricCloudDebugView> ParseDebugView(std::string_view text) noexcept
{
    for (const auto& [value, name] : kDebugViews) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string_view StatusName(VolumetricCloudFrameStatus status) noexcept
{
    switch (status) {
    case VolumetricCloudFrameStatus::NotRendered: return "NotRendered";
    case VolumetricCloudFrameStatus::Off: return "Off";
    case VolumetricCloudFrameStatus::Ok: return "Ok";
    case VolumetricCloudFrameStatus::Disabled: return "Disabled";
    case VolumetricCloudFrameStatus::AssetMissing: return "AssetMissing";
    case VolumetricCloudFrameStatus::ShadersNotReady: return "ShadersNotReady";
    case VolumetricCloudFrameStatus::TargetFailed: return "TargetFailed";
    case VolumetricCloudFrameStatus::PassDisabled: return "PassDisabled";
    }
    return "NotRendered";
}

std::string SizeText(std::uint32_t w, std::uint32_t h, std::uint32_t d)
{
    if (w == 0) {
        return "not loaded";
    }
    return std::to_string(w) + "x" + std::to_string(h) + (d > 0 ? "x" + std::to_string(d) : "");
}

void Add(fx::FeatureDebugResult& out, std::string key, fx::FeatureDebugValue value)
{
    out.fields.push_back({ std::move(key), std::move(value) });
}

fx::FeatureDebugResult Fail(std::string key, std::string message)
{
    fx::FeatureDebugResult result;
    result.ok = false;
    result.badKey = std::move(key);
    result.message = std::move(message);
    return result;
}

} // namespace

fx::FeatureDebugResult VolumetricCloudDebugEndpoint::GetState() const
{
    const VolumetricCloudSettings& s = m_feature.Settings();
    fx::FeatureDebugResult out;
    Add(out, "enabled", s.enabled);
    Add(out, "debugView", std::string(DebugViewName(s.debugView)));
    Add(out, "slice", static_cast<double>(s.slice));
    Add(out, "channel", static_cast<std::int64_t>(s.channel));
    Add(out, "tiling", static_cast<double>(s.tiling));

    const fx::VolumeSize shape = m_feature.ShapeSize();
    const fx::VolumeSize detail = m_feature.DetailSize();
    const Extent2D weather = m_feature.WeatherSize();
    const Extent2D blue = m_feature.BlueNoiseSize();
    Add(out, "game.assets.shape", SizeText(shape.width, shape.height, shape.depth));
    Add(out, "game.assets.detail", SizeText(detail.width, detail.height, detail.depth));
    Add(out, "game.assets.weather", SizeText(weather.width, weather.height, 0));
    Add(out, "game.assets.blueNoise", SizeText(blue.width, blue.height, 0));
    Add(out, "game.status", std::string(StatusName(m_feature.Snapshot(rg::ViewScope::Primary).status)));
    Add(out, "scene.status", std::string(StatusName(m_feature.Snapshot(rg::ViewScope::Secondary).status)));
    return out;
}

fx::FeatureDebugResult VolumetricCloudDebugEndpoint::Set(std::span<const fx::FeatureDebugArg> args)
{
    VolumetricCloudSettings candidate = m_feature.Settings();
    for (const fx::FeatureDebugArg& arg : args) {
        if (arg.key == "enabled") {
            const auto parsed = fx::ParseDebugBool(arg.value);
            if (!parsed) {
                return Fail(arg.key, "value does not fit this key");
            }
            candidate.enabled = *parsed;
        } else if (arg.key == "debugView") {
            const auto view = ParseDebugView(arg.value);
            if (!view) {
                return Fail(arg.key, "debugView must be Off, ShapeSlice, DetailSlice, WeatherMap or BlueNoise");
            }
            candidate.debugView = *view;
        } else if (arg.key == "channel") {
            const auto parsed = fx::ParseDebugUnsigned(arg.value);
            if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
                return Fail(arg.key, "value does not fit this key");
            }
            candidate.channel = static_cast<std::uint32_t>(*parsed);
        } else if (arg.key == "slice" || arg.key == "tiling") {
            const auto parsed = fx::ParseDebugFloat(arg.value);
            if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max()) {
                return Fail(arg.key, "value does not fit this key");
            }
            (arg.key == "slice" ? candidate.slice : candidate.tiling) = static_cast<float>(*parsed);
        } else {
            const bool readOnly = arg.key.starts_with("game.") || arg.key.starts_with("scene.");
            return Fail(arg.key, readOnly ? "key is read-only" : "unknown key");
        }
    }
    m_feature.ApplySettings(candidate);
    return GetState();
}

fx::FeatureDebugResult VolumetricCloudDebugEndpoint::Invoke(
    std::string_view action, std::span<const fx::FeatureDebugArg> args)
{
    if (action != "reset_defaults") {
        fx::FeatureDebugResult result;
        result.ok = false;
        result.message = "unknown action; supported: reset_defaults";
        return result;
    }
    if (!args.empty()) {
        return Fail(args.front().key, "reset_defaults takes no arguments");
    }
    m_feature.ApplySettings(VolumetricCloudSettings{});
    return GetState();
}

} // namespace gte
