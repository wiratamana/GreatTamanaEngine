#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace gte::Profiling {

// Trims a compiler function signature to "Namespace::Class::Method".
// Writes at most out.size() - 1 bytes plus a terminator and returns the byte count. Never allocates.
std::size_t TrimFunctionName(std::string_view signature, std::span<char> out) noexcept;

} // namespace gte::Profiling
