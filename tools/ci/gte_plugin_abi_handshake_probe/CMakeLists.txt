cmake_minimum_required(VERSION 3.19)

# editor-core-separation-3 campaign, PHASE2
# (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.6) - a tiny,
# MANUALLY-INVOCABLE, CI-only local CMake project that configures the REAL
# repository root CMakeLists.txt with GTE_CORE_STANDALONE_PROBE_ONLY=ON
# (exactly like tools/ci/gte_core_player_link_probe/ already does - see that
# project's own CMakeLists.txt for the full "why nested cmake, not
# add_subdirectory()" reasoning, which applies identically here) and then
# builds ONE MORE target that ONLY exists in that configuration:
# gte_plugin_abi_handshake_probe - a tiny executable that LoadLibraryW()s a
# real, already-built demo_hello_world.dll and mechanically re-proves the
# WHOLE plugin ABI handshake (fingerprint check, GetModuleInfo(), destroy,
# unload) end-to-end OUTSIDE GreatTamanaEditor.exe entirely, needing zero
# gte_core/gte_editor at all - only gte_plugin_abi.
project(GtePluginAbiHandshakeProbe LANGUAGES NONE)

get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(GTE_PLUGIN_ABI_HANDSHAKE_PROBE_INNER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gte_plugin_abi_inner_build")

add_custom_target(gte_plugin_abi_handshake_probe ALL
    COMMAND "${CMAKE_COMMAND}"
            -S "${GTE_REPO_ROOT}"
            -B "${GTE_PLUGIN_ABI_HANDSHAKE_PROBE_INNER_BUILD_DIR}"
            -G Ninja
            "-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
            "-DGTE_ENABLE_PLUGINS=ON"
            "-DGTE_BUILD_TESTS=OFF"
    COMMAND "${CMAKE_COMMAND}"
            --build "${GTE_PLUGIN_ABI_HANDSHAKE_PROBE_INNER_BUILD_DIR}"
            --target gte_plugin_abi_handshake_probe
    COMMENT "Configuring + building gte_plugin_abi_handshake_probe (+ demo_hello_world.dll transitively) in ${GTE_PLUGIN_ABI_HANDSHAKE_PROBE_INNER_BUILD_DIR}"
    VERBATIM
    USES_TERMINAL
)
