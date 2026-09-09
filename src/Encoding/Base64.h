#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte::Encoding {

// Encodes `size` bytes at `data` as standard base64 text (RFC 4648, '+'/'/'
// alphabet with '=' padding - the same alphabet every common base64
// consumer, e.g. a browser's `atob()`/an LLM tool's own base64 image
// decoder, expects by default). Returns an empty string for size == 0.
// `data` may be nullptr only when size == 0.
//
// Hand-rolled deliberately, matching this codebase's general "roll it
// ourselves when the shape is this small" philosophy already established for
// math/ECS/JSON (see AGENTS.md) - the only other base64 encoder anywhere in
// this repo is `httplib::detail::base64_encode`, an internal implementation
// detail of a vendored third-party library, not something engine code should
// reach into.
std::string EncodeBase64(const std::uint8_t* data, std::size_t size);

// Convenience overload for a std::vector<uint8_t> (e.g. PngEncoder.h's own
// output) - the shape every real call site in this campaign actually has.
std::string EncodeBase64(const std::vector<std::uint8_t>& bytes);

} // namespace gte::Encoding
