// plugins/demo_hello_world/HelloWorldPlugin.cpp
//
// PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.5 - Milestone
// 0's own handshake proof: implements zero capabilities, nothing else. This
// plugin is deliberately tiny/throwaway-quality - it exists to prove the ABI
// boundary works, not to demonstrate a real feature.
//
// editor-core-separation-5 campaign, PHASE4
// (PHASE4_HELLO_WORLD_ZERO_CAPABILITY_SYMMETRY_MIGRATION.md) - migrated onto
// ZeroCapabilityPluginModule + GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE (see
// PLUGIN_AUTHORING_ERGONOMICS_PROPOSAL_2026-09-24.md's own "What about
// demo_hello_world (zero capabilities)?" section), completing this campaign's
// goal of moving all four demo plugins onto the same authoring pattern. This
// ALSO switches this ONE plugin from the heap-allocated
// (new/delete-per-load) flavor to the static-instance flavor every other demo
// plugin already used before this campaign - see
// PHASE0_MASTER_STRATEGY.md's "Decisions made without ask_questions" #3 for
// why: this plugin never had any real per-instance construction logic to
// begin with, so nothing is lost, and it makes all four demo plugins follow
// one single, consistent pattern. Every string literal below (module
// name/version/description) is byte-for-byte identical to this file's
// pre-migration content.

#include "../gte_plugin_abi/SingleCapabilityPluginModule.h"
#include "../gte_plugin_abi/PluginExportsMacro.h"

namespace gte {
namespace {

ZeroCapabilityPluginModule g_module(
    MakeModuleInfo("HelloWorldPlugin", "1.0.0", "Milestone 0 handshake proof - implements zero capabilities."));

} // namespace
} // namespace gte

GTE_DEFINE_PLUGIN_EXPORTS_STATIC_INSTANCE(gte::g_module)
