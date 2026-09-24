#pragma once

#include <cstdint>

// The frozen, versioned handshake every plugin .dll and the host process
// both compile against - see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #5 and the source design doc's Section 8. A fixed-size POD,
// safe to read via GetProcAddress()+call, safe to memcmp() byte-for-byte.
// NEVER add a non-trivial member (std::string, a pointer, a vtable) to this
// struct - every field must be a fixed-size, trivially-copyable primitive.
namespace gte {

struct GtePluginAbiFingerprint {
    // Bumped BY HAND, in this exact header, whenever gte_plugin_abi itself
    // changes in a way that isn't purely additive (an existing interface's
    // method signature changes, a struct field is removed/reordered, ...).
    // Adding a brand-new interface/capability version string is NOT a
    // reason to bump this - see docs/conventions/plugin-architecture.md.
    std::uint32_t abiContractGeneration;

    // "GNU" for GCC/MinGW (this repository's only supported toolchain
    // today - see CMakeLists.txt's own WIN32-only gate). Not
    // null-terminated beyond its own fixed size if the id is ever exactly
    // 15 characters - always compare with memcmp() over the full fixed
    // width, never strcmp(), so a non-terminated edge case can never read
    // out of bounds.
    char compilerId[16];

    // From CMAKE_CXX_COMPILER_VERSION, split into three integers (baked in
    // via configure_file() at CMake configure time - see this phase's own
    // CMakeLists.txt changes below). A mismatch in ANY of the three fields
    // is a real fingerprint mismatch - this project does not attempt to
    // reason about "close enough" compiler versions, per the source design
    // doc's own Section 0.1 "no interpretation, no close enough" rule.
    std::uint32_t compilerVersionMajor;
    std::uint32_t compilerVersionMinor;
    std::uint32_t compilerVersionPatch;

    // "Debug" / "Release" / "RelWithDebInfo" / "MinSizeRel" - from
    // CMAKE_BUILD_TYPE at configure time. A Debug-built plugin loaded by a
    // Release-built host (or vice versa) is a real, historically-common
    // source of silent ODR/struct-layout mismatches (different assert()/
    // iterator-debugging settings) - refused, not merely warned about.
    char buildConfig[16];

    // sizeof(void*) at compile time - a cheap, first-line sanity guard
    // against a genuinely wrong-architecture .dll (e.g. an accidental
    // 32-bit build) being loaded into a 64-bit host process, independent of
    // (and checked before) every other field.
    std::uint32_t pointerSize;

    // 1 if this binary was built with shared (DLL) libgcc/libstdc++
    // linkage, 0 if statically linked - see the source design doc's Section
    // 8.1 ("Iron Rule") and PHASE0_MASTER_STRATEGY.md's Locked Design
    // Decision #4. Compared for EQUALITY (like every other field here) by
    // operator== below - a MISMATCH between host and plugin is refused like
    // any other fingerprint mismatch. Honest correction (editor-core-
    // separation-4 campaign, PHASE1): earlier documentation for this field
    // claimed the host additionally refuses to load ANY plugin outright
    // whenever its OWN fingerprint has this field read as 0 - THAT
    // STANDALONE REFUSAL DOES NOT EXIST IN THIS CODE, and is not implemented
    // by this phase either (this development machine's only usable
    // toolchain cannot produce a shared-CRT binary at all - see
    // cmake/MingwRuntime.cmake's own top-of-file comment - so a literal
    // refusal would disable plugin loading entirely here). What DOES exist,
    // as of this phase: PluginHost::LoadPlugins() logs a loud, one-time
    // GTE_LOG_WARNING whenever the HOST's own sharedRuntimeLinkage reads 0,
    // naming the real, ongoing risk explicitly (statically-linked host and
    // plugins do not share one process-wide heap - any future capability
    // that transfers heap ownership across the ABI boundary is undefined
    // behavior under this condition) - see PluginHost.cpp's own
    // LogSharedCrtRiskWarningOnce() for the exact wording. Treat this field
    // as "checked for equality, and its host-side value is loudly warned
    // about when 0" - not "a hard gate," until a dedicated future decision
    // implements a real hard gate (e.g. once this repo's active toolchain is
    // switched to a shared-CRT-capable one and this becomes enforceable
    // without disabling plugin loading altogether).
    std::uint32_t sharedRuntimeLinkage;
};

// Byte-for-byte comparison - the ONLY correct way to compare two
// fingerprints (never compare field-by-field with any "is this close
// enough" logic - see the struct's own top comment).
inline bool operator==(const GtePluginAbiFingerprint& a, const GtePluginAbiFingerprint& b) noexcept
{
    return a.abiContractGeneration == b.abiContractGeneration
        && a.compilerVersionMajor == b.compilerVersionMajor
        && a.compilerVersionMinor == b.compilerVersionMinor
        && a.compilerVersionPatch == b.compilerVersionPatch
        && a.pointerSize == b.pointerSize
        && a.sharedRuntimeLinkage == b.sharedRuntimeLinkage
        && __builtin_memcmp(a.compilerId, b.compilerId, sizeof(a.compilerId)) == 0
        && __builtin_memcmp(a.buildConfig, b.buildConfig, sizeof(a.buildConfig)) == 0;
}

} // namespace gte
