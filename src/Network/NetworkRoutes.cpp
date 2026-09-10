#include "NetworkRoutes.h"

#include <nlohmann/json.hpp>

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
        arr.push_back(std::move(item));
    }
    nlohmann::json body;
    body["textures"] = std::move(arr);
    return body.dump();
}

} // namespace gte::Network
