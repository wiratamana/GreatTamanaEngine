// src/Editor/ScreenPassAutoWire.h
//
// editor-core-separation-24 campaign ("Project Assembly On-Screen Render
// Feature Compositing", BIG-STEP 2 - Editor Integration), PHASE4
// (PHASE4_AUTO_WIRE_HELPER_TRY_AUTO_WIRE_REGISTER_CALL.md).
//
// Declared here, in its OWN header/source pair, with EXTERNAL linkage - NOT
// inside EditorProjectLifecycleCapability.cpp's own anonymous namespace. This
// is not a style preference: tests/Editor/ScreenPassAutoWireTests.cpp needs
// to call TryAutoWireRegisterCall() directly, by name, from a separate
// translation unit, which is only possible with external linkage
// (PHASE0_MASTER_STRATEGY.md, Locked Decision 16).
#pragma once

#include <filesystem>
#include <string>

namespace gte {

// Reads the real, on-disk `<ProjectName>Game.cpp` file at `projectGameCppPath`
// and, if BOTH of PHASE2's two required anchor comments
// ("GTE_AUTO_REGISTER_FORWARD_DECLARATIONS" / "GTE_AUTO_REGISTER_ANCHOR") are
// present, inserts:
//   - "void " + registerFunctionName + "(gte::Core& core);" immediately below
//     the forward-declarations anchor.
//   - "    " + registerFunctionName + "(core);" immediately below the
//     RegisterProject()-body anchor.
//
// Returns true if both insertions were made (or were ALREADY present and
// actively wired - a safe, idempotent no-op) - false if the file cannot be
// opened at all, or if EITHER anchor is missing, in which case the target
// file is left COMPLETELY, BYTE-FOR-BYTE UNTOUCHED (never a half-patched
// file - this is an all-or-nothing operation).
//
// `registerFunctionName` - e.g. "RegisterFooScreenPass" (PascalCase, matching
// every other generated symbol's own naming convention).
bool TryAutoWireRegisterCall(const std::filesystem::path& projectGameCppPath,
    const std::string& registerFunctionName);

} // namespace gte
