cmake_minimum_required(VERSION 3.19)

if(NOT WIN32)
    message(FATAL_ERROR "GreatTamanaEngine only builds on Windows.")
endif()

project(GreatTamanaEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Options ---------------------------------------------------------------
# Editor's Project panel (Project folder browser and drag-and-drop import).
option(GTE_ENABLE_PROJECT_PANEL "Build the Editor's Project panel (file browser + drag-and-drop import)" ON)
# GoogleTest suite in tests/. OFF skips the GoogleTest download and every test source.
option(GTE_BUILD_TESTS "Build the GreatTamanaEngineTests unit test suite (GoogleTest)" ON)
# OFF turns every GTE_PROFILE_SCOPE() into a no-op.
option(GTE_ENABLE_PROFILER "Compile in CPU scope-timer profiling instrumentation" ON)
# OFF runs scheduled jobs inline on the calling thread instead of on a worker pool.
option(GTE_ENABLE_JOB_SYSTEM "Compile in the Job System's real worker-thread pool" ON)
# OFF stops Application from starting the embedded HTTP server.
option(GTE_ENABLE_NETWORK "Compile in and auto-start the embedded, loopback-only HTTP server" ON)
# Auto-discover and load Projects/<Name>/Libraries project assemblies.
option(GTE_ENABLE_PROJECT_ASSEMBLIES "Auto-discover and load per-developer Projects/<Name>/ Project Assembly .dll's" ON)
# Sparse-clone Models/Sponza into _reference/ when missing. Needs git on PATH.
option(GTE_FETCH_SPONZA "Clone the Sponza glTF test model (Models/Sponza only) into _reference/ if missing" ON)
# CI only (tools/ci/gte_core_standalone_probe): configure just gte_core, without SDL3, ImGui, the exe or tests.
option(GTE_CORE_STANDALONE_PROBE_ONLY "CI-ONLY: configure ONLY gte_core. Never set this manually." OFF)

# --- Modules -----------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchSDL3)
include(FetchVulkan)
include(FetchHeaderLibs)
include(FetchSourceLibs)
include(FetchSaba)
include(FetchKTX)
include(FetchImGui)
include(FetchGTest)
include(FetchSponza)
include(CompileShaders)
include(GenerateGlslConstants)
include(MingwRuntime)
include(GteProject)

# --- Third-party dependencies --------------------------------------------------
find_package(Threads REQUIRED)

fetch_vulkan()
fetch_vma()
fetch_stb()
fetch_stb_image_write()
fetch_httplib()
fetch_json()
fetch_cgltf()
fetch_mikktspace()
fetch_meshoptimizer()
fetch_spirv_reflect()
fetch_saba()
fetch_ktx()

if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)
    fetch_sdl3()
    fetch_imgui()
    fetch_imguizmo()
    if(GTE_FETCH_SPONZA)
        fetch_sponza()
    endif()
endif()

if(GTE_BUILD_TESTS)
    fetch_gtest()
endif()

# --- Engine and tests ------------------------------------------------------------
add_subdirectory(src)

if(GTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
