# plugins/demo_render_feature_v3/CMakeLists.txt
#
# editor-core-separation-9 campaign, PHASE2
# (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
# demo plugin .dll implementing IRenderFeatureModule_v3 - reimplements
# demo_render_feature_v2's own exact RED solid fill via the new, generic
# Dispatch("gte.builtin.solid_fill", ...) mechanism, for a direct pixel-parity
# A/B proof against that existing _v2 plugin (see PHASE2_COMPLETION_REPORT.md).
# This is also the PRIMARY _v3 demo plugin PHASE3 will ADD its own 2-pass GPU
# blur demo passes to (task_manager/editor-core-separation-9/PHASE0_MASTER_STRATEGY.md's
# Locked Product Decision #7) - never a second, separate folder for that.
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2 - a plugin .dll NEVER links gte_core/gte_editor directly).

add_library(demo_render_feature_v3 SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature_v3 PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature_v3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    PREFIX ""
)
gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_v3)
