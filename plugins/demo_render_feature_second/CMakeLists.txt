# plugins/demo_render_feature_second/CMakeLists.txt
#
# editor-core-separation-4 campaign, PHASE5
# (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
# SECOND, genuinely independent throwaway demo plugin .dll implementing
# IRenderFeatureModule_v1, so the "2+ plugins implement the same capability"
# path is exercised for real. Mirrors plugins/demo_render_feature/
# CMakeLists.txt's own CMake shape exactly.
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2 - a plugin .dll NEVER links gte_core/gte_editor directly).

add_library(demo_render_feature_second SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature_second PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature_second PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    # Same MinGW default "lib" prefix fix demo_hello_world/CMakeLists.txt
    # already applies (PHASE2_COMPLETION_REPORT.md, Deviation #2) - a stable,
    # predictable, prefix-free name.
    PREFIX ""
)
# editor-core-separation-4 campaign, PHASE4
# (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - a plugin .dll
# target must call the DLL-specific variant (no runtime DLL staging into
# the shared plugins/ scan folder), not the original host/probe function.
gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_second)
