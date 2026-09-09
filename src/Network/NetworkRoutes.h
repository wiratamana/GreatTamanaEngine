#pragma once

#include <string>

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

} // namespace gte::Network
