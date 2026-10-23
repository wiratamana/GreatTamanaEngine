#include "NetworkRoutes.h"

// editor-core-separation-2 campaign, PHASE3
// (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - this file no
// longer #includes Editor/Logger.h. ParseGetLogsQuery() below now clamps
// its `limit` field against `kLogCapacity` (Core/Logging.h, already
// #included via NetworkRoutes.h) instead of `Logger::kCapacity` - same
// value (2000), just relocated to a gte_core-owned home so this
// gte_core-tier file never needs the gte_editor-only Logger CLASS just for
// one compile-time constant (closes Defect C).

#include "Core/Plugins/ProjectAssemblyNameValidation.h"
#include "../Core/Plugins/HotReloadEngineStateMutex.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace gte::Network {

std::string HandleHelloWorld()
{
    return "hello world";
}

CaptureResponseFormat ResolveCaptureResponseFormat(const std::string& queryFormat, const std::string& acceptHeader)
{
    if (queryFormat == "png") {
        return CaptureResponseFormat::RawPng;
    }
    if (queryFormat == "base64" || queryFormat == "json") {
        return CaptureResponseFormat::JsonBase64;
    }
    if (!queryFormat.empty()) {
        // Unrecognized ?format= value - not an error, falls back to the
        // default (see this function's own doc comment in NetworkRoutes.h).
        return CaptureResponseFormat::RawPng;
    }
    if (acceptHeader.find("application/json") != std::string::npos) {
        return CaptureResponseFormat::JsonBase64;
    }
    return CaptureResponseFormat::RawPng;
}

std::string BuildCaptureJsonBody(int width, int height, const std::string& base64Png)
{
    std::string body;
    body.reserve(base64Png.size() + 64);
    body += "{\"width\":";
    body += std::to_string(width);
    body += ",\"height\":";
    body += std::to_string(height);
    body += ",\"format\":\"png\",\"data_base64\":\"";
    body += base64Png;
    body += "\"}";
    return body;
}

// --- network-impl-3 campaign
// (PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md) - see NetworkRoutes.h's own
// doc comments above each declaration for the exact, locked validation rules
// implemented below.

namespace {

// Non-throwing parse: a malformed body becomes a `.is_discarded()` result,
// never a thrown exception - verified against the actually-vendored
// nlohmann/json single header (parse(text, callback, allow_exceptions)).
nlohmann::json ParseJsonNoThrow(const std::string& jsonBody)
{
    return nlohmann::json::parse(jsonBody, /*callback*/ nullptr, /*allow_exceptions*/ false);
}

// network-impl-5 campaign
// (PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md) - shared
// helper behind ParseSetEntityTrsRequest()'s "translation"/
// "rotation_euler_degrees"/"scale" fields AND
// ParseInstantiateLightRequest()'s own "rotation_euler_degrees" field - all
// four follow the EXACT same all-or-nothing x/y/z rule (see
// NetworkRoutes.h's own doc comments): absent entirely, or explicitly JSON
// null, leaves outHasValue false with no error; present-and-non-null must be
// a JSON object with all three of "x"/"y"/"z" present as JSON numbers, or
// this fails with "<fieldName> must be an object with numeric x, y, and z
// fields". Returns false (and sets outErrorMessage) only on that failure -
// callers should immediately `return result;` when this returns false.
bool TryParseAllOrNothingXyz(const nlohmann::json& parent, const std::string& fieldName, bool& outHasValue,
    float& outX, float& outY, float& outZ, std::string& outErrorMessage)
{
    outHasValue = false;
    if (!parent.contains(fieldName) || parent[fieldName].is_null()) {
        return true;
    }

    const nlohmann::json& value = parent[fieldName];
    if (!value.is_object() || !value.contains("x") || !value.contains("y") || !value.contains("z") ||
        !value["x"].is_number() || !value["y"].is_number() || !value["z"].is_number()) {
        outErrorMessage = fieldName + " must be an object with numeric x, y, and z fields";
        return false;
    }

    outX = value["x"].get<float>();
    outY = value["y"].get<float>();
    outZ = value["z"].get<float>();
    outHasValue = true;
    return true;
}

} // namespace

ParsedInstantiatePrimitiveRequest ParseInstantiatePrimitiveRequest(const std::string& jsonBody)
{
    ParsedInstantiatePrimitiveRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    // 2. "shape"
    if (!parsed.contains("shape") || !parsed["shape"].is_string() ||
        parsed["shape"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: shape";
        return result;
    }
    result.shape = parsed["shape"].get<std::string>();

    // 3. "name"
    if (!parsed.contains("name") || !parsed["name"].is_string() ||
        parsed["name"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: name";
        return result;
    }
    result.name = parsed["name"].get<std::string>();

    // 4. "world_position" (optional)
    if (parsed.contains("world_position")) {
        const nlohmann::json& worldPosition = parsed["world_position"];
        if (!worldPosition.is_object()) {
            result.errorMessage = "world_position must be an object";
            return result;
        }
        if (worldPosition.contains("x")) {
            if (!worldPosition["x"].is_number()) {
                result.errorMessage = "world_position.x must be a number";
                return result;
            }
            result.worldX = worldPosition["x"].get<float>();
        }
        if (worldPosition.contains("y")) {
            if (!worldPosition["y"].is_number()) {
                result.errorMessage = "world_position.y must be a number";
                return result;
            }
            result.worldY = worldPosition["y"].get<float>();
        }
        if (worldPosition.contains("z")) {
            if (!worldPosition["z"].is_number()) {
                result.errorMessage = "world_position.z must be a number";
                return result;
            }
            result.worldZ = worldPosition["z"].get<float>();
        }
    }

    // 5. "parent" (optional)
    if (parsed.contains("parent") && !parsed["parent"].is_null()) {
        const nlohmann::json& parent = parsed["parent"];
        if (!parent.is_string()) {
            result.errorMessage = "parent must be a string or null";
            return result;
        }
        const std::string parentName = parent.get<std::string>();
        if (!parentName.empty()) {
            result.hasParent = true;
            result.parentName = parentName;
        }
    }

    result.valid = true;
    return result;
}

ParsedDeleteEntityRequest ParseDeleteEntityRequest(const std::string& jsonBody)
{
    ParsedDeleteEntityRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    if (!parsed.contains("name") || !parsed["name"].is_string() ||
        parsed["name"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: name";
        return result;
    }
    result.name = parsed["name"].get<std::string>();

    result.valid = true;
    return result;
}

// --- network-impl-5 campaign
// (PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md) - see
// NetworkRoutes.h's own doc comments above each declaration for the exact,
// locked validation rules implemented below.

ParsedSetEntityTrsRequest ParseSetEntityTrsRequest(const std::string& jsonBody)
{
    ParsedSetEntityTrsRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    // 2. "name"
    if (!parsed.contains("name") || !parsed["name"].is_string() ||
        parsed["name"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: name";
        return result;
    }
    result.name = parsed["name"].get<std::string>();

    // 3. "translation" (optional, all-or-nothing)
    if (!TryParseAllOrNothingXyz(parsed, "translation", result.hasTranslation, result.translationX,
            result.translationY, result.translationZ, result.errorMessage)) {
        return result;
    }

    // 4. "rotation_euler_degrees" (optional, all-or-nothing)
    if (!TryParseAllOrNothingXyz(parsed, "rotation_euler_degrees", result.hasRotationEulerDegrees,
            result.rotationPitchXDegrees, result.rotationYawYDegrees, result.rotationRollZDegrees,
            result.errorMessage)) {
        return result;
    }

    // 5. "scale" (optional, all-or-nothing)
    if (!TryParseAllOrNothingXyz(
            parsed, "scale", result.hasScale, result.scaleX, result.scaleY, result.scaleZ, result.errorMessage)) {
        return result;
    }

    result.valid = true;
    return result;
}

ParsedInstantiateLightRequest ParseInstantiateLightRequest(const std::string& jsonBody)
{
    ParsedInstantiateLightRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    // 2. "light_type" (optional)
    if (parsed.contains("light_type")) {
        if (!parsed["light_type"].is_string()) {
            result.errorMessage = "light_type must be a string";
            return result;
        }
        result.lightType = parsed["light_type"].get<std::string>();
    }

    // 3. "name" (required)
    if (!parsed.contains("name") || !parsed["name"].is_string() ||
        parsed["name"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: name";
        return result;
    }
    result.name = parsed["name"].get<std::string>();

    // 4. "world_position" (optional, per-axis-optional-defaults-to-0)
    if (parsed.contains("world_position")) {
        const nlohmann::json& worldPosition = parsed["world_position"];
        if (!worldPosition.is_object()) {
            result.errorMessage = "world_position must be an object";
            return result;
        }
        if (worldPosition.contains("x")) {
            if (!worldPosition["x"].is_number()) {
                result.errorMessage = "world_position.x must be a number";
                return result;
            }
            result.worldX = worldPosition["x"].get<float>();
        }
        if (worldPosition.contains("y")) {
            if (!worldPosition["y"].is_number()) {
                result.errorMessage = "world_position.y must be a number";
                return result;
            }
            result.worldY = worldPosition["y"].get<float>();
        }
        if (worldPosition.contains("z")) {
            if (!worldPosition["z"].is_number()) {
                result.errorMessage = "world_position.z must be a number";
                return result;
            }
            result.worldZ = worldPosition["z"].get<float>();
        }
    }

    // 5. "rotation_euler_degrees" (optional, all-or-nothing)
    if (!TryParseAllOrNothingXyz(parsed, "rotation_euler_degrees", result.hasRotationEulerDegrees,
            result.rotationPitchXDegrees, result.rotationYawYDegrees, result.rotationRollZDegrees,
            result.errorMessage)) {
        return result;
    }

    // 6. "color" (optional; present-and-non-null must be an object; each of
    // r/g/b is itself optional-but-numeric-if-present)
    if (parsed.contains("color") && !parsed["color"].is_null()) {
        const nlohmann::json& color = parsed["color"];
        if (!color.is_object()) {
            result.errorMessage = "color must be an object";
            return result;
        }
        if (color.contains("r")) {
            if (!color["r"].is_number()) {
                result.errorMessage = "color.r must be a number";
                return result;
            }
            result.colorR = color["r"].get<float>();
        }
        if (color.contains("g")) {
            if (!color["g"].is_number()) {
                result.errorMessage = "color.g must be a number";
                return result;
            }
            result.colorG = color["g"].get<float>();
        }
        if (color.contains("b")) {
            if (!color["b"].is_number()) {
                result.errorMessage = "color.b must be a number";
                return result;
            }
            result.colorB = color["b"].get<float>();
        }
    }

    // 7. "illuminance_lux" (optional, must be >= 0 if present)
    if (parsed.contains("illuminance_lux")) {
        if (!parsed["illuminance_lux"].is_number()) {
            result.errorMessage = "illuminance_lux must be a non-negative number";
            return result;
        }
        const float illuminance = parsed["illuminance_lux"].get<float>();
        if (illuminance < 0.0f) {
            result.errorMessage = "illuminance_lux must be a non-negative number";
            return result;
        }
        result.illuminanceLux = illuminance;
    }

    // 8. "active" (optional bool)
    if (parsed.contains("active")) {
        if (!parsed["active"].is_boolean()) {
            result.errorMessage = "active must be a boolean";
            return result;
        }
        result.active = parsed["active"].get<bool>();
    }

    // 9. "parent" (optional) - identical rule to
    // ParseInstantiatePrimitiveRequest()'s own "parent" field.
    if (parsed.contains("parent") && !parsed["parent"].is_null()) {
        const nlohmann::json& parent = parsed["parent"];
        if (!parent.is_string()) {
            result.errorMessage = "parent must be a string or null";
            return result;
        }
        const std::string parentName = parent.get<std::string>();
        if (!parentName.empty()) {
            result.hasParent = true;
            result.parentName = parentName;
        }
    }

    result.valid = true;
    return result;
}

std::string BuildInstantiatePrimitiveResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& resolvedName,
    bool parentRequestedButNotFound, const std::string& requestedParentName)
{
    if (!success) {
        return BuildGenericErrorResponseJson(errorMessage);
    }

    nlohmann::json body;
    body["success"] = true;
    body["entity"] = nlohmann::json::object({ { "index", entityIndex }, { "generation", entityGeneration } });
    body["name"] = resolvedName;
    body["parent_requested_but_not_found"] = parentRequestedButNotFound;
    body["requested_parent_name"] = requestedParentName;
    return body.dump();
}

std::string BuildDeleteEntityResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t deletedEntityIndex, std::uint32_t deletedEntityGeneration)
{
    if (!success) {
        return BuildGenericErrorResponseJson(errorMessage);
    }

    nlohmann::json body;
    body["success"] = true;
    body["entity"] =
        nlohmann::json::object({ { "index", deletedEntityIndex }, { "generation", deletedEntityGeneration } });
    return body.dump();
}

std::string BuildGenericErrorResponseJson(const std::string& errorMessage)
{
    nlohmann::json body;
    body["success"] = false;
    body["error"] = errorMessage;
    return body.dump();
}

std::string BuildSetEntityTrsResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration, bool translationChanged, bool rotationChanged,
    bool scaleChanged, const TransformSnapshotView& resultingTransform)
{
    if (!success) {
        return BuildGenericErrorResponseJson(errorMessage);
    }

    nlohmann::json body;
    body["success"] = true;
    body["entity"] = nlohmann::json::object({ { "index", entityIndex }, { "generation", entityGeneration } });
    body["changed"] = nlohmann::json::object({
        { "translation", translationChanged },
        { "rotation", rotationChanged },
        { "scale", scaleChanged },
    });
    body["transform"] = nlohmann::json::object({
        { "position",
            nlohmann::json::object({ { "x", resultingTransform.positionX }, { "y", resultingTransform.positionY },
                { "z", resultingTransform.positionZ } }) },
        { "rotation_euler_degrees",
            nlohmann::json::object({ { "x", resultingTransform.rotationEulerXDegrees },
                { "y", resultingTransform.rotationEulerYDegrees },
                { "z", resultingTransform.rotationEulerZDegrees } }) },
        { "rotation_quaternion",
            nlohmann::json::object({ { "x", resultingTransform.rotationQuatX },
                { "y", resultingTransform.rotationQuatY }, { "z", resultingTransform.rotationQuatZ },
                { "w", resultingTransform.rotationQuatW } }) },
        { "scale",
            nlohmann::json::object({ { "x", resultingTransform.scaleX }, { "y", resultingTransform.scaleY },
                { "z", resultingTransform.scaleZ } }) },
    });
    return body.dump();
}

// --- network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md)

ParsedGetTextureQuery ParseGetTextureQuery(const std::string& textureNameParam, const std::string& channelParam)
{
    ParsedGetTextureQuery result;
    if (textureNameParam.empty()) {
        result.errorMessage = "missing or empty required query parameter: texture_name";
        return result;
    }
    result.textureName = textureNameParam;

    if (channelParam.empty() || channelParam == "color") {
        result.wantsDepth = false;
    } else if (channelParam == "depth") {
        result.wantsDepth = true;
    } else {
        result.errorMessage = "invalid channel - must be \"color\" or \"depth\"";
        return result;
    }

    result.valid = true;
    return result;
}

std::string BuildTextureCaptureJsonBody(
    int width, int height, const std::string& base64Png, std::uint64_t framesSinceUpdate)
{
    nlohmann::json body;
    body["width"] = width;
    body["height"] = height;
    body["format"] = "png";
    body["data_base64"] = base64Png;
    body["frames_since_update"] = framesSinceUpdate;
    return body.dump();
}

std::string BuildListTexturesResponseJson(const std::vector<TextureListEntryView>& entries)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const TextureListEntryView& entry : entries) {
        nlohmann::json item;
        item["name"] = entry.name;
        item["regime"] = entry.regime;
        item["format"] = entry.format;
        item["width"] = entry.width;
        item["height"] = entry.height;
        item["has_depth"] = entry.hasDepth;
        item["frames_since_update"] = entry.framesSinceUpdate;
        item["kind"] = entry.kind;
        item["depth"] = entry.depth;
        arr.push_back(std::move(item));
    }
    nlohmann::json body;
    body["textures"] = std::move(arr);
    return body.dump();
}

// editor-core-separation-7 campaign, PHASE4 - GET /render_graph. Uses
// gte::rg::to_json(nlohmann::json&, const RenderGraphMetadata&) via ADL -
// already the fully-resolved, plain-scalars-and-strings JSON shape (see
// NetworkRoutes.h's own doc comment above this function's declaration).
std::string BuildRenderGraphMetadataResponseJson(const gte::rg::RenderGraphMetadata& metadata)
{
    nlohmann::json body = metadata;
    return body.dump();
}

// --- network-impl-7 campaign - GET /activate_tab and GET /list_tabs. See
// NetworkRoutes.h's own doc comments above each declaration for the exact,
// locked validation/response rules implemented below.

ParsedActivateTabQuery ParseActivateTabQuery(const std::string& nameParam)
{
    ParsedActivateTabQuery result;
    if (nameParam.empty()) {
        result.valid = false;
        result.errorMessage = "missing or empty required query parameter: name";
        return result;
    }
    result.valid = true;
    result.tabName = nameParam;
    {
        std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
        result.notFound = !EditorPanelRegistry::Instance().IsKnownName(nameParam);
    }
    return result;
}

std::string BuildActivateTabResponseJson(bool success, bool tabExists, const std::string& tabName)
{
    (void)tabExists; // See NetworkRoutes.h's own doc comment - `success` alone already selects the right branch below.
    nlohmann::json body;
    body["success"] = success;
    if (success) {
        body["activated_tab"] = tabName;
    } else {
        body["error"] = "panel '" + tabName +
            "' has no live window yet this session - try again after the Editor has rendered at least one frame";
    }
    return body.dump();
}

std::string BuildUnknownTabNameResponseJson(const std::string& tabName)
{
    nlohmann::json body;
    body["success"] = false;
    body["error"] = "unknown tab name '" + tabName + "' - see GET /list_tabs for the currently known names";
    return body.dump();
}

std::string BuildListTabsResponseJson()
{
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(GetHotReloadEngineStateMutex());
        names = EditorPanelRegistry::Instance().AllNames();
    }
    nlohmann::json body;
    body["tabs"] = nlohmann::json::array();
    for (const std::string& name : names) {
        body["tabs"].push_back(name);
    }
    return body.dump();
}

// --- task_manager/frame-debugger-3 campaign, PHASE7
// (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md) - see
// NetworkRoutes.h's own doc comments above each declaration for the exact,
// locked validation/response rules implemented below.

namespace {

// Strict whole-integer parse: the ENTIRE string must be consumed (no
// trailing garbage, e.g. "12abc" is rejected, not silently truncated to
// 12) and must not be empty - mirrors this file's own existing
// "caller mistake -> loud validation failure, never a silent best guess"
// philosophy (see this file's header comment).
bool TryParseWholeInt(const std::string& text, int& outValue)
{
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        outValue = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

// Same strict, whole-string-consumed parse as TryParseWholeInt() above, for
// a decimal float.
bool TryParseWholeFloat(const std::string& text, float& outValue)
{
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const float parsed = std::stof(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        outValue = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

ParsedFrameDebuggerEnableQuery ParseFrameDebuggerEnableQuery(const std::string& valueParam)
{
    ParsedFrameDebuggerEnableQuery result;
    if (valueParam == "true") {
        result.value = true;
    } else if (valueParam == "false") {
        result.value = false;
    } else {
        result.errorMessage = "missing or invalid required query parameter: value - must be \"true\" or \"false\"";
        return result;
    }
    result.valid = true;
    return result;
}

ParsedFrameDebuggerSelectEventQuery ParseFrameDebuggerSelectEventQuery(const std::string& indexParam)
{
    ParsedFrameDebuggerSelectEventQuery result;
    int parsedIndex = -1;
    if (!TryParseWholeInt(indexParam, parsedIndex)) {
        result.errorMessage = "missing or invalid required query parameter: index - must be an integer";
        return result;
    }
    result.index = parsedIndex;
    result.valid = true;
    return result;
}

ParsedFrameDebuggerSetChannelQuery ParseFrameDebuggerSetChannelQuery(const std::string& valueParam)
{
    ParsedFrameDebuggerSetChannelQuery result;
    if (valueParam != "all" && valueParam != "r" && valueParam != "g" && valueParam != "b" && valueParam != "a") {
        result.errorMessage = "invalid channel - must be \"all\", \"r\", \"g\", \"b\", or \"a\"";
        return result;
    }
    result.channel = valueParam;
    result.valid = true;
    return result;
}

ParsedFrameDebuggerSetLevelsQuery ParseFrameDebuggerSetLevelsQuery(
    const std::string& blackParam, const std::string& whiteParam)
{
    ParsedFrameDebuggerSetLevelsQuery result;
    float parsedBlack = 0.0f;
    if (!TryParseWholeFloat(blackParam, parsedBlack)) {
        result.errorMessage = "missing or invalid required query parameter: black - must be a number";
        return result;
    }
    float parsedWhite = 1.0f;
    if (!TryParseWholeFloat(whiteParam, parsedWhite)) {
        result.errorMessage = "missing or invalid required query parameter: white - must be a number";
        return result;
    }
    result.black = parsedBlack;
    result.white = parsedWhite;
    result.valid = true;
    return result;
}

namespace {

nlohmann::json FrameDebuggerStateToJson(const FrameDebuggerStateResponseView& state)
{
    nlohmann::json body;
    body["enabled"] = state.enabled;
    body["windowOpen"] = state.windowOpen;
    body["hasCapturedFrame"] = state.hasCapturedFrame;
    body["totalEventCount"] = state.totalEventCount;
    body["selectedEventIndex"] = state.selectedEventIndex;
    body["channel"] = state.channel;
    body["levelsBlack"] = state.levelsBlack;
    body["levelsWhite"] = state.levelsWhite;
    body["perDrawEventCount"] = state.perDrawEventCount;
    return body;
}

} // namespace

std::string BuildFrameDebuggerStateResponseJson(const FrameDebuggerStateResponseView& state)
{
    return FrameDebuggerStateToJson(state).dump();
}

std::string BuildFrameDebuggerCommandResponseJson(
    bool success, const std::string& errorMessage, const FrameDebuggerStateResponseView& state)
{
    nlohmann::json body;
    body["success"] = success;
    if (!success) {
        body["error"] = errorMessage;
    }
    body["state"] = FrameDebuggerStateToJson(state);
    return body.dump();
}


// --- task_manager/stl-parser-2 campaign, PHASE2 - POST /import_asset. See
// NetworkRoutes.h's own doc comments above each declaration for the exact,
// locked validation/response rules implemented below.

ParsedImportAssetRequest ParseImportAssetRequest(const std::string& jsonBody)
{
    ParsedImportAssetRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    // 2. "source_path"
    if (!parsed.contains("source_path") || !parsed["source_path"].is_string() ||
        parsed["source_path"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: source_path";
        return result;
    }
    result.sourcePath = parsed["source_path"].get<std::string>();

    // 3. "destination_folder" (optional - absent/null/empty string all mean "")
    if (parsed.contains("destination_folder") && !parsed["destination_folder"].is_null()) {
        if (!parsed["destination_folder"].is_string()) {
            result.errorMessage = "destination_folder must be a string";
            return result;
        }
        result.destinationFolder = parsed["destination_folder"].get<std::string>();
    }

    result.valid = true;
    return result;
}

std::string BuildImportAssetResponseJson(const ImportedAssetResponseView& view)
{
    nlohmann::json body;
    body["success"] = true;
    body["message"] = view.message;
    body["final_relative_path"] = view.finalRelativePath;
    body["final_absolute_path"] = view.finalAbsolutePath;
    body["guid"] = view.guid;
    body["converted_to_mesh_asset"] = view.convertedToMeshAsset;
    body["mesh_source_format"] = view.meshSourceFormat;
    body["converted_to_ktx2"] = view.convertedToKtx2;
    body["converted_to_motion_asset"] = view.convertedToMotionAsset;
    body["mesh_vertex_count"] = view.meshVertexCount;
    body["mesh_triangle_count"] = view.meshTriangleCount;
    return body.dump();
}

// --- task_manager/stl-parser-2 campaign, PHASE4 - POST /instantiate_asset.
// See NetworkRoutes.h's own doc comments above each declaration for the
// exact, locked validation/response rules implemented below.

ParsedInstantiateAssetRequest ParseInstantiateAssetRequest(const std::string& jsonBody)
{
    ParsedInstantiateAssetRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    if (!parsed.contains("gta_path") || !parsed["gta_path"].is_string() ||
        parsed["gta_path"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: gta_path";
        return result;
    }
    result.gtaPath = parsed["gta_path"].get<std::string>();

    result.valid = true;
    return result;
}

std::string BuildInstantiateAssetResponseJson(
    std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& resolvedName)
{
    nlohmann::json body;
    body["success"] = true;
    body["entity"] = nlohmann::json::object({ { "index", entityIndex }, { "generation", entityGeneration } });
    body["name"] = resolvedName;
    return body.dump();
}

// --- task_manager/scene-serialization-2 campaign, PHASE5
// (PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md) - POST /save_scene and
// POST /load_scene. See NetworkRoutes.h's own doc comment above
// ParseScenePathRequest() for the exact, locked (and deliberately
// DIFFERENT from every other parser in this file) validation rules
// implemented below.

ParsedScenePathRequest ParseScenePathRequest(const std::string& jsonBody)
{
    ParsedScenePathRequest result;

    // DELIBERATE EXCEPTION to this file's usual "malformed/non-object JSON
    // body is a validation failure" convention (see every other
    // ParseXxxRequest() above) - see NetworkRoutes.h's own doc comment on
    // ParsedScenePathRequest for why: both /save_scene and /load_scene have
    // exactly ONE optional field, so "no body at all" (or a body that
    // isn't even valid JSON) is treated identically to "path omitted"
    // rather than rejected outright. Do NOT "fix" this to match the other
    // parsers in this file.
    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.valid = true;
        return result;
    }

    if (parsed.contains("path") && !parsed["path"].is_null()) {
        if (!parsed["path"].is_string()) {
            result.errorMessage = "path must be a string";
            return result;
        }
        result.path = parsed["path"].get<std::string>();
    }

    result.valid = true;
    return result;
}

std::string BuildScenePathResponseJson(bool success, const std::string& errorMessage, const std::string& resolvedPath)
{
    if (!success) {
        return BuildGenericErrorResponseJson(errorMessage);
    }

    nlohmann::json body;
    body["success"] = true;
    body["resolved_path"] = resolvedPath;
    return body.dump();
}

std::string BuildOpenSponzaSceneResponseJson(
    bool importedThisCall, const std::string& gtaPath, const std::string& message)
{
    nlohmann::json body;
    body["success"] = true;
    body["imported_this_call"] = importedThisCall;
    body["gta_path"] = gtaPath;
    body["message"] = message;
    return body.dump();
}

// --- task_manager/logger-1 campaign, PHASE3
// (PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md) - GET /get_logs and
// POST /clear_logs. See NetworkRoutes.h's own doc comments above each
// declaration for the exact, locked validation/response rules implemented
// below.

namespace {

// Strict, whole-string-consumed, NON-NEGATIVE base-10 std::uint64_t parse -
// since_id/frame_min/frame_max/limit must all reject a leading '-' (unlike
// TryParseWholeInt() above, which intentionally accepts one for
// /frame_debugger/select_event's own "-1 means deselect" semantics - not
// appropriate here, since every one of these four query parameters is
// always a non-negative count/id).
bool TryParseNonNegativeUInt64(const std::string& text, std::uint64_t& outValue)
{
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed);
        if (consumed != text.size()) {
            return false;
        }
        outValue = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

ParsedGetLogsQuery ParseGetLogsQuery(const std::string& sinceIdParam, const std::string& minLevelParam,
    const std::string& categoryParam, const std::string& keywordParam, const std::string& frameMinParam,
    const std::string& frameMaxParam, const std::string& limitParam)
{
    ParsedGetLogsQuery result;

    if (!sinceIdParam.empty()) {
        std::uint64_t sinceId = 0;
        if (!TryParseNonNegativeUInt64(sinceIdParam, sinceId)) {
            result.errorMessage = "invalid query parameter: since_id - must be a non-negative integer";
            return result;
        }
        result.filter.sinceId = sinceId;
    }

    if (!minLevelParam.empty()) {
        LogLevel level = LogLevel::Debug;
        if (!TryParseLogLevel(minLevelParam, &level)) {
            result.errorMessage =
                "invalid query parameter: min_level - must be \"debug\", \"info\", \"warning\", or \"error\"";
            return result;
        }
        result.filter.hasMinLevel = true;
        result.filter.minLevel = level;
    }

    // category/keyword: always valid as-is, any string (including empty).
    result.filter.category = categoryParam;
    result.filter.keyword = keywordParam;

    if (!frameMinParam.empty()) {
        std::uint64_t frameMin = 0;
        if (!TryParseNonNegativeUInt64(frameMinParam, frameMin)) {
            result.errorMessage = "invalid query parameter: frame_min - must be a non-negative integer";
            return result;
        }
        result.filter.hasFrameMin = true;
        result.filter.frameMin = frameMin;
    }

    if (!frameMaxParam.empty()) {
        std::uint64_t frameMax = 0;
        if (!TryParseNonNegativeUInt64(frameMaxParam, frameMax)) {
            result.errorMessage = "invalid query parameter: frame_max - must be a non-negative integer";
            return result;
        }
        result.filter.hasFrameMax = true;
        result.filter.frameMax = frameMax;
    }

    if (limitParam.empty()) {
        result.filter.limit = 200;
    } else {
        std::uint64_t limit = 0;
        if (!TryParseNonNegativeUInt64(limitParam, limit)) {
            result.errorMessage = "invalid query parameter: limit - must be a non-negative integer";
            return result;
        }
        // Silently clamped to kLogCapacity (Core/Logging.h) when larger - a
        // caller asking for "too many" is harmless, unlike a genuinely
        // malformed value (see NetworkRoutes.h's own doc comment).
        result.filter.limit =
            static_cast<std::size_t>(limit > kLogCapacity ? kLogCapacity : limit);
    }

    // Every one of the six parameters above has now been checked and none
    // of them set `valid = false` - see ParsedGetLogsQuery's own doc
    // comment (Logger-1 PHASE3's corrected Step 3.1/3.2) for why this must
    // be set explicitly, as the very last statement, rather than defaulted
    // to true.
    result.valid = true;
    return result;
}

std::string BuildGetLogsResponseJson(
    const std::vector<LogEntry>& entries, bool loggingEnabled, std::uint64_t latestId)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const LogEntry& entry : entries) {
        nlohmann::json item;
        item["id"] = entry.id;
        item["frame"] = entry.frameNumber;
        item["timestamp_seconds"] = entry.timestampSeconds;
        item["level"] = ToString(entry.level);
        item["category"] = entry.category;
        item["message"] = entry.message;
        item["is_blocking"] = entry.isBlocking;
        arr.push_back(std::move(item));
    }

    nlohmann::json body;
    body["logging_enabled"] = loggingEnabled;
    body["count"] = entries.size();
    body["latest_id"] = latestId;
    body["entries"] = std::move(arr);
    return body.dump();
}

std::string BuildClearLogsResponseJson(std::size_t clearedCount)
{
    nlohmann::json body;
    body["success"] = true;
    body["cleared_count"] = clearedCount;
    return body.dump();
}

std::optional<std::uint32_t> ExactUint32InRange(double value, std::uint32_t minValue, std::uint32_t maxValue)
{
    if (!std::isfinite(value) || value != std::floor(value) || value < static_cast<double>(minValue)
        || value > static_cast<double>(maxValue)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

ParsedSpawnInstanceGridRequest ParseSpawnInstanceGridRequest(const std::string& jsonBody)
{
    ParsedSpawnInstanceGridRequest result;

    // A missing or non-object body means "use the defaults".
    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.valid = true;
        return result;
    }

    if (parsed.contains("count") && !parsed["count"].is_null()) {
        const std::string rangeMessage =
            "count must be an integer from 1 to " + std::to_string(kMaxSpawnGridInstances);
        if (!parsed["count"].is_number()) {
            result.errorMessage = rangeMessage;
            return result;
        }
        const std::optional<std::uint32_t> count =
            ExactUint32InRange(parsed["count"].get<double>(), 1u, kMaxSpawnGridInstances);
        if (!count.has_value()) {
            result.errorMessage = rangeMessage;
            return result;
        }
        result.instanceCount = *count;
    }

    if (parsed.contains("spacing") && !parsed["spacing"].is_null()) {
        const std::string rangeMessage = "spacing must be a number from " + std::to_string(kMinSpawnGridSpacing)
            + " to " + std::to_string(kMaxSpawnGridSpacing);
        if (!parsed["spacing"].is_number()) {
            result.errorMessage = rangeMessage;
            return result;
        }
        const double spacing = parsed["spacing"].get<double>();
        if (!std::isfinite(spacing) || spacing < static_cast<double>(kMinSpawnGridSpacing)
            || spacing > static_cast<double>(kMaxSpawnGridSpacing)) {
            result.errorMessage = rangeMessage;
            return result;
        }
        result.spacing = static_cast<float>(spacing);
    }

    result.valid = true;
    return result;
}

std::string BuildSpawnInstanceGridResponseJson(
    bool success, const std::string& errorMessage, std::uint32_t instanceCount)
{
    if (!success) {
        return BuildGenericErrorResponseJson(errorMessage);
    }

    nlohmann::json body;
    body["success"] = true;
    body["instance_count"] = instanceCount;
    return body.dump();
}

namespace {

// Reads an optional numeric field. Missing or null leaves `out` empty. A non-number sets `error`.
bool ReadOptionalNumber(const nlohmann::json& body, const char* field, std::optional<double>& out, std::string& error)
{
    out.reset();
    const auto it = body.find(field);
    if (it == body.end() || it->is_null()) {
        return true;
    }
    if (!it->is_number()) {
        error = std::string(field) + " must be a number";
        return false;
    }
    out = it->get<double>();
    return true;
}

// Reads weld and the numeric limits shared by both cluster build routes. False with errorMessage set on a bad value.
bool ReadClusterOptionFields(const nlohmann::json& parsed, ParsedBuildClustersRequest& result)
{
    if (parsed.contains("weld") && !parsed["weld"].is_null()) {
        if (!parsed["weld"].is_string()) {
            result.errorMessage = "weld must be a string";
            return false;
        }
        const std::string weld = parsed["weld"].get<std::string>();
        if (weld != "exact" && weld != "position_only") {
            result.errorMessage = "weld must be \"exact\" or \"position_only\"";
            return false;
        }
        result.weld = weld;
    }

    std::optional<double> number;
    if (!ReadOptionalNumber(parsed, "max_vertices", number, result.errorMessage)) {
        return false;
    }
    if (number.has_value()) {
        const std::optional<std::uint32_t> value = ExactUint32InRange(*number, 3u, 255u);
        if (!value.has_value()) {
            result.errorMessage = "max_vertices must be an integer from 3 to 255";
            return false;
        }
        result.maxVertices = *value;
    }

    if (!ReadOptionalNumber(parsed, "max_triangles", number, result.errorMessage)) {
        return false;
    }
    if (number.has_value()) {
        const std::optional<std::uint32_t> value = ExactUint32InRange(*number, 4u, 512u);
        if (!value.has_value() || (*value % 4u) != 0u) {
            result.errorMessage = "max_triangles must be a multiple of 4 from 4 to 512";
            return false;
        }
        result.maxTriangles = *value;
    }

    if (!ReadOptionalNumber(parsed, "cone_weight", number, result.errorMessage)) {
        return false;
    }
    if (number.has_value()) {
        if (!std::isfinite(*number) || *number < 0.0 || *number > 1.0) {
            result.errorMessage = "cone_weight must be a number from 0 to 1";
            return false;
        }
        result.coneWeight = static_cast<float>(*number);
    }
    return true;
}

} // namespace

ParsedBuildClustersRequest ParseBuildClustersRequest(const std::string& jsonBody)
{
    ParsedBuildClustersRequest result;

    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }

    if (!parsed.contains("gta_path") || !parsed["gta_path"].is_string()
        || parsed["gta_path"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: gta_path";
        return result;
    }
    result.gtaPath = parsed["gta_path"].get<std::string>();

    result.valid = ReadClusterOptionFields(parsed, result);
    return result;
}

ParsedBuildClustersRequest ParseInspectorBuildClustersRequest(const std::string& jsonBody)
{
    ParsedBuildClustersRequest result;

    const bool blank = std::all_of(jsonBody.begin(), jsonBody.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    if (blank) {
        result.valid = true;
        return result;
    }
    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }
    result.valid = ReadClusterOptionFields(parsed, result);
    return result;
}

ParsedSelectAssetQuery ParseSelectAssetQuery(const std::string& pathParam)
{
    ParsedSelectAssetQuery result;
    if (pathParam.empty()) {
        result.errorMessage = "missing required query parameter: path";
        return result;
    }
    const std::size_t dot = pathParam.rfind('.');
    std::string extension = dot == std::string::npos ? std::string() : pathParam.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool driveAbsolute = pathParam.size() >= 3 && std::isalpha(static_cast<unsigned char>(pathParam[0])) != 0
        && pathParam[1] == ':' && (pathParam[2] == '\\' || pathParam[2] == '/');
    const bool rootAbsolute = pathParam[0] == '\\' || pathParam[0] == '/';
    if (extension != ".gta" || !(driveAbsolute || rootAbsolute)) {
        result.errorMessage = "path must be an absolute path to a *.gta file";
        return result;
    }
    result.path = pathParam;
    result.valid = true;
    return result;
}

std::string BuildSelectAssetResponseJson(const std::string& selectedPath)
{
    nlohmann::json body;
    body["success"] = true;
    body["selected_path"] = selectedPath;
    return body.dump();
}

ParsedSplitViewsQuery ParseSplitViewsQuery(const std::string& enabledParam)
{
    ParsedSplitViewsQuery result;
    if (enabledParam == "true") {
        result.enabled = true;
    } else if (enabledParam == "false") {
        result.enabled = false;
    } else {
        result.errorMessage = "missing or invalid required query parameter: enabled - must be \"true\" or \"false\"";
        return result;
    }
    result.valid = true;
    return result;
}

std::string BuildSplitViewsResponseJson(bool split)
{
    nlohmann::json body;
    body["success"] = true;
    body["split"] = split;
    return body.dump();
}

std::string BuildInspectorStateResponseJson(const InspectorStateResponseView& view)
{
    nlohmann::json body;
    body["selection_kind"] = view.selectionKind;
    body["selected_path"] = view.selectedPath;
    body["shown_path"] = view.shownPath;
    body["clusters_text"] = view.clustersText;
    body["cluster_status"] = view.clusterStatus;
    body["build_button_enabled"] = view.buildEnabled;
    body["build_button_disabled_reason"] = view.buildDisabledReason;
    body["build_running"] = view.buildRunning;
    body["clone_button_disabled_reason"] = view.cloneButtonDisabledReason;
    body["clone_destination"] = view.cloneDestination;
    body["clone_weld"] = view.cloneWeld;
    body["open_viewer_after_clone"] = view.openViewerAfterClone;
    return body.dump();
}

std::string BuildInspectorBuildStartedResponseJson()
{
    nlohmann::json body;
    body["success"] = true;
    body["started"] = true;
    return body.dump();
}

std::string BuildClustersResponseJson(const ClusterReportResponseView& view)
{
    nlohmann::json body;
    body["success"] = true;
    body["weld"] = view.weld;
    body["vertex_count_before"] = view.vertexCountBefore;
    body["vertex_count_after"] = view.vertexCountAfter;
    body["triangle_count"] = view.triangleCount;
    body["cluster_count"] = view.clusterCount;
    body["avg_triangles_per_cluster"] = view.avgTrianglesPerCluster;
    body["avg_vertices_per_cluster"] = view.avgVerticesPerCluster;
    body["poor_fill"] = view.poorFill;
    body["milliseconds"] = view.milliseconds;
    return body.dump();
}

// --- editor-core-separation-8 campaign, PHASE5
// (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - see NetworkRoutes.h's
// own doc comments above each declaration for the exact, locked validation/
// response rules implemented below.

ParsedRenderGraphSetPassEnabledQuery ParseRenderGraphSetPassEnabledQuery(
    const std::string& nameParam, const std::string& enabledParam)
{
    ParsedRenderGraphSetPassEnabledQuery result;
    if (nameParam.empty()) {
        result.errorMessage = "missing or invalid required query parameter: name - must be non-empty";
        return result;
    }
    if (enabledParam == "true") {
        result.enabled = true;
    } else if (enabledParam == "false") {
        result.enabled = false;
    } else {
        result.errorMessage = "missing or invalid required query parameter: enabled - must be \"true\" or \"false\"";
        return result;
    }
    result.name = nameParam;
    result.valid = true;
    return result;
}

ParsedRenderGraphSetFeatureEnabledQuery ParseRenderGraphSetFeatureEnabledQuery(
    const std::string& nameParam, const std::string& enabledParam)
{
    ParsedRenderGraphSetFeatureEnabledQuery result;
    if (nameParam.empty()) {
        result.errorMessage = "missing or invalid required query parameter: name - must be non-empty";
        return result;
    }
    if (enabledParam == "true") {
        result.enabled = true;
    } else if (enabledParam == "false") {
        result.enabled = false;
    } else {
        result.errorMessage = "missing or invalid required query parameter: enabled - must be \"true\" or \"false\"";
        return result;
    }
    result.name = nameParam;
    result.valid = true;
    return result;
}

ParsedRenderGraphSetFeaturePriorityQuery ParseRenderGraphSetFeaturePriorityQuery(
    const std::string& nameParam, const std::string& priorityParam)
{
    ParsedRenderGraphSetFeaturePriorityQuery result;
    if (nameParam.empty()) {
        result.errorMessage = "missing or invalid required query parameter: name - must be non-empty";
        return result;
    }
    int parsedPriority = 0;
    if (!TryParseWholeInt(priorityParam, parsedPriority)) {
        result.errorMessage = "missing or invalid required query parameter: priority - must be an integer";
        return result;
    }
    result.name = nameParam;
    result.priority = static_cast<std::int32_t>(parsedPriority);
    result.valid = true;
    return result;
}

std::string BuildRenderGraphControlCommandResponseJson(
    bool success, const std::string& errorMessage, std::optional<bool> windowOpen)
{
    nlohmann::json body;
    body["success"] = success;
    if (!success) {
        body["error"] = errorMessage;
    }
    if (windowOpen.has_value()) {
        body["window_open"] = *windowOpen;
    }
    return body.dump();
}

std::string BuildRenderGraphControlPassStatesResponseJson(
    const std::vector<RenderGraphControlPassStateResponseView>& passStates)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const RenderGraphControlPassStateResponseView& state : passStates) {
        nlohmann::json item;
        item["name"] = state.name;
        item["enabled"] = state.enabled;
        item["ever_declared_this_session"] = state.everDeclaredThisSession;
        item["owning_feature_name"] = state.owningFeatureName;
        arr.push_back(std::move(item));
    }

    nlohmann::json body;
    body["passes"] = std::move(arr);
    return body.dump();
}

ParsedRenderGraphSetDisplayRegimeQuery ParseRenderGraphSetDisplayRegimeQuery(const std::string& regimeParam)
{
    ParsedRenderGraphSetDisplayRegimeQuery parsed;
    if (regimeParam == "present") {
        parsed.valid = true;
        parsed.present = true;
    } else if (regimeParam == "offscreen") {
        parsed.valid = true;
        parsed.present = false;
    } else {
        parsed.errorMessage = "missing or invalid required query parameter: regime - must be \"present\" or \"offscreen\"";
    }
    return parsed;
}
// --- editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - see NetworkRoutes.h's own doc comments above each
// declaration for the exact, locked response/validation shapes implemented
// below.

std::string BuildHotReloadStatusResponseJson(const IHotReloadDebugCapability::Status& status)
{
    nlohmann::json body;
    body["phase"] = status.phase;
    body["project_name"] = status.projectName;
    body["cycle_id"] = status.cycleId;
    body["phase_elapsed_ms"] = status.phaseElapsedMilliseconds;
    body["last_outcome"] = status.lastOutcome;
    body["last_error_message"] = status.lastErrorMessage;
    return body.dump();
}

ParsedProjectNameQuery ParseProjectNameQuery(const std::string& nameParam)
{
    ParsedProjectNameQuery parsed;
    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(nameParam, validationError)) {
        parsed.errorMessage = nameParam.empty() ? "'name' query parameter is required" : validationError;
        return parsed;
    }
    parsed.projectName = nameParam;
    parsed.valid = true;
    return parsed;
}

std::string BuildLedgerEntryResponseJson(
    const std::string& projectName, const IHotReloadDebugCapability::LedgerEntry& entry)
{
    nlohmann::json body;
    body["project_name"] = projectName;
    body["render_pass_names"] = entry.renderPassNames;
    // editor-core-separation-23 campaign, PHASE4
    // (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - snake_case, matching
    // every sibling key in this exact JSON body.
    body["render_feature_names"] = entry.renderFeatureNames;
    body["panel_names"] = entry.panelNames;
    body["component_type_names"] = entry.componentTypeNames;
    return body.dump();
}

std::string BuildLoadedAssembliesResponseJson(const std::vector<std::string>& dllFileNames)
{
    nlohmann::json body;
    body["dll_file_names"] = dllFileNames;
    return body.dump();
}

std::string BuildComponentTypeNamesResponseJson(const std::vector<std::string>& typeNames)
{
    nlohmann::json body;
    body["type_names"] = typeNames;
    return body.dump();
}

std::string BuildCompileOnlyTriggerResponseJson(bool started, const std::string& reason)
{
    nlohmann::json body;
    body["started"] = started;
    if (!started) {
        body["reason"] = reason;
    }
    return body.dump();
}

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE4 - LDD-HR6. Mirrors ParseFrameDebuggerSelectEventQuery()'s
// own body exactly, substituting the field name/message.
ParsedSetProbeMarkerValueQuery ParseSetProbeMarkerValueQuery(const std::string& valueParam)
{
    ParsedSetProbeMarkerValueQuery result;
    int parsedValue = 0;
    if (!TryParseWholeInt(valueParam, parsedValue)) {
        result.errorMessage = "missing or invalid required query parameter: value - must be an integer";
        return result;
    }
    result.value = parsedValue;
    result.valid = true;
    return result;
}

namespace {

std::string DumpFeatureDebugJson(const nlohmann::ordered_json& json)
{
    return json.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

nlohmann::ordered_json FeatureDebugValueToJson(const fx::FeatureDebugValue& value)
{
    return std::visit(
        [](const auto& v) -> nlohmann::ordered_json {
            if constexpr (std::is_same_v<std::decay_t<decltype(v)>, double>) {
                return std::isfinite(v) ? nlohmann::ordered_json(v) : nlohmann::ordered_json(nullptr);
            } else {
                return nlohmann::ordered_json(v);
            }
        },
        value);
}

FeatureDebugReply MakeFeatureDebugError(int status, const std::string& message, const std::string& badKey = {})
{
    nlohmann::ordered_json body;
    body["success"] = false;
    body["error"] = message;
    if (!badKey.empty()) {
        body["key"] = badKey;
    }
    return { status, DumpFeatureDebugJson(body) };
}

// Returns the error reply for a busy or timed-out bridge, or nothing when a result exists.
std::optional<FeatureDebugReply> FeatureDebugTransportError(bool alreadyPending, bool timedOut)
{
    if (alreadyPending) {
        return MakeFeatureDebugError(503, "another feature debug command is already in progress");
    }
    if (timedOut) {
        return MakeFeatureDebugError(504,
            "feature debug command timed out; it may still be applied later, so read the state before repeating a set");
    }
    return std::nullopt;
}

} // namespace

ParsedFeatureDebugQuery ParseFeatureDebugQuery(
    FeatureDebugRoute route, const std::vector<std::pair<std::string, std::string>>& params)
{
    ParsedFeatureDebugQuery parsed;
    std::unordered_set<std::string> seen;
    bool haveName = false;
    bool haveAction = false;

    for (const auto& [key, value] : params) {
        if (key.empty() || key.size() > kMaxFeatureDebugKeyBytes) {
            parsed.errorMessage = "query key '" + key.substr(0, kMaxFeatureDebugKeyBytes) + "' is empty or longer than "
                + std::to_string(kMaxFeatureDebugKeyBytes) + " bytes";
            return parsed;
        }
        if (value.size() > kMaxFeatureDebugValueBytes) {
            parsed.errorMessage = "value of query key '" + key + "' is longer than "
                + std::to_string(kMaxFeatureDebugValueBytes) + " bytes";
            return parsed;
        }
        if (!seen.insert(key).second) {
            parsed.errorMessage = "query key '" + key + "' appears more than once";
            return parsed;
        }
        if (key == "name") {
            parsed.featureName = value;
            haveName = !value.empty();
        } else if (key == "action" && route == FeatureDebugRoute::Invoke) {
            parsed.action = value;
            haveAction = !value.empty();
        } else if (route == FeatureDebugRoute::State) {
            parsed.errorMessage = "unexpected query key '" + key + "'";
            return parsed;
        } else if (parsed.args.size() >= kMaxFeatureDebugArgs) {
            parsed.errorMessage = "more than " + std::to_string(kMaxFeatureDebugArgs) + " arguments";
            return parsed;
        } else {
            parsed.args.push_back({ key, value });
        }
    }

    if (!haveName) {
        parsed.errorMessage = "missing required query parameter: name";
    } else if (route == FeatureDebugRoute::Invoke && !haveAction) {
        parsed.errorMessage = "missing required query parameter: action";
    } else {
        parsed.valid = true;
    }
    return parsed;
}

FeatureDebugReply BuildFeatureDebugListReply(
    bool alreadyPending, bool timedOut, const std::vector<std::string>& featureNames)
{
    if (auto error = FeatureDebugTransportError(alreadyPending, timedOut)) {
        return *error;
    }
    nlohmann::ordered_json body;
    body["features"] = featureNames;
    return { 200, DumpFeatureDebugJson(body) };
}

FeatureDebugReply BuildFeatureDebugReply(bool alreadyPending, bool timedOut, bool found,
    const std::string& featureName, const fx::FeatureDebugResult& result)
{
    if (auto error = FeatureDebugTransportError(alreadyPending, timedOut)) {
        return *error;
    }
    if (!found) {
        return MakeFeatureDebugError(404, "no loaded feature named '" + featureName + "' has a debug endpoint");
    }
    if (!result.ok) {
        return MakeFeatureDebugError(400, result.message, result.badKey);
    }
    nlohmann::ordered_json fields = nlohmann::ordered_json::object();
    for (const fx::FeatureDebugField& field : result.fields) {
        fields[field.key] = FeatureDebugValueToJson(field.value);
    }
    nlohmann::ordered_json body;
    body["feature"] = featureName;
    if (!result.message.empty()) {
        body["message"] = result.message;
    }
    body["fields"] = std::move(fields);
    return { 200, DumpFeatureDebugJson(body) };
}

namespace {

constexpr std::size_t kMaxViewerSetPairs = 64;

// True for a finite number in 0..1.
bool TryParseUnitFraction(const std::string& text, float& out)
{
    float value = 0.0f;
    if (!TryParseWholeFloat(text, value) || !std::isfinite(value) || value < 0.0f || value > 1.0f) {
        return false;
    }
    out = value;
    return true;
}

// Parses an optional "true"/"false" query value; empty keeps `out`.
bool TryParseOptionalBool(const std::string& text, bool& out)
{
    if (text.empty()) {
        return true;
    }
    if (text == "true") {
        out = true;
        return true;
    }
    if (text == "false") {
        out = false;
        return true;
    }
    return false;
}

nlohmann::json Xyz(const float (&v)[3])
{
    return nlohmann::json::object({ { "x", v[0] }, { "y", v[1] }, { "z", v[2] } });
}

nlohmann::json SegmentsToJson(const std::vector<OverlaySegment2D>& segments)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const OverlaySegment2D& s : segments) {
        arr.push_back(nlohmann::json::object({
            { "a", nlohmann::json::object({ { "x", s.a.x }, { "y", s.a.y } }) },
            { "b", nlohmann::json::object({ { "x", s.b.x }, { "y", s.b.y } }) },
        }));
    }
    return arr;
}

nlohmann::json ClusterViewerReportToJson(const ClusterViewerReport& r)
{
    nlohmann::json settings = nlohmann::json::object();
    for (const KeyValue& kv : r.settings) {
        settings[kv.key] = kv.value;
    }

    nlohmann::json body;
    body["open"] = r.open;
    body["path"] = r.path;
    body["valid"] = r.valid;
    body["mesh"] = nlohmann::json::object({
        { "vertices", r.mesh.vertices },
        { "triangles", r.mesh.triangles },
        { "clusters", r.mesh.clusters },
        { "clusterModes", r.mesh.clusterModes },
        { "note", r.mesh.note },
    });
    body["settings"] = std::move(settings);
    body["cull"] = nlohmann::json::object({
        { "ok", r.cull.ok },
        { "total", r.cull.total },
        { "passedFrustum", r.cull.passedFrustum },
        { "passedCone", r.cull.passedCone },
        { "visible", r.cull.visible },
        { "byFrustum", r.cull.byFrustum },
        { "byCone", r.cull.byCone },
        { "byOcclusion", r.cull.byOcclusion },
        { "visibleTriangles", r.cull.visibleTriangles },
        { "runMilliseconds", r.cull.runMilliseconds },
        { "occlusionApplied", r.cull.occlusionApplied },
        { "hizCut", r.cull.hizCut },
        { "nested", r.cull.nested },
        { "error", r.cull.error },
    });
    body["hiz"] = nlohmann::json::object({
        { "captured", r.hiz.captured },
        { "stale", r.hiz.stale },
        { "width", r.hiz.width },
        { "height", r.hiz.height },
    });
    body["truth"] = nlohmann::json::object({
        { "valid", r.truth.valid },
        { "stale", r.truth.stale },
        { "seen", r.truth.seen },
        { "falseCulls", r.truth.falseCulls },
        { "conservative", r.truth.conservative },
        { "invalidIds", r.truth.invalidIds },
        { "pixelsChecked", r.truth.pixelsChecked },
    });
    body["debugCamera"] = nlohmann::json::object({
        { "show", r.debugCamera.show },
        { "selected", r.debugCamera.selected },
        { "screen", nlohmann::json::object({ { "apexX", r.debugCamera.apexX }, { "apexY", r.debugCamera.apexY },
                                               { "visible", r.debugCamera.screenVisible } }) },
    });
    body["clusterPathSupported"] = r.clusterPathSupported;
    return body;
}

} // namespace

ParsedClusterViewerSetQuery ParseClusterViewerSetQuery(const std::vector<std::pair<std::string, std::string>>& params)
{
    ParsedClusterViewerSetQuery result;
    if (params.empty()) {
        result.errorMessage = "no setting given - pass at least one key=value query pair";
        return result;
    }
    if (params.size() > kMaxViewerSetPairs) {
        result.errorMessage = "more than " + std::to_string(kMaxViewerSetPairs) + " settings in one request";
        return result;
    }
    std::unordered_set<std::string> seen;
    for (const auto& [key, value] : params) {
        if (key.empty() || key.size() > kMaxFeatureDebugKeyBytes) {
            result.errorMessage = "query key '" + key.substr(0, kMaxFeatureDebugKeyBytes) + "' is empty or too long";
            return result;
        }
        if (value.size() > kMaxFeatureDebugValueBytes) {
            result.errorMessage = "value of query key '" + key + "' is too long";
            return result;
        }
        if (!seen.insert(key).second) {
            result.errorMessage = "query key '" + key + "' appears more than once";
            return result;
        }
        result.pairs.push_back({ key, value });
    }
    result.valid = true;
    return result;
}

ParsedPickQuery ParsePickQuery(const std::string& xParam, const std::string& yParam)
{
    ParsedPickQuery result;
    if (!TryParseUnitFraction(xParam, result.x)) {
        result.errorMessage = "missing or invalid required query parameter: x - must be a number from 0 to 1";
        return result;
    }
    if (!TryParseUnitFraction(yParam, result.y)) {
        result.errorMessage = "missing or invalid required query parameter: y - must be a number from 0 to 1";
        return result;
    }
    result.valid = true;
    return result;
}

ParsedClusterViewerImageQuery ParseClusterViewerImageQuery(const std::string& targetParam, const std::string& overlayParam)
{
    ParsedClusterViewerImageQuery result;
    if (targetParam.empty() || targetParam == "view") {
        result.debugCamera = false;
    } else if (targetParam == "debug") {
        result.debugCamera = true;
    } else {
        result.errorMessage = "invalid query parameter: target - must be \"view\" or \"debug\"";
        return result;
    }
    if (!TryParseOptionalBool(overlayParam, result.overlay)) {
        result.errorMessage = "invalid query parameter: overlay - must be \"true\" or \"false\"";
        return result;
    }
    result.valid = true;
    return result;
}

ParsedSceneCameraImageQuery ParseSceneCameraImageQuery(const std::string& overlayParam)
{
    ParsedSceneCameraImageQuery result;
    if (!TryParseOptionalBool(overlayParam, result.overlay)) {
        result.errorMessage = "invalid query parameter: overlay - must be \"true\" or \"false\"";
        return result;
    }
    result.valid = true;
    return result;
}

int HttpStatusForError(ClusterViewerError error)
{
    switch (error) {
    case ClusterViewerError::None: return 200;
    case ClusterViewerError::BadInput: return 400;
    case ClusterViewerError::Unavailable: return 503;
    case ClusterViewerError::NotOpen:
    case ClusterViewerError::NoValidMesh:
    case ClusterViewerError::Unsupported:
    case ClusterViewerError::Conflict: return 409;
    }
    return 409;
}

int HttpStatusForError(SceneCameraError error)
{
    switch (error) {
    case SceneCameraError::None: return 200;
    case SceneCameraError::BadInput: return 400;
    case SceneCameraError::NotFound: return 404;
    case SceneCameraError::Unavailable: return 503;
    case SceneCameraError::NotRendered:
    case SceneCameraError::NoCamera: return 409;
    }
    return 409;
}

std::string BuildClusterViewerStateJson(const ClusterViewerReport& report)
{
    return ClusterViewerReportToJson(report).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string BuildClusterViewerCommandResponseJson(const ClusterViewerResult& result, const ClusterViewerReport& report)
{
    if (!result.ok) {
        return BuildGenericErrorResponseJson(result.message);
    }
    nlohmann::json body;
    body["success"] = true;
    body["message"] = result.message;
    body["state"] = ClusterViewerReportToJson(report);
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string BuildPickResponseJson(bool hit, const std::string& entityName)
{
    nlohmann::json body;
    body["success"] = true;
    body["hit"] = hit;
    if (!entityName.empty()) {
        body["entity"] = entityName;
    }
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

ParsedCloneClustersRequest ParseCloneClustersRequest(const std::string& jsonBody)
{
    ParsedCloneClustersRequest result;

    nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded()) {
        result.errorMessage = "malformed JSON body";
        return result;
    }
    if (!parsed.is_object()) {
        result.errorMessage = "request body must be a JSON object";
        return result;
    }
    if (!parsed.contains("gta_path") || !parsed["gta_path"].is_string()
        || parsed["gta_path"].get<std::string>().empty()) {
        result.errorMessage = "missing or invalid required field: gta_path";
        return result;
    }
    result.gtaPath = parsed["gta_path"].get<std::string>();

    if (parsed.contains("weld") && !parsed["weld"].is_null()) {
        if (!parsed["weld"].is_string()) {
            result.errorMessage = "weld must be a string";
            return result;
        }
        const std::string weld = parsed["weld"].get<std::string>();
        if (weld != "auto" && weld != "exact" && weld != "position_only") {
            result.errorMessage = "weld must be \"auto\", \"exact\" or \"position_only\"";
            return result;
        }
        result.weld = weld;
    }
    parsed.erase("weld");

    if (parsed.contains("open_viewer") && !parsed["open_viewer"].is_null()) {
        if (!parsed["open_viewer"].is_boolean()) {
            result.errorMessage = "open_viewer must be a boolean";
            return result;
        }
        result.openViewer = parsed["open_viewer"].get<bool>();
    }

    ParsedBuildClustersRequest options;
    if (!ReadClusterOptionFields(parsed, options)) {
        result.errorMessage = options.errorMessage;
        return result;
    }
    result.maxVertices = options.maxVertices;
    result.maxTriangles = options.maxTriangles;
    result.coneWeight = options.coneWeight;
    result.valid = true;
    return result;
}

std::string BuildCloneClustersResponseJson(const CloneClustersResponseView& view)
{
    nlohmann::json body;
    body["success"] = true;
    body["created_path"] = view.createdPath;
    body["guid"] = view.guid;
    body["weld_used"] = view.weldUsed;
    body["weld_fell_back"] = view.weldFellBack;
    body["vertex_count_before"] = view.vertexCountBefore;
    body["vertex_count_after"] = view.vertexCountAfter;
    body["triangle_count"] = view.triangleCount;
    body["cluster_count"] = view.clusterCount;
    body["avg_triangles_per_cluster"] = view.avgTrianglesPerCluster;
    body["milliseconds"] = view.milliseconds;
    body["message"] = view.message;
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string BuildSceneCameraSelectResponseJson(const std::string& entityName)
{
    nlohmann::json body;
    body["success"] = true;
    body["selected"] = entityName;
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string BuildSceneCameraStateJson(const SceneCameraReport& report)
{
    nlohmann::json cameras = nlohmann::json::array();
    for (const SceneCameraInfo& c : report.cameras) {
        cameras.push_back(nlohmann::json::object({
            { "entity", c.entityId },
            { "name", c.name },
            { "position", Xyz(c.position) },
            { "rotation_euler_degrees", Xyz(c.rotationEuler) },
            { "fov_y_degrees", c.fovYDegrees },
            { "near", c.nearZ },
            { "far", c.farZ },
            { "selected", c.selected },
            { "screen", nlohmann::json::object({ { "apex_x", c.apexScreenX }, { "apex_y", c.apexScreenY },
                                                   { "visible", c.apexVisible } }) },
        }));
    }
    nlohmann::json body;
    body["rendered"] = report.rendered;
    body["image"] = nlohmann::json::object({ { "width", report.imageWidth }, { "height", report.imageHeight } });
    body["selected_entity"] = report.selectedEntity;
    body["cameras"] = std::move(cameras);
    body["selected_frustum"] = SegmentsToJson(report.selectedFrustum);
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

} // namespace gte::Network
