# plugins/demo_render_feature_v2_second/CMakeLists.txt
#
# editor-core-separation-6 campaign, PHASE6
# (PHASE6_V2_DEMO_PLUGINS_AND_PIXEL_PROOF.md, Step 3.2) - a real, PERMANENT,
# SECOND, genuinely independent demo plugin .dll implementing
# IRenderFeatureModule_v2 - contributes a BLUE radial vignette pass, PreUI
# stage, priority 0, AlphaOver blend mode. Mirrors
# plugins/demo_render_feature_v2/CMakeLists.txt's own CMake shape exactly.
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2 - a plugin .dll NEVER links gte_core/gte_editor directly).

add_library(demo_render_feature_v2_second SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature_v2_second PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature_v2_second PROPERTIES
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
gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_v2_second)
