#pragma once

#include "GtePluginAbiFingerprint.h"
#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"
#include "IPluginModule.h"

// editor-core-separation-5 campaign, PHASE1
// (PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md) - hides the extern "C" {
// ... } export block every plugin .dll must write out by hand today (see
// PublicSurface.md's own "IPluginModule and the three fixed exports" section
// for exactly why extern "C" itself can never be removed - GetProcAddress()
// resolves these 3 names by exact, literal, unmangled string match, so
// PluginHost::TryLoadOnePlugin() and every existing plugin .dll are
// completely unaffected by this file - it produces the IDENTICAL exported
// symbols, just without the plugin author re-typing the same 8 lines every
// time).
//
// Two flavors, matching the two ways a plugin's IPluginModule can be produced:
//
// GTE_DEFINE_PLUGIN_EXPORTS(ModuleClass) - the module is heap-allocated fresh
//     each time GTE_CreatePluginModule() is called (`new ModuleClass()`), and
//     genuinely destroyed via `delete` when GTE_DestroyPluginModule() is
//     called. Use this when the module needs real per-instance construction
//     logic.
//
// GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(instanceExpr) - the module is a
//     single, already-existing object (typically a file-scope
//     `namespace { MyModule g_module; }`) - GTE_CreatePluginModule() just
//     returns its address, GTE_DestroyPluginModule() is an intentional no-op
//     (the object's own destructor runs naturally at DLL unload/process
//     exit). PREFERRED for the common case: it means this plugin never once
//     calls `new`/`delete` for its own module object at all, which is one
//     less thing to reason about under the plugin ABI's own shared-heap
//     caveat (nothing to free across the boundary because nothing was ever
//     allocated across it) - see docs/conventions/plugin-architecture.md's
//     "The shared/DLL CRT requirement" section for the full caveat this
//     sidesteps.
#define GTE_DEFINE_PLUGIN_EXPORTS(ModuleClass) \
    extern "C" { \
    __declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint() { return gte::MakeThisBuildsFingerprint(); } \
    __declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return new ModuleClass(); } \
    __declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule* module) { delete module; } \
    }

#define GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(instanceExpr) \
    extern "C" { \
    __declspec(dllexport) gte::GtePluginAbiFingerprint GTE_GetPluginAbiFingerprint() { return gte::MakeThisBuildsFingerprint(); } \
    __declspec(dllexport) gte::IPluginModule* GTE_CreatePluginModule() { return &(instanceExpr); } \
    __declspec(dllexport) void GTE_DestroyPluginModule(gte::IPluginModule*) { /* static-instance flavor - nothing to free */ } \
    }
