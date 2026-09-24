#pragma once

#include <cstddef>
#include <cstring>
#include <string>

// editor-core-separation-4 campaign, PHASE6
// (PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md) - GtePluginModuleInfo's
// fixed-size char[] buffers (plugins/gte_plugin_abi/GtePluginModuleInfo.h)
// are NOT mechanically guaranteed to be null-terminated by the ABI contract
// itself (only by convention - see that struct's own doc comment). A
// plugin the engine's own authors did not write could write a
// non-terminated buffer, and this engine has no way to verify that in
// advance. strnlen(buf, sizeof(buf)) is the one correct, always-safe way to
// read it: it never reads past the buffer's own known, fixed size,
// regardless of whether a null terminator is actually present inside it.
//
// Header-only/inline (rather than a private helper hidden inside
// PluginHost.cpp) so this bounded-read logic is directly includable and
// Tier-1-testable from tests/Core/FixedBufferReaderTests.cpp - see this
// phase's own completion report for why option (a) was chosen over keeping
// it private.
namespace gte {

inline std::string ReadFixedBuffer(const char* buffer, std::size_t bufferSize)
{
    return std::string(buffer, strnlen(buffer, bufferSize));
}

} // namespace gte
