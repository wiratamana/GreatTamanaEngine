#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

// Reads a compiled SPIR-V file; relative paths resolve against the executable directory only.
// Throws std::runtime_error if the file is missing, empty, not 4-byte aligned or lacks the SPIR-V magic.
std::vector<std::byte> ReadShaderFileBytes(const std::string& path);

} // namespace gte
