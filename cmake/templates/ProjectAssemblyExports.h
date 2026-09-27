// ProjectAssemblyExports.h
//
// Copied verbatim into every Project Assembly's own Libraries/ folder -
// canonical source: cmake/templates/ProjectAssemblyExports.h.
// editor-core-separation-11 campaign (Project Assembly system), PHASE3.
// Every _Game.dll/_Editor.dll exports exactly ONE fixed, fingerprint-free
// entry point - no ABI-versioning ceremony needed (these two binaries are
// guaranteed same-toolchain/same-build by construction, unlike a
// gte_plugin_abi .dll).
#pragma once

namespace gte { class Core; class EditorHost; }

// Use this macro in exactly ONE .cpp file per _Game.dll/_Editor.dll target.
// TWO variants exist (never one, conditionally-parameterized) because C++
// preprocessor macros cannot conditionally add/remove a function parameter
// based on which target is building - see
// PHASE5_PROJECTASSEMBLYHOST_RUNTIME_LOADER.md for exactly how
// ProjectAssemblyHost resolves and calls this export, and why the _Game
// vs. _Editor parameter-count difference is real and load-bearing, not
// cosmetic.
#define GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterFn) \
    extern "C" __declspec(dllexport) void GTE_RegisterProject(gte::Core& core) { RegisterFn(core); }

#define GTE_DEFINE_PROJECT_EXPORTS_EDITOR(RegisterFn) \
    extern "C" __declspec(dllexport) void GTE_RegisterProject(gte::Core& core, gte::EditorHost& editorHost) { RegisterFn(core, editorHost); }
