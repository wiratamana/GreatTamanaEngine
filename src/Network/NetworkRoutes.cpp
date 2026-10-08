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

#include <cmath>
#include <mutex>

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

// --- GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
// PHASE6 (task_manager/render-pass-5/PHASE6_EDITOR_TOOLING_AND_LIVE_VALIDATION.md)
// - POST /spawn_gpu_driven_test_batch. See NetworkRoutes.h's own doc comment
// above ParseSpawnGpuDrivenTestBatchRequest() for the exact, locked
// validation rules implemented below.

ParsedSpawnGpuDrivenTestBatchRequest ParseSpawnGpuDrivenTestBatchRequest(const std::string& jsonBody)
{
    ParsedSpawnGpuDrivenTestBatchRequest result;

    // Same DELIBERATE EXCEPTION ParseScenePathRequest() above documents -
    // this endpoint's own single-optional-field shape makes "no body at
    // all" a completely reasonable, common request ("spawn the
    // default-sized batch"), not a malformed one.
    const nlohmann::json parsed = ParseJsonNoThrow(jsonBody);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.valid = true;
        return result;
    }

    if (parsed.contains("count") && !parsed["count"].is_null()) {
        if (!parsed["count"].is_number()) {
            result.errorMessage = "count must be a positive integer";
            return result;
        }
        const double asDouble = parsed["count"].get<double>();
        if (asDouble < 1.0 || asDouble != std::floor(asDouble)) {
            result.errorMessage = "count must be a positive integer";
            return result;
        }
        result.instanceCount = static_cast<std::uint32_t>(asDouble);
    }

    result.valid = true;
    return result;
}

std::string BuildSpawnGpuDrivenTestBatchResponseJson(
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

} // namespace gte::Network
