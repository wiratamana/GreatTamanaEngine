# plugins/demo_render_feature/CMakeLists.txt
#
# PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md, Step 3.5 - a real, throwaway
# demo plugin .dll implementing IRenderFeatureModule_v1 - contributes exactly
# one render-graph pass that clears the Game/Scene View to solid magenta
# (Milestone 1). Mirrors plugins/demo_hello_world/CMakeLists.txt's own CMake
# shape exactly.
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2 - a plugin .dll NEVER links gte_core/gte_editor directly).

add_library(demo_render_feature SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    # Same MinGW default "lib" prefix fix demo_hello_world/CMakeLists.txt
    # already applies (PHASE2_COMPLETION_REPORT.md, Deviation #2) - a stable,
    # predictable, prefix-free name.
    PREFIX ""
)
gte_apply_plugin_shared_crt_linkage(demo_render_feature)
