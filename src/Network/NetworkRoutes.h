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

// --- network-impl-5 campaign
// (PHASE1_NETWORK_ROUTES_REQUEST_PARSING_AND_RESPONSE_BUILDING.md) - real
// JSON request parsing/response building for the new POST /set_entity_trs
// and POST /instantiate_light endpoints (Phase 4). Same PURE, JSON-only,
// zero-Game/ECS/Renderer/Math dependency discipline as everything above -
// see this file's own header comment.

// Parsed, VALIDATED result of a POST /set_entity_trs request body.
// `valid == false` means `errorMessage` explains exactly why - every OTHER
// field is meaningless in that case. See ParseSetEntityTrsRequest()'s own
// doc comment below for the exact validation rules.
struct ParsedSetEntityTrsRequest {
    bool valid = false;
    std::string errorMessage;
    std::string name;

    // All-or-nothing (network-impl-5's Locked Design Decision #2 -
    // PHASE0_MASTER_STRATEGY.md): hasTranslation is only ever true when
    // "translation" was present (and non-null) AND supplied all of x/y/z as
    // numbers. An explicit JSON null is treated exactly like the key being
    // absent (hasTranslation stays false) - see this file's own Step 2 note.
    bool hasTranslation = false;
    float translationX = 0.0f;
    float translationY = 0.0f;
    float translationZ = 0.0f;

    // Euler degrees, (pitchX, yawY, rollZ) - matches
    // Quat::FromEulerDegrees()'s own parameter order/units exactly. There is
    // NO quaternion input field anywhere in this campaign - see
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.
    bool hasRotationEulerDegrees = false;
    float rotationPitchXDegrees = 0.0f;
    float rotationYawYDegrees = 0.0f;
    float rotationRollZDegrees = 0.0f;

    bool hasScale = false;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
};

// Parses `jsonBody` (the raw POST body) for POST /set_entity_trs.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. `jsonBody` must parse as valid JSON at all, and the top-level value
//      must be a JSON OBJECT - otherwise "malformed JSON body" (this EXACT
//      string, verbatim - no parser-provided detail appended; see this
//      file's own Step 2 note, and match
//      ParseInstantiatePrimitiveRequest()'s real, tested behavior, not its
//      own slightly-stale header comment) / "request body must be a JSON
//      object".
//   2. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name".
//   3. "translation" is OPTIONAL. If absent entirely, OR present but JSON
//      `null`, hasTranslation stays false and translationX/Y/Z stay at their
//      defaults (meaningless). If present and non-null, it must be a JSON
//      OBJECT and ALL THREE of its "x"/"y"/"z" members must be present and
//      JSON NUMBERS - ANY of them missing or non-numeric is a validation
//      FAILURE: "translation must be an object with numeric x, y, and z
//      fields" (deliberately all-or-nothing - see PHASE0's Locked Design
//      Decision #2 for why this is NOT the same per-axis-optional rule
//      ParseInstantiatePrimitiveRequest()'s own "world_position" field
//      uses; see this file's own Step 2 note for why null is treated as
//      "absent" here specifically, unlike "world_position").
//   4. "rotation_euler_degrees" is OPTIONAL, same all-or-nothing x/y/z rule
//      (including the same null-means-absent treatment) as "translation"
//      above - otherwise "rotation_euler_degrees must be an object with
//      numeric x, y, and z fields". There is no "rotation_quaternion" field
//      recognized anywhere in this function - if a caller sends one, it is
//      silently ignored as an unrecognized extra field (same
//      forward-compatible convention as everything else in this file), NOT
//      an error.
//   5. "scale" is OPTIONAL, same all-or-nothing x/y/z rule (including the
//      same null-means-absent treatment) as "translation" - otherwise
//      "scale must be an object with numeric x, y, and z fields".
// A request with valid == true and every hasX flag false (no
// translation/rotation_euler_degrees/scale key present at all, or all three
// explicitly null) is NOT an error - see PHASE0's Locked Design Decision #6
// (a harmless no-op, doubling as a de-facto "read the current transform"
// query once Phase 2/4 exist).
ParsedSetEntityTrsRequest ParseSetEntityTrsRequest(const std::string& jsonBody);

// Parsed, VALIDATED result of a POST /instantiate_light request body.
// `valid == false` means `errorMessage` explains exactly why - every OTHER
// field is meaningless in that case.
struct ParsedInstantiateLightRequest {
    bool valid = false;
    std::string errorMessage;

    // "" (absent from the request entirely) means "use the default light
    // type" - Game::InstantiateLight() (Phase 2) is what actually resolves
    // "" (and, case-insensitively, "directional") to a real light kind, and
    // rejects anything else - this function only validates the JSON SHAPE
    // (a string, if present at all), never the semantic value, exactly
    // mirroring ParseInstantiatePrimitiveRequest()'s own "shape" field
    // convention (see that function's own doc comment for the identical
    // reasoning).
    std::string lightType;

    // REQUIRED - see PHASE0's Locked Design Decision #4 (unlike
    // "lightType" above, an empty/missing "name" IS a validation failure
    // here, enforced at THIS layer).
    std::string name;

    // Per-axis-optional, defaulting to 0.0f for any missing axis - CREATION-
    // time semantics, identical convention to
    // ParseInstantiatePrimitiveRequest()'s own "world_position" field (this
    // is deliberately NOT the same all-or-nothing rule
    // ParseSetEntityTrsRequest() above uses for "translation" - see
    // PHASE0_MASTER_STRATEGY.md's Step 2 note on why creation-time fields
    // and mutation-time fields use different optionality rules). An
    // explicit "world_position": null is NOT treated as absent here -
    // it fails with "world_position must be an object", exactly matching
    // ParseInstantiatePrimitiveRequest()'s own real, pre-existing behavior
    // for this same field (the new null-means-absent convention introduced
    // by this phase applies only to the brand-new all-or-nothing groups,
    // never retroactively to this pre-existing field - see this file's own
    // Step 2 note).
    float worldX = 0.0f;
    float worldY = 0.0f;
    float worldZ = 0.0f;

    // All-or-nothing, same rule (and same field meaning/units, including the
    // null-means-absent treatment) as ParseSetEntityTrsRequest()'s own
    // "rotation_euler_degrees" above - see that struct's own doc comment.
    // Absent entirely (or explicitly null) -> false (Phase 2's
    // Game::InstantiateLight() then applies the "late-afternoon" default
    // rotation per PHASE0's Locked Design Decision #5, NOT identity).
    bool hasRotationEulerDegrees = false;
    float rotationPitchXDegrees = 0.0f;
    float rotationYawYDegrees = 0.0f;
    float rotationRollZDegrees = 0.0f;

    // Per-component-optional, defaulting to 1.0f for any missing component -
    // CREATION-time semantics matching DirectionalLight::color's own default
    // (Vec3::One()) - see ECS/Components/DirectionalLight.h. Same
    // "explicit null is NOT treated as absent" caveat as "world_position"
    // above (an explicit "color": null fails with "color must be an
    // object" - see below).
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;

    // Optional; defaults to DirectionalLight::illuminanceLux's own default
    // (100000.0f) when absent. If present, MUST be a non-negative JSON
    // number - a negative value is a validation failure ("illuminance_lux
    // must be a non-negative number") since a negative light intensity is
    // physically meaningless and almost certainly a caller mistake worth
    // surfacing loudly rather than silently accepting.
    float illuminanceLux = 100000.0f;

    // Optional bool; defaults to DirectionalLight::active's own default
    // (true) when absent. If present, must be a JSON boolean - otherwise
    // "active must be a boolean".
    bool active = true;

    // Same "absent/null/empty string all mean no parent; any other JSON
    // type is a validation failure" rule as
    // ParseInstantiatePrimitiveRequest()'s own "parent" field - reused
    // verbatim, see that function's own doc comment (point 5) for the
    // exact rule.
    bool hasParent = false;
    std::string parentName;
};

// Parses `jsonBody` (the raw POST body) for POST /instantiate_light.
// Validation rules (checked in this order - the FIRST failure found is what
// `errorMessage` reports):
//   1. Same "must parse as a JSON object" rule as
//      ParseSetEntityTrsRequest() above - including the same EXACT
//      "malformed JSON body" literal string (see this file's own Step 2
//      note; do not append any parser-provided detail).
//   2. "light_type" is OPTIONAL; if present, must be a JSON STRING (may be
//      empty) - otherwise "light_type must be a string". The actual
//      "directional"-or-nothing-else semantic check happens in Phase 2's
//      Game::InstantiateLight(), not here (see ParsedInstantiateLightRequest::lightType's
//      own doc comment above).
//   3. "name" must be present, a JSON STRING, and non-empty - otherwise
//      "missing or invalid required field: name" (identical message text
//      to ParseInstantiatePrimitiveRequest()'s own, for consistency).
//   4. "world_position" is OPTIONAL, per-axis-optional-defaults-to-0 - same
//      rule/validation-failure message shape as
//      ParseInstantiatePrimitiveRequest()'s own "world_position" field
//      (point 4 of that function's own doc comment, including its exact
//      "world_position must be an object"/"world_position.x/y/z must be a
//      number" message text and its existing null-is-not-treated-specially
//      behavior).
//   5. "rotation_euler_degrees" is OPTIONAL, all-or-nothing x/y/z rule
//      (including null-means-absent) - identical shape/message convention
//      to ParseSetEntityTrsRequest()'s own field of the same name.
//   6. "color" is OPTIONAL; if present and non-null, must be a JSON OBJECT -
//      otherwise "color must be an object" (mirrors "world_position must be
//      an object" exactly, including that an explicit "color": null is NOT
//      treated as absent - see ParsedInstantiateLightRequest::colorR's own
//      doc comment above). Each of its "r"/"g"/"b" members is itself
//      OPTIONAL (missing -> 1.0f for that component) but if present must be
//      a JSON NUMBER - otherwise the exact message "color.r must be a
//      number" / "color.g must be a number" / "color.b must be a number"
//      (three DISTINCT message strings, one per component that actually
//      failed - mirroring "world_position.x must be a number"'s own
//      per-axis-specific message shape exactly, never one message with a
//      literal "r/g/b" substring in it).
//   7. "illuminance_lux" is OPTIONAL; if present, must be a JSON NUMBER
//      that is >= 0 - otherwise "illuminance_lux must be a non-negative
//      number".
//   8. "active" is OPTIONAL; if present, must be a JSON BOOLEAN - otherwise
//      "active must be a boolean".
//   9. "parent" is OPTIONAL - identical rule to
//      ParseInstantiatePrimitiveRequest()'s own "parent" field (point 5 of
//      that function's own doc comment) - reuse that exact logic.
// Unrecognized extra JSON fields are silently ignored, same
// forward-compatible convention as everywhere else in this file.
ParsedInstantiateLightRequest ParseInstantiateLightRequest(const std::string& jsonBody);

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
//
// network-impl-5 campaign: POST /instantiate_light's route handler (Phase 4)
// calls this SAME function for its own success/failure response - the two
// endpoints' response shapes (spawn a named, possibly-parented entity,
// report the same five pieces of information) are intentionally identical,
// so no second, dedicated "BuildInstantiateLightResponseJson()" was written.
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

// A plain, Math/Vec3/Quat-free snapshot of one entity's resulting local
// transform, for BuildSetEntityTrsResponseJson()'s own response body below.
// NetworkServer.cpp (Phase 4) is the one place that copies a real
// SetEntityTrsOutcome's Vec3/Quat fields (src/Game/EngineCommandResults.h,
// Phase 2) into this struct, one field at a time - same "a struct crossing
// a layer boundary is never accepted directly here" boundary
// TextureListEntryView's own doc comment already establishes for this file.
// The rotation is intentionally echoed in BOTH representations (Euler
// degrees for human readability, raw quaternion for a caller that wants the
// exact, non-lossy value) - this is READ-ONLY OUTPUT, so it creates none of
// the input ambiguity PHASE0's Locked Design Decision #1 rules out for
// INPUT.
//
// NOTE ON NAMING: this struct's field is "positionX/Y/Z" (matching
// Transform::position's own field name, ECS/Components/Transform.h) even
// though the REQUEST'S corresponding JSON key/struct field is
// "translation"/"translationX/Y/Z" (ParsedSetEntityTrsRequest above). This
// asymmetry is deliberate, not an inconsistency to "fix": the request key
// describes an ACTION ("translate this entity by/to..."), while the
// response key describes the entity's actual CURRENT STATE (its Transform's
// real "position" field) - matching each one's own most natural name in its
// own context, exactly like the request's "world_position" (an
// instantiate-time placement) and this struct's own "positionX/Y/Z" (a
// read-back of live component state) already differ in
// ParsedInstantiatePrimitiveRequest/DirectionalLight elsewhere in this
// codebase. Do not rename either side to "fix" this - it would only make
// one of the two contexts read less naturally.
struct TransformSnapshotView {
    float positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;
    float rotationEulerXDegrees = 0.0f, rotationEulerYDegrees = 0.0f, rotationEulerZDegrees = 0.0f;
    float rotationQuatX = 0.0f, rotationQuatY = 0.0f, rotationQuatZ = 0.0f, rotationQuatW = 1.0f;
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
};

// Builds the response body for POST /set_entity_trs.
// Success shape:
//   {"success":true,"entity":{"index":<uint>,"generation":<uint>},
//    "changed":{"translation":<bool>,"rotation":<bool>,"scale":<bool>},
//    "transform":{
//      "position":{"x":..,"y":..,"z":..},
//      "rotation_euler_degrees":{"x":..,"y":..,"z":..},
//      "rotation_quaternion":{"x":..,"y":..,"z":..,"w":..},
//      "scale":{"x":..,"y":..,"z":..}}}
// Failure shape: identical to BuildGenericErrorResponseJson() below -
// {"success":false,"error":"<errorMessage>"} - via an internal `if
// (!success) { return BuildGenericErrorResponseJson(errorMessage); }` early
// return, EXACTLY mirroring BuildInstantiatePrimitiveResponseJson()'s own
// real, verified implementation (confirmed directly against
// NetworkRoutes.cpp - not just its header comment) - never a second,
// parallel builder function. On failure, entityIndex/entityGeneration/
// translationChanged/rotationChanged/scaleChanged/resultingTransform are
// all ignored - only errorMessage is read. This function is only ever
// called by /set_entity_trs's own SUCCESS path (Phase 4); Phase 4's route
// handler calls BuildGenericErrorResponseJson() DIRECTLY for its own
// failure path instead (the exact same split
// BuildInstantiatePrimitiveResponseJson()'s own callers already use) -
// this function's own internal `!success` branch above exists purely so
// its signature/behavior matches its sibling builders' shape exactly, not
// because Phase 4 is expected to rely on it for the failure case.
std::string BuildSetEntityTrsResponseJson(bool success, const std::string& errorMessage,
    std::uint32_t entityIndex, std::uint32_t entityGeneration,
    bool translationChanged, bool rotationChanged, bool scaleChanged,
    const TransformSnapshotView& resultingTransform);

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
