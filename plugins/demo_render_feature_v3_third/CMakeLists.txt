# plugins/demo_render_feature_v3_third/CMakeLists.txt
#
# editor-core-separation-9 campaign, PHASE2
# (PHASE2_OPERATION_REGISTRY_AND_ADAPTER_V3.md, Step 3.5) - a real, PERMANENT
# _v3 demo plugin exercising the THIRD shared "uber ops" operation,
# gte.builtin.color_grade, via a solid-fill-then-color-grade pair (there is no
# existing _v2 demo plugin using color grade to A/B against - see
# PHASE2_COMPLETION_REPORT.md for the honest, reasoned explanation of why this
# one is verified differently from the other two _v3 demo plugins).
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2).

add_library(demo_render_feature_v3_third SHARED
    RenderFeaturePlugin.cpp
)
target_link_libraries(demo_render_feature_v3_third PRIVATE gte_plugin_abi)
set_target_properties(demo_render_feature_v3_third PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    PREFIX ""
)
gte_apply_plugin_dll_shared_crt_linkage(demo_render_feature_v3_third)
