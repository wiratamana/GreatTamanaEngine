#include "TerrainCommands.h"

#include "../../Terrain/TerrainSettingsKeys.h"

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gte {

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

std::string JsonEscape(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

TerrainCommandResult Fail(int status, const std::string& message)
{
    TerrainCommandResult result;
    result.httpStatus = status;
    result.body = "{\"success\":false,\"error\":\"" + JsonEscape(message) + "\"}";
    return result;
}

TerrainCommandResult Done(const terrain::TerrainResult& outcome)
{
    if (!outcome.ok) {
        return Fail(409, outcome.message);
    }
    TerrainCommandResult result;
    result.body = "{\"success\":true,\"message\":\"" + JsonEscape(outcome.message) + "\"}";
    return result;
}

bool ParseNumber(const std::string& text, double& out)
{
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(out);
}

bool ParseFlag(const std::string& text, bool& out)
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

// Reads a numeric option; false (with `error` set) when the text is not a number.
bool NumberOption(const std::string& key, const std::string& value, double& out, std::string& error)
{
    if (ParseNumber(value, out)) {
        return true;
    }
    error = "bad number for " + key + ": " + value;
    return false;
}

// Applies keys that belong to the runtime options; returns true when the key was one of them.
bool ApplyRuntimeKey(terrain::TerrainRuntimeOptions& options, const std::string& key, const std::string& value, std::string& error, bool& bad)
{
    double number = 0.0;
    bool flag = false;
    if (key == "maxVisibleNodes" || key == "maxUploadsPerTick" || key == "streamingMaxInFlight" || key == "unloadDelayFrames"
        || key == "bakeNodesPerSide") {
        if (!NumberOption(key, value, number, error)) {
            bad = true;
            return true;
        }
        const int v = static_cast<int>(std::lround(number));
        if (key == "maxVisibleNodes") {
            options.maxVisibleNodes = v;
        } else if (key == "maxUploadsPerTick") {
            options.maxUploadsPerTick = v;
        } else if (key == "streamingMaxInFlight") {
            options.streamingMaxInFlight = v;
        } else if (key == "bakeNodesPerSide") {
            options.bakeNodesPerSide = v;
        } else {
            options.unloadDelayFrames = v;
        }
        return true;
    }
    if (key == "collectGpuStats") {
        if (!ParseFlag(value, flag)) {
            error = "bad bool for " + key + ": " + value;
            bad = true;
        } else {
            options.collectGpuStats = flag;
        }
        return true;
    }
    return false;
}

TerrainCommandResult StateCommand(terrain::ITerrainWorld& world)
{
    TerrainCommandResult result;
    const terrain::TerrainRuntimeOptions& runtime = world.RuntimeOptions();
    std::string body = terrain::TerrainStatsToJson(world.Stats());
    body.pop_back();
    body += ",\"runtime\":{\"maxVisibleNodes\":" + std::to_string(runtime.maxVisibleNodes)
        + ",\"maxUploadsPerTick\":" + std::to_string(runtime.maxUploadsPerTick)
        + ",\"streamingMaxInFlight\":" + std::to_string(runtime.streamingMaxInFlight)
        + ",\"unloadDelayFrames\":" + std::to_string(runtime.unloadDelayFrames)
        + ",\"bakeNodesPerSide\":" + std::to_string(runtime.bakeNodesPerSide)
        + ",\"collectGpuStats\":" + (runtime.collectGpuStats ? "true" : "false") + "}";
    body += ",\"settings\":" + terrain::SettingsToJson(world.Settings()) + "}";
    result.body = std::move(body);
    return result;
}

// One call applies every key: settings and runtime options together, preview and debug flags last.
TerrainCommandResult SetCommand(terrain::ITerrainWorld& world, const Pairs& params)
{
    terrain::TerrainSettings settings = world.Settings();
    terrain::TerrainRuntimeOptions runtime = world.RuntimeOptions();
    std::optional<bool> preview;
    std::optional<bool> debugAabb;
    std::optional<terrain::TerrainPreviewWindow> window;
    terrain::TerrainPreviewWindow nextWindow = world.PreviewWindow();
    bool settingsTouched = false;
    bool runtimeTouched = false;
    std::string error;
    for (const auto& [key, value] : params) {
        bool bad = false;
        if (ApplyRuntimeKey(runtime, key, value, error, bad)) {
            if (bad) {
                return Fail(400, error);
            }
            runtimeTouched = true;
            continue;
        }
        bool flag = false;
        double number = 0.0;
        if (key == "livePreview") {
            if (!ParseFlag(value, flag)) {
                return Fail(400, "bad bool for livePreview");
            }
            preview = flag;
        } else if (key == "debugAabb") {
            if (!ParseFlag(value, flag)) {
                return Fail(400, "bad bool for debugAabb");
            }
            debugAabb = flag;
        } else if (key == "previewCenterX" || key == "previewCenterZ" || key == "previewSpacing") {
            if (!NumberOption(key, value, number, error)) {
                return Fail(400, error);
            }
            nextWindow.autoCenter = false;
            (key == "previewCenterX" ? nextWindow.centerX : key == "previewCenterZ" ? nextWindow.centerZ : nextWindow.spacing) = number;
            window = nextWindow;
        } else if (terrain::SetSettingByName(settings, key, value, &error)) {
            settingsTouched = true;
        } else {
            return Fail(400, error);
        }
    }
    std::string message;
    if (runtimeTouched) {
        const terrain::TerrainResult outcome = world.SetRuntimeOptions(runtime);
        if (!outcome.ok) {
            return Fail(409, outcome.message);
        }
    }
    if (settingsTouched) {
        const terrain::TerrainResult outcome = world.SetSettings(settings);
        if (!outcome.ok) {
            return Fail(409, outcome.message);
        }
        message = outcome.message;
    }
    if (window.has_value()) {
        world.SetPreviewWindow(*window);
    }
    if (preview.has_value()) {
        const terrain::TerrainResult outcome = world.SetLivePreview(*preview);
        if (!outcome.ok) {
            return Fail(409, outcome.message);
        }
    }
    if (debugAabb.has_value()) {
        world.SetDebugAabb(*debugAabb);
    }
    TerrainCommandResult result;
    result.body = "{\"success\":true,\"message\":\"" + JsonEscape(message) + "\"}";
    return result;
}

TerrainCommandResult OpenCommand(terrain::ITerrainWorld& world, const Pairs& params)
{
    terrain::TerrainOpenOptions options;
    for (const auto& [key, value] : params) {
        double number = 0.0;
        bool flag = false;
        std::string error;
        if (key == "profile") {
            if (!terrain::TryParseTerrainProfile(value, options.profile)) {
                return Fail(400, "profile must be full, lean or geometry_only");
            }
        } else if (key == "poolSlots") {
            if (!NumberOption(key, value, number, error) || number < 0.0) {
                return Fail(400, "bad poolSlots");
            }
            options.poolSlots = static_cast<std::uint32_t>(number);
        } else if (key == "water") {
            if (!ParseFlag(value, flag)) {
                return Fail(400, "bad bool for water");
            }
            options.water = flag;
        } else if (key == "budget") {
            if (!ParseFlag(value, flag)) {
                return Fail(400, "bad bool for budget");
            }
            options.renderBudget = flag;
        } else {
            return Fail(400, "unknown open option: " + key);
        }
    }
    return Done(world.Open(options));
}

TerrainCommandResult FlightCommand(terrain::ITerrainWorld& world, const Pairs& params)
{
    terrain::FlightSpec spec;
    for (const auto& [key, value] : params) {
        std::string error;
        if (key == "kind") {
            if (value == "orbit") {
                spec.kind = terrain::FlightKind::Orbit;
            } else if (value == "line") {
                spec.kind = terrain::FlightKind::Line;
            } else {
                return Fail(400, "kind must be orbit or line");
            }
            continue;
        }
        double number = 0.0;
        if (!NumberOption(key, value, number, error)) {
            return Fail(400, error);
        }
        if (key == "x") {
            spec.centerX = number;
        } else if (key == "z") {
            spec.centerZ = number;
        } else if (key == "radius") {
            spec.radius = number;
        } else if (key == "heading") {
            spec.headingDegrees = number;
        } else if (key == "speed") {
            spec.speed = number;
        } else if (key == "alt") {
            spec.altitudeAboveGround = number;
        } else if (key == "minAlt") {
            spec.minAltitude = number;
        } else if (key == "lookDown") {
            spec.lookDownDegrees = number;
        } else if (key == "fixedStep") {
            spec.fixedStepSeconds = number;
        } else if (key == "duration") {
            spec.durationSeconds = number;
        } else {
            return Fail(400, "unknown flight option: " + key);
        }
    }
    return Done(world.StartFlight(spec));
}

TerrainCommandResult PathCommand(terrain::ITerrainWorld& world, const Pairs& params, bool save)
{
    std::string path;
    for (const auto& [key, value] : params) {
        if (key == "path") {
            path = value;
        } else {
            return Fail(400, "unknown option: " + key);
        }
    }
    if (path.empty()) {
        return Fail(400, "path is required");
    }
    return Done(save ? world.SaveAsset(path) : world.LoadAsset(path));
}

} // namespace

TerrainCommandResult ExecuteTerrainCommand(terrain::ITerrainWorld& world, const TerrainCommandRequest& request)
{
    const std::string& action = request.action;
    if (action == "state") {
        return StateCommand(world);
    }
    if (action == "settings") {
        TerrainCommandResult result;
        result.body = terrain::SettingsToJson(world.Settings());
        return result;
    }
    if (action == "set") {
        return SetCommand(world, request.params);
    }
    if (action == "open") {
        return OpenCommand(world, request.params);
    }
    if (action == "close") {
        return Done(world.Close());
    }
    if (action == "flight") {
        return FlightCommand(world, request.params);
    }
    if (action == "stop_flight") {
        world.StopFlight();
        return Done(terrain::TerrainResult{});
    }
    if (action == "save") {
        return PathCommand(world, request.params, true);
    }
    if (action == "load") {
        return PathCommand(world, request.params, false);
    }
    return Fail(404, "unknown terrain action: " + action);
}

} // namespace gte
