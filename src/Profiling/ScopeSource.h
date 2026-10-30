#pragma once

#include "ScopeEvent.h"

#include <cstdint>
#include <string_view>

namespace gte::Profiling {

enum class ScopeOrigin : std::uint8_t {
    Unknown,
    Engine,
    Project,
};

// Where a scope name was first written. `fileName` is an interned basename.
struct ScopeSource {
    NameId fileName = kInvalidNameId;
    std::uint32_t line = 0;
    ScopeOrigin origin = ScopeOrigin::Unknown;
};

// Checkout directory of the engine with a trailing separator, derived from this library's own source path.
// Empty when that path has an unexpected shape.
std::string_view EngineRoot() noexcept;

// Under `engineRoot`: src/ is Engine, Projects/ is Project, anything else is Unknown.
// Outside it (or with an empty root): a "/Projects/" folder is Project, else a "/src/" folder is Engine.
ScopeOrigin OriginOfPath(std::string_view path, std::string_view engineRoot) noexcept;

// Same as InternName, and remembers the call site. The first site wins. Copies the text, never keeps `file`.
NameId InternNameAt(const char* text, const char* file, std::uint32_t line) noexcept;

// Trims a compiler function signature, then InternNameAt. An empty result becomes "<unknown function>".
NameId InternFunctionName(const char* signature, const char* file, std::uint32_t line) noexcept;

// Lock-free. Returns an empty source for an unknown id.
ScopeSource SourceOf(NameId id) noexcept;

} // namespace gte::Profiling
