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
