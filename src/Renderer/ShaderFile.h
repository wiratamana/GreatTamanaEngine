#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gte {

// Reads a compiled SPIR-V file. Relative paths resolve against the executable directory first,
// then the working directory. Throws std::runtime_error if the file is missing, empty,
// not a multiple of 4 bytes, or does not start with the SPIR-V magic number.
std::vector<std::byte> ReadShaderFileBytes(const std::string& path);

} // namespace gte
