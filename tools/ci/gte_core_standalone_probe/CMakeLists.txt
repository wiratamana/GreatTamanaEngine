cmake_minimum_required(VERSION 3.19)

# editor-core-separation-1 campaign, PHASE18
# (task_manager/editor-core-separation-1/
# PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md) - a tiny,
# MANUALLY-INVOCABLE, CI-only local CMake project that builds ONLY the
# gte_core static library target, from the REAL repository root
# CMakeLists.txt - no gte_editor, no main executable, no test suite, no
# ImGui/SDL3 fetching. This is the ONLY place in the whole project where
# gte_editor is intentionally not configured (design doc Section 7.3/9,
# Phase 10; PHASE0_MASTER_STRATEGY.md's Locked Design Decision #5 - this
# repo has no real CI pipeline of its own, so this is a manually-invoked
# local probe, never a GitHub Actions workflow, and never a user-facing
# CMake option in the main build). See README.md (this same folder) for
# the exact command to run this by hand, and what a successful run proves.
#
# DEVIATION FROM THE ORIGINAL PLAN'S OWN SUGGESTED MECHANISM, DISCOVERED AND
# DOCUMENTED DURING EXECUTION (per Universal Rule 9 - decided during
# execution rather than assumed):
# The PHASE18 plan's own Step 3 suggested "add_subdirectory pointing back at
# the real source tree" as the likely-cleaner option versus hand-duplicating
# gte_core's own file list. That was tried FIRST and found to genuinely NOT
# work: every cmake/Fetch*.cmake module this repo's real root CMakeLists.txt
# calls (fetch_vulkan()/fetch_vma()/fetch_stb()/fetch_httplib()/fetch_json()/
# fetch_ktx()/fetch_saba(), all still reached even with
# GTE_CORE_STANDALONE_PROBE_ONLY=ON) hardcodes ${CMAKE_SOURCE_DIR} (the
# ABSOLUTE TOP of the whole source tree - a fixed, never-changing value tied
# to whatever directory was passed to cmake's own top-level "-S" argument),
# not ${CMAKE_CURRENT_SOURCE_DIR} - so add_subdirectory()-ing the real root
# from THIS folder resolved every one of those paths relative to THIS
# folder instead of the real repo root, failing immediately with "include
# could not find requested file: FetchSDL3" and siblings. Fixing that
# properly would mean rewriting ~140 CMAKE_SOURCE_DIR references across 13
# files spread through cmake/*.cmake - real, working, but a FAR larger and
# riskier diff than this phase's own declared "Files Touched" list allows,
# and not worth the risk for what is fundamentally a mechanical CI-tooling
# concern.
#
# THE WORKING MECHANISM INSTEAD: this file configures a trivial, LANGUAGES
# NONE meta-project (needs no compiler of its own at all) whose one real
# job is a single add_custom_target() that shells out to a completely
# SEPARATE, NESTED "cmake -S <real repo root> -B <its own private build
# dir>" invocation - i.e. the real root CMakeLists.txt is configured and
# built EXACTLY the way a normal top-level build always configures/builds
# it (so every ${CMAKE_SOURCE_DIR} reference inside every Fetch*.cmake
# module resolves correctly, unmodified, with zero risk to the real,
# shared build scripts), just with GTE_CORE_STANDALONE_PROBE_ONLY=ON and
# GTE_BUILD_TESTS=OFF passed on that inner invocation's own command line.
# `cmake --build` returning a non-zero exit code (because the inner
# configure/build failed) makes THIS project's own build fail too - a real,
# mechanical pass/fail signal, not just an advisory message.
project(GteCoreStandaloneProbe LANGUAGES NONE)

# The real repository root - three directories up from this file
# (tools/ci/gte_core_standalone_probe/ -> tools/ci/ -> tools/ -> repo root).
get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# A private, nested build directory for the real root CMakeLists.txt's own
# GTE_CORE_STANDALONE_PROBE_ONLY=ON configuration - kept entirely inside
# THIS project's own build tree, never colliding with a normal
# `cmake -S . -B build` configuration of the real repo root.
set(GTE_CORE_PROBE_INNER_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/gte_core_inner_build")

add_custom_target(gte_core_standalone_probe ALL
    COMMAND "${CMAKE_COMMAND}"
            -S "${GTE_REPO_ROOT}"
            -B "${GTE_CORE_PROBE_INNER_BUILD_DIR}"
            -G Ninja
            "-DGTE_CORE_STANDALONE_PROBE_ONLY=ON"
            "-DGTE_BUILD_TESTS=OFF"
    COMMAND "${CMAKE_COMMAND}"
            --build "${GTE_CORE_PROBE_INNER_BUILD_DIR}"
            --target gte_core
    COMMENT "Configuring + building gte_core STANDALONE (zero gte_editor/SDL/ImGui) in ${GTE_CORE_PROBE_INNER_BUILD_DIR}"
    VERBATIM
    USES_TERMINAL
)
