cmake_minimum_required(VERSION 3.19)

# editor-core-separation-2 campaign, PHASE4 - a tiny, MANUALLY-INVOCABLE,
# CI-only local CMake project that configures the REAL repository root
# CMakeLists.txt with GTE_CORE_STANDALONE_PROBE_ONLY=ON (exactly like
# tools/ci/gte_core_standalone_probe/ already does - see that project's own
# CMakeLists.txt for the full "why nested cmake, not add_subdirectory()"
# reasoning, which applies identically here) and then builds ONE MORE
# target that ONLY exists in that configuration:
# gte_core_player_link_probe - a tiny executable LINKED AGAINST gte_core.a
# ALONE. Unlike tools/ci/gte_core_standalone_probe/ (which only proves
# gte_core BUILDS AS AN ARCHIVE with zero gte_editor #include - an archive
# build never resolves cross-translation-unit symbols at all, so it cannot
# catch an undefined-reference-style violation), THIS probe's own build
# step is a REAL EXECUTABLE LINK - it fails immediately, mechanically, if
# gte_core's own object files carry any unresolved reference to a symbol
# only gte_editor.a defines. See README.md (this same folder).
project(GteCorePlayerLinkProbe LANGUAGES NONE)

get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gte_core_inner_build")

add_custom_target(gte_core_player_link_probe ALL
    COMMAND "${CMAKE_COMMAND}"
            -S "${GTE_REPO_ROOT}"
            -B "${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
            -G Ninja
            "-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
            "-DGTE_BUILD_TESTS=OFF"
    COMMAND "${CMAKE_COMMAND}"
            --build "${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
            --target gte_core_player_link_probe
    COMMENT "Configuring + LINKING gte_core_player_link_probe against gte_core ALONE (zero gte_editor/SDL/ImGui) in ${GTE_PLAYER_LINK_PROBE_INNER_BUILD_DIR}"
    VERBATIM
    USES_TERMINAL
)
