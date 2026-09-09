#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gte::Network {

// Pure, httplib-independent route handler logic - Tier 1 testable (see
// tests/Network/NetworkRoutesTests.cpp), no live httplib::Server/socket/
// thread involved at all. NetworkServer.cpp (Phase 2) is only ever a thin
// wiring layer that calls these functions and forwards their result into
// httplib::Response::set_content() - it must never compose response text
// itself. Every future endpoint's own response-computation logic must be
// added here the same way, as its own small function, so it stays testable
// the same way.
//
// Returns the exact response body for GET /http_hello_world - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract" for the exact
// expected bytes (no trailing newline).
std::string HandleHelloWorld();

// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - the response-
// format-negotiation logic shared by BOTH /get_game_view and (Phase 5)
// /get_swapchain.
enum class CaptureResponseFormat { RawPng, JsonBase64 };

// Resolves which shape a capture endpoint's response should take, given the
// request's own `?format=` query parameter value (empty string if absent)
// and `Accept` header value (empty string if absent) - see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #3 for the exact,
// locked precedence rules this implements:
//   - queryFormat == "png"            -> RawPng
//   - queryFormat == "base64"/"json"  -> JsonBase64
//   - queryFormat is anything else non-empty -> RawPng (an unrecognized
//     value is NOT an error - falls back to the default, same spirit as
//     this engine's other "unknown-value falls back to a safe default"
//     precedents, e.g. GpuTimingSample's own tri-state resolution)
//   - queryFormat is empty AND acceptHeader contains "application/json"
//     (a simple substring check - real Accept headers can have multiple,
//     weighted values; this engine only ever needs the simple case) -> JsonBase64
//   - otherwise (queryFormat empty, Accept doesn't ask for JSON) -> RawPng
CaptureResponseFormat ResolveCaptureResponseFormat(const std::string& queryFormat, const std::string& acceptHeader);

// Builds the JSON body for the JsonBase64 response shape - a small, fixed,
// hand-formatted JSON object (see PHASE0_MASTER_STRATEGY.md's own
// "no JSON library" decision):
// {"width":<int>,"height":<int>,"format":"png","data_base64":"<...>"}
// `base64Png` must already be valid base64 text (see Encoding::EncodeBase64)
// - this function does no escaping of it (base64's own alphabet contains no
// character that needs JSON-string escaping).
std::string BuildCaptureJsonBody(int width, int height, const std::string& base64Png);

// --- network-impl-3 campaign
// (PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md) - real JSON request
// parsing/response building for the new POST /instantiate_primitive and
// POST /delete_entity endpoints (Phase 5). Implemented via nlohmann::json
// (see cmake/FetchJson.cmake) - the FIRST place in this engine that parses
// genuinely untrusted, possibly-malformed JSON text rather than only ever
// emitting a few known-safe fields (BuildCaptureJsonBody() above hand-
// formats safely only because it carries just integers/base64 text - that
// trick does not generalize to arbitrary caller-supplied entity/parent
// names, which can contain characters that need real JSON-string escaping).
// Every function below stays PURE - no httplib/socket/thread/Registry/Game/
// Renderer dependency of any kind, exactly like everything above.

// Parsed, VALIDATED result of a POST /instantiate_primitive request body.
// `valid == false` means `errorMessage` explains exactly why - every OTHER
// field is meaningless in that case. See ParseInstantiatePrimitiveRequest()'s
// own doc comment below for the exact validation rules.
struct ParsedInstantiatePrimitiveRequest {
    bool valid = false;
    std::string errorMessage;
    std::string shape;
    std::string name;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;
    bool hasParent = false;
    std::string parentName; // meaningful only when hasParent is true
};

// Parses `jsonBody` (the raw POST body) for POST /instantiate_primitive.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. `jsonBody` must parse as valid JSON at all, and the top-level value
//      must be a JSON OBJECT (not an array/string/number/etc) - otherwise
//      "malformed JSON body: <parser's own message>" / "request body must be
//      a JSON object".
//   2. "shape" must be present, a JSON STRING, and non-empty after parsing -
//      otherwise "missing or invalid required field: shape". This function
//      does NOT itself validate the shape NAME is a recognized PrimitiveType
//      (cube/sphere/capsule/cone/plane) - that is
//      PrimitiveMeshGenerator::TryParsePrimitiveTypeName()'s job (Phase 2),
//      called later by Game::InstantiatePrimitive() (Phase 3). This function
//      only validates the JSON SHAPE of the request, never its semantic
//      meaning - keeps this function's own test suite independent of
//      PrimitiveType ever gaining/losing a value.
//   3. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name".
//   4. "world_position" is OPTIONAL. If absent entirely, worldX/Y/Z all
//      default to 0.0f. If present, it must be a JSON OBJECT; each of its
//      "x"/"y"/"z" members is itself OPTIONAL (missing -> 0.0f for that axis)
//      but if present must be a JSON NUMBER (otherwise "world_position.x/y/z
//      must be a number").
//   5. "parent" is OPTIONAL. Absent entirely, JSON null, OR an empty string
//      all mean "no parent requested" (hasParent = false, parentName left
//      empty). Any other JSON STRING means hasParent = true, parentName = that
//      string. Any other JSON type (number/bool/object/array) for "parent" is
//      a validation failure: "parent must be a string or null".
// Unrecognized extra JSON fields are silently ignored (forward-compatible -
// a future client sending an extra field never breaks an older engine build).
ParsedInstantiatePrimitiveRequest ParseInstantiatePrimitiveRequest(const std::string& jsonBody);

// Parsed, VALIDATED result of a POST /delete_entity request body:
// `{"name": "..."}`. Same "valid == false means errorMessage explains why"
// contract as above. Validation: jsonBody must parse as a JSON object;
// "name" must be present, a JSON string, and non-empty - otherwise "missing
// or invalid required field: name".
struct ParsedDeleteEntityRequest {
    bool valid = false;
    std::string errorMessage;
    std::string name;
};
ParsedDeleteEntityRequest ParseDeleteEntityRequest(const std::string& jsonBody);

// Response-JSON builders for the two new endpoints (Phase 5's actual route
// handlers call these). Deliberately take only PLAIN SCALAR parameters -
// never a Game-layer/EngineCommandBridge struct type - so this file keeps its
// existing "pure, httplib-independent, and now also completely Game/ECS-
// independent" contract from its own file header comment. Built via
// nlohmann::json (correct string escaping) rather than hand-formatted like
// BuildCaptureJsonBody() above, because an entity/parent NAME - unlike a
// base64 image or a plain integer - can legitimately contain characters that
// need real JSON-string escaping (a quote, a backslash, ...).
//
// Success shape:
//   {"success":true,"entity":{"index":<uint>,"generation":<uint>},
//    "name":"<resolvedName>",
//    "parent_requested_but_not_found":<bool>,
//    "requested_parent_name":"<...>"}   (only meaningful/non-empty when the
//                                        previous field is true)
// Failure shape (BuildGenericErrorResponseJson below): {"success":false,"error":"<message>"}
std::string BuildInstantiatePrimitiveResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration, const std::string& resolvedName,
    bool parentRequestedButNotFound, const std::string& requestedParentName);

// Success shape: {"success":true,"entity":{"index":<uint>,"generation":<uint>}}
// Failure shape: identical to BuildGenericErrorResponseJson() below.
std::string BuildDeleteEntityResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t deletedEntityIndex, std::uint32_t deletedEntityGeneration);

// Shared failure-shape builder used by BOTH new endpoints AND any future one
// (malformed JSON, bridge unavailable/busy, timeout - see Phase 5):
// {"success":false,"error":"<errorMessage>"}
std::string BuildGenericErrorResponseJson(const std::string& errorMessage);

// --- network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// GET /get_texture and GET /list_textures. Every function below stays PURE,
// same discipline as everything above.

// Parsed, validated GET /get_texture query parameters. `valid == false`
// means `errorMessage` explains exactly why (a 400 response - see
// NetworkServer.cpp's own route lambda) - every other field is meaningless
// in that case.
struct ParsedGetTextureQuery {
    bool valid = false;
    std::string errorMessage;
    std::string textureName;
    // Mirrors FrameCaptureBridge's own DebugTextureChannel (Phase 4) -
    // NetworkRoutes.h deliberately does NOT #include FrameCaptureBridge.h
    // (this file's own existing convention - see its header comment:
    // "completely Game/ECS-independent" - FrameCaptureBridge lives under
    // src/Application/, one layer further from pure than this file wants to
    // depend on), so this is its OWN small, parallel bool instead of
    // reusing that enum directly - NetworkServer.cpp's route lambda is
    // what converts `wantsDepth` into the real
    // FrameCaptureBridge::DebugTextureChannel value at the one call site
    // that already depends on both headers anyway.
    bool wantsDepth = false;
};

// Validation rules: "texture_name" must be present and non-empty -
// otherwise "missing or empty required query parameter: texture_name".
// "channel" is OPTIONAL; absent or exactly "color" -> wantsDepth = false;
// exactly "depth" -> wantsDepth = true; any OTHER non-empty value ->
// "invalid channel - must be \"color\" or \"depth\"" (a validation
// FAILURE, unlike ResolveCaptureResponseFormat()'s own "unrecognized value
// falls back to a default" convention - a typo'd channel name is much more
// likely to be a caller MISTAKE worth surfacing loudly than a forward-
// compatible "ignore it" case, since guessing wrong here would otherwise
// silently return the WRONG channel's image with no error at all).
// NOTE: matching is EXACT-CASE ("color"/"depth" only, never "Color"/"DEPTH")
// - mirrors ResolveCaptureResponseFormat()'s own exact-lowercase-only
// matching in this same file; this is a deliberate, consistent choice
// across every query-parameter parser in this file, not an oversight - do
// not add case-insensitive matching here without doing the same everywhere
// else in this file first.
ParsedGetTextureQuery ParseGetTextureQuery(const std::string& textureNameParam, const std::string& channelParam);

// Builds GET /get_texture's own JSON/base64 response body (used only when
// CaptureResponseFormat::JsonBase64 is resolved - see
// ResolveCaptureResponseFormat(), reused unchanged from Phase 3 of
// network-impl-2):
// {"width":<int>,"height":<int>,"format":"png","data_base64":"<...>","frames_since_update":<uint>}
// NOTE: this response's OWN "format" field is always the literal string
// "png" - the PNG *encoding*, exactly like BuildCaptureJsonBody()'s
// existing field of the same name. Do NOT confuse this with
// TextureListEntryView::format below, which is the SOURCE texture's
// VkFormat (e.g. "B8G8R8A8_UNORM") - the two are unrelated concepts that
// simply happen to share a JSON key name in two different response shapes.
// Built via nlohmann::json (see this file's own Step 2 note) - NOT
// BuildCaptureJsonBody() (which stays exactly as /get_game_view/
// /get_swapchain need it, untouched by this campaign).
std::string BuildTextureCaptureJsonBody(
    int width, int height, const std::string& base64Png, std::uint64_t framesSinceUpdate);

// One entry of GET /list_textures's own JSON array - see
// BuildListTexturesResponseJson() below. A plain, scalars-only struct THIS
// file owns (see this file's own Step 2 note on why a struct crossing a
// layer boundary is never accepted directly here) - NetworkServer.cpp is
// the one place that copies gte::PublishedTextureListEntry
// (src/Application/FrameCaptureBridge.h, Phase 5's own addition there - see
// Step 3.3 below) into this struct, one field at a time.
struct TextureListEntryView {
    std::string name;
    std::string regime; // "synchronous" or "pipelined" - already resolved to a string upstream (Application::Run(), Step 3.4) - this file never sees rg::ExecuteTimingMode.
    std::string format;  // e.g. "B8G8R8A8_UNORM" - the texture's COLOR VkFormat, already resolved to a string upstream - see this file's own note on BuildTextureCaptureJsonBody() above for why this is a DIFFERENT "format" concept than that function's own field of the same name.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasDepth = false;
    std::uint64_t framesSinceUpdate = 0;
};

// Builds the full GET /list_textures response body:
// {"textures":[{"name":"GameView","regime":"synchronous","format":"B8G8R8A8_UNORM","width":1280,"height":720,"has_depth":true,"frames_since_update":0}, ...]}
// An empty `entries` produces {"textures":[]}, never an error - a session
// where nothing has rendered a single named texture yet (e.g. queried
// immediately at startup, before the first frame) is a valid, normal state.
std::string BuildListTexturesResponseJson(const std::vector<TextureListEntryView>& entries);

} // namespace gte::Network
