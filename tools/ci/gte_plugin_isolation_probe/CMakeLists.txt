cmake_minimum_required(VERSION 3.19)

# editor-core-separation-3 campaign, PHASE5
# (PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md, Step 3.1) - a tiny,
# MANUALLY-INVOCABLE, CI-only local CMake project that configures the REAL
# repository root CMakeLists.txt with GTE_CORE_STANDALONE_PROBE_ONLY=ON
# (exactly like tools/ci/gte_plugin_abi_handshake_probe/ already does - see
# that project's own CMakeLists.txt for the full "why nested cmake, not
# add_subdirectory()" reasoning, which applies here identically) and then
# builds ONE MORE target that ONLY exists in that configuration:
# gte_plugin_isolation_probe - a tiny executable, linked against gte_core.a
# ALONE (no gte_editor/SDL/ImGui at all), that loads EVERY .dll in the shared
# plugins/ folder (all three demo plugins) via the real, production
# gte::PluginHost class and mechanically proves "always all-in" stays
# editor-clean in a genuinely Player-shaped process (Milestone 3).
project(GtePluginIsolationProbe LANGUAGES NONE)

get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(GTE_PLUGIN_ISOLATION_PROBE_INNER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gte_plugin_isolation_inner_build")

add_custom_target(gte_plugin_isolation_probe ALL
    COMMAND "${CMAKE_COMMAND}"
            -S "${GTE_REPO_ROOT}"
            -B "${GTE_PLUGIN_ISOLATION_PROBE_INNER_BUILD_DIR}"
            -G Ninja
            "-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
            "-DGTE_ENABLE_PLUGINS=ON"
            "-DGTE_BUILD_TESTS=OFF"
    COMMAND "${CMAKE_COMMAND}"
            --build "${GTE_PLUGIN_ISOLATION_PROBE_INNER_BUILD_DIR}"
            --target gte_plugin_isolation_probe
    COMMENT "Configuring + building gte_plugin_isolation_probe (+ all three demo plugin .dlls transitively) in ${GTE_PLUGIN_ISOLATION_PROBE_INNER_BUILD_DIR}"
    VERBATIM
    USES_TERMINAL
)
