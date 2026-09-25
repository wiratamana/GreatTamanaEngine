# plugins/demo_render_feature_v3_second/CMakeLists.txt
#
# editor-core-separation-9 campaign, PHASE2
# (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
# _v3 demo plugin, mirroring demo_render_feature_v2_second's own exact BLUE
# radial vignette, reimplemented via the new generic
# Dispatch("gte.builtin.radial_vignette", ...) mechanism, for a direct
# pixel-parity A/B proof (see PHASE2_COMPLETION_REPORT.md).
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2).

add_library(demo_render_feature_v3_second SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature_v3_second PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature_v3_second PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    PREFIX ""
)
gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_v3_second)
