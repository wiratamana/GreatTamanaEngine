cmake_minimum_required(VERSION 3.19)

# tools/ci/gte_project_assembly_link_probe/CMakeLists.txt
#
# editor-core-separation-11 campaign, PHASE2
# (task_manager/editor-core-separation-11/PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md)
# - a tiny, MANUALLY-INVOCABLE, CI-only local CMake project proving the "a
# Project Assembly .dll links against the already-running
# GreatTamanaEditor.exe" mechanism (LDD2, PHASE0_MASTER_STRATEGY.md)
# genuinely works on this repo's real toolchain, BEFORE PHASE3 ever touches
# the real root CMakeLists.txt.
#
# Unlike every OTHER tools/ci/* probe (gte_core_standalone_probe,
# gte_core_player_link_probe, gte_plugin_abi_handshake_probe,
# gte_plugin_isolation_probe), this is deliberately NOT a nested-cmake
# wrapper around the real root CMakeLists.txt configured with
# GTE_CORE_STANDALONE_PROBE_ONLY=ON - that flag specifically SKIPS
# fetching/defining the imgui/imguizmo CMake targets (see root
# CMakeLists.txt's own `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guard around
# include(FetchImGui)/fetch_imgui()), which would make this probe's own
# Step 5 ImGui-context test (Finding A, PHASE0_MASTER_STRATEGY.md) impossible
# to run at all. Instead, this is a genuinely standalone project: it fetches
# its OWN, independent copies of SDL3 / Vulkan-Headers+volk / ImGui by
# reusing this repo's real cmake/Fetch*.cmake modules directly (via
# CMAKE_MODULE_PATH, mirroring root CMakeLists.txt line 118's own identical
# mechanism) - producing a real, genuine `imgui` STATIC target, built from
# the exact same fetch module and source/backend file list the real engine
# uses, without needing the main build/ tree or any nested configure at all.
# fetch_imgui() stages its files under this probe's OWN
# ${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/imgui via CMAKE_SOURCE_DIR
# resolving to THIS project's source root, not the main repo's already-
# fetched copy - a fresh, independent copy, expected and harmless.
#
# LANGUAGES CXX C (not CXX alone): mirrors root CMakeLists.txt line 11
# (project(GreatTamanaEngine LANGUAGES CXX C)) - required because volk.c
# (fetch_vulkan()'s own `volk` STATIC target) is a plain C translation unit;
# CMake cannot determine a linker language for a C-only target inside a
# project that never enabled the C language at all.
project(GteProjectAssemblyLinkProbe LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(GTE_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${GTE_REPO_ROOT}/cmake")

include(FetchSDL3)
fetch_sdl3()

include(FetchVulkan)
fetch_vulkan()

include(FetchImGui)
fetch_imgui()

# Step 4's toggle (PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md): whether the
# host exe auto-exports every global symbol found in its own object files
# (mirroring __declspec(dllexport) on everything, without needing it written
# explicitly). Defaults ON (the real, intended PHASE3 recipe) - to reproduce
# Step 4(b)'s "should fail to even link" experiment, re-configure this SAME
# source directory into a SEPARATE, throwaway binary directory with
# -DGTE_PROBE_EXPORT_ALL_SYMBOLS=OFF. Never flip this OFF in the PERMANENT
# build-project-assembly-link-probe tree - that tree always reflects the
# real, intended PHASE3 recipe (ON).
#
# REAL, CONCRETE FINDING (this probe's own Step 4 run, 2026-09-28) - a
# genuine deviation from the original design docs, exactly the class of
# "MinGW/GCC deviates from generic documentation" hazard PHASE2's own file
# warned about (see cmake/MingwRuntime.cmake's own -shared-libstdc++
# discovery precedent): CMake's own `WINDOWS_EXPORT_ALL_SYMBOLS` target
# property is DOCUMENTED (`cmake --help-property WINDOWS_EXPORT_ALL_SYMBOLS`)
# as "implemented only for MS-compatible tools on Windows" - it is a SILENT
# NO-OP on this repo's actual GNU/MinGW toolchain (confirmed: setting it to
# ON produced zero `--export-all-symbols`/any other extra flag anywhere in
# the generated build.ninja, and a real, non-annotated symbol
# (HostPlainAdd()) failed to link into probe_module.dll with
# WINDOWS_EXPORT_ALL_SYMBOLS left ON). The property is still SET below
# (harmless, forward-compatible, in case a future MSVC/clang-cl toolchain
# switch ever happens), but the REAL, WORKING mechanism on THIS toolchain is
# GNU ld's own native `--export-all-symbols` linker flag, applied directly
# via target_link_options() - confirmed, live, to make both g_probeCounter
# (explicit dllexport) AND HostPlainAdd()/ImGui::GetCurrentContext()
# (un-annotated) all resolve correctly across the .exe/.dll boundary. THIS,
# not the CMake property, is the real recipe PHASE3 must use on
# GreatTamanaEditor.
option(GTE_PROBE_EXPORT_ALL_SYMBOLS
    "Step 4 toggle: auto-export every global symbol from gte_project_assembly_probe_host (via GNU ld's --export-all-symbols, the mechanism that actually works on this MinGW toolchain - see the comment above). Leave ON for the permanent probe tree; only set OFF in a scratch build dir to reproduce Step 4(b)'s 'should fail to link' experiment."
    ON)

add_executable(gte_project_assembly_probe_host main.cpp)
target_link_libraries(gte_project_assembly_probe_host PRIVATE imgui)
set_target_properties(gte_project_assembly_probe_host PROPERTIES
    ENABLE_EXPORTS ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)
if(GTE_PROBE_EXPORT_ALL_SYMBOLS)
    target_link_options(gte_project_assembly_probe_host PRIVATE "-Wl,--export-all-symbols")
endif()
sdl3_copy_runtime_dll(gte_project_assembly_probe_host)

add_library(gte_project_assembly_probe_module SHARED probe_module.cpp)
target_link_libraries(gte_project_assembly_probe_module
    PRIVATE gte_project_assembly_probe_host)
# The crux of Finding A (PHASE0_MASTER_STRATEGY.md): get imgui's HEADERS
# without re-linking its already-compiled code a second time. Linking
# gte_project_assembly_probe_host ALONE (above) does NOT transitively expose
# imgui's include directory (a PRIVATE link boundary breaks usage-requirement
# propagation, the same shape as Finding C's real GreatTamanaEditor PRIVATE
# link) - this explicit target_include_directories() call against imgui's
# own INTERFACE_INCLUDE_DIRECTORIES property is the exact, minimal fix. It
# adds ONLY a header search path - imgui is never named in a
# target_link_libraries() call for this target anywhere, so its
# already-compiled object code is never linked in a second time (confirmed:
# see this project's own build log for zero imgui*.cpp.obj object files
# pulled into gte_project_assembly_probe_module).
target_include_directories(gte_project_assembly_probe_module PRIVATE
    $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
set_target_properties(gte_project_assembly_probe_module PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:gte_project_assembly_probe_host>"
    PREFIX ""
    # main.cpp calls LoadLibraryW(L"probe_module.dll") verbatim (matching the
    # phase file's own Step 2 sample code) - the CMake TARGET name itself
    # stays gte_project_assembly_probe_module (matching Step 3's naming
    # convention/every other target in this project), but OUTPUT_NAME must be
    # overridden so the actual built file on disk is named to match.
    OUTPUT_NAME "probe_module"
)
