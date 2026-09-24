# tests/Fixtures/FakePlugins/CMakeLists.txt
#
# editor-core-separation-4 campaign, PHASE8
# (PHASE8_PLUGINHOST_FAILURE_PATH_REGRESSION_TESTS_AND_CAMPAIGN_CLOSEOUT.md,
# Step 3.1) - 5 deliberately-broken/instrumented fixture .dll targets used
# ONLY by tests/Core/Plugins/PluginHostFailurePathTests.cpp to exercise
# PluginHost's own 4 documented failure/skip paths. Each fixture is its own
# tiny SHARED library target, isolated from the real, shared plugins/ scan
# folder (GTE_PLUGIN_RUNTIME_OUTPUT_DIR) via its own dedicated
# RUNTIME_OUTPUT_DIRECTORY under ${CMAKE_BINARY_DIR}/test_fixtures/fake_plugins
# - so these deliberately broken .dll's can NEVER be accidentally picked up
# by the real Editor's own plugin scan.
#
# Reached via add_subdirectory(Fixtures/FakePlugins) near the top of
# tests/CMakeLists.txt, itself only reached when GTE_BUILD_TESTS is ON (see
# the root CMakeLists.txt) - same scope tests/CMakeLists.txt itself is
# already gated by.

set(GTE_FAKE_PLUGIN_FIXTURES_DIR "${CMAKE_BINARY_DIR}/test_fixtures/fake_plugins")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/FakePluginFixturesDirGenerated.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/FakePluginFixturesDirGenerated.h"
    @ONLY
)

# CRITICAL WIRING STEP - the configure_file() call above writes into THIS
# subdirectory's OWN CMAKE_CURRENT_BINARY_DIR (tests/Fixtures/FakePlugins's
# binary dir), which is a COMPLETELY DIFFERENT directory than
# gte_plugin_abi's own generated/ folder (plugins/gte_plugin_abi's binary
# dir) - linking a fixture target against gte_plugin_abi does NOT expose
# THIS folder's own generated header to anything. Without this small
# INTERFACE library, tests/Core/Plugins/PluginHostFailurePathTests.cpp's own
# #include "generated/FakePluginFixturesDirGenerated.h" fails to compile
# ("No such file or directory") the moment GreatTamanaEngineTests is built,
# since nothing else ever added this directory to that target's own include
# path. Wired into GreatTamanaEngineTests via target_link_libraries() in
# tests/CMakeLists.txt, right after the existing gte_plugin_abi
# INTERFACE-library precedent (target_include_directories(gte_plugin_abi
# INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")) - just its own tiny,
# dedicated INTERFACE library instead of piggy-backing on an existing one
# (this fixture's own generated header has nothing to do with the plugin
# ABI itself).
add_library(gte_fake_plugin_fixtures_dirs INTERFACE)
# NOTE - real, mechanically-discovered correction to this phase file's own
# literal sketch (which used "${CMAKE_CURRENT_BINARY_DIR}/generated" here):
# the generated header physically lives at
# "${CMAKE_CURRENT_BINARY_DIR}/generated/FakePluginFixturesDirGenerated.h"
# (see the configure_file() call above), and
# PluginHostFailurePathTests.cpp's own #include spelling is
# "generated/FakePluginFixturesDirGenerated.h" (i.e. it includes the
# "generated/" path component itself) - so the include directory added here
# must be CMAKE_CURRENT_BINARY_DIR (this subdirectory's plain binary dir,
# WITHOUT its own "/generated" suffix), not "${CMAKE_CURRENT_BINARY_DIR}/generated"
# (which would require the #include to search for a REDUNDANT nested
# "generated/generated/..." path - confirmed as a real, reproducible
# "No such file or directory" compile failure before this fix, not a
# hypothetical one). This is a narrower case than gte_plugin_abi's own
# precedent (that module's generated header lives one level DEEPER, under
# its own "generated/gte_plugin_abi/" subfolder, so its include directory
# correctly IS "${CMAKE_CURRENT_BINARY_DIR}/generated" one level up from
# its own module-named subfolder) - this fixture's own generated header has
# no such module-named subfolder, "generated/" IS the leaf folder name.
target_include_directories(gte_fake_plugin_fixtures_dirs INTERFACE
    "${CMAKE_CURRENT_BINARY_DIR}"
)

function(add_fake_plugin_fixture target_name source_file output_subdir)
    add_library(${target_name} SHARED ${source_file})
    target_link_libraries(${target_name} PRIVATE gte_plugin_abi)
    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${GTE_FAKE_PLUGIN_FIXTURES_DIR}/${output_subdir}"
    )
    # Folded in HERE so none of the 5 calls below can accidentally forget it
    # - every fixture .dll is a plugin-ABI-boundary .dll exactly like a real
    # demo plugin, so it must call the SAME CRT-linkage helper every demo
    # plugin CMakeLists.txt calls (PHASE4's
    # gte_apply_plugin_dll_shared_crt_linkage()).
    gte_apply_plugin_dll_shared_crt_linkage(${target_name})
endfunction()

# Section 3.2 (this phase's own file) - 3 fixtures in "unhappy_path" (each
# fails a DIFFERENT one of PluginHost's own load-time checks), 2 fixtures in
# "destroy_order" (both load successfully, prove reverse-destroy-order).
add_fake_plugin_fixture(fake_plugin_missing_export FakePluginMissingExport.cpp unhappy_path)
add_fake_plugin_fixture(fake_plugin_bad_fingerprint FakePluginBadFingerprint.cpp unhappy_path)
add_fake_plugin_fixture(fake_plugin_declines_to_load FakePluginDeclinesToLoad.cpp unhappy_path)
add_fake_plugin_fixture(fake_plugin_destroy_order_a FakePluginDestroyOrderA.cpp destroy_order)
add_fake_plugin_fixture(fake_plugin_destroy_order_b FakePluginDestroyOrderB.cpp destroy_order)
