cmake_minimum_required(VERSION 3.19)

# --- Platform gate -----------------------------------------------------
# This build currently only supports Windows. Everything else is a quick,
# explicit failure rather than attempting (and likely breaking) a build.
if(NOT WIN32)
    message(FATAL_ERROR
        "GreatTamanaEngine's CMake build only supports Windows right now. "
        "Building on this platform is not supported.")
endif()

project(GreatTamanaEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Master switch for the in-engine Editor/Debug UI module (src/Editor/) -
# Dear ImGui-backed panels (Hierarchy/Inspector/Scene/Game/...). Turning
# this OFF is what makes a genuine "release/final game build": no ImGui
# fetch, no ImGui sources compiled, no ImGui symbols linked at all - not
# just a runtime flag. Application/Renderer/Game never need #ifdefs around
# this either way - see src/Editor/EditorLayer.h (IEditorLayer interface +
# CreateEditorLayer() factory) and its two mutually-exclusive
# implementations, ImGuiEditorLayer.cpp (real, this ON) and
# NullEditorLayer.cpp (inert no-op, this OFF).
option(GTE_ENABLE_EDITOR "Build with in-engine Editor/Debug UI (Dear ImGui)" ON)

# Master switch for the GoogleTest-based unit test suite (tests/) - see
# cmake/FetchGTest.cmake and tests/CMakeLists.txt. Turning this OFF means a
# configure never touches the network for GoogleTest and never compiles a
# single test/GoogleTest source file - same "zero-touch when off" philosophy
# as GTE_ENABLE_EDITOR above.
option(GTE_BUILD_TESTS "Build the GreatTamanaEngineTests unit test suite (GoogleTest)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchSDL3)
include(FetchVulkan)
include(FetchVMA)
include(CompileShaders)

# Downloads (if not already present) and stages SDL3 for Windows:
#   include/SDL3/*.h  - headers
#   SDL3.dll          - runtime binary
#   lib/SDL3.lib      - import library, for linking
# Also defines the SDL3::SDL3 imported target used below.
fetch_sdl3()

# Downloads (if not already present) and stages Vulkan for Windows, with no
# Vulkan SDK installation required on the machine:
#   include/vulkan/*.h, include/vk_video/*.h - official Vulkan-Headers
#   third_party/volk/volk.h, volk.c          - volk meta-loader (dynamically
#                                               loads vulkan-1.dll at runtime)
# Also defines the Vulkan::Headers and volk targets used below.
fetch_vulkan()

# Downloads (if not already present) and stages the Vulkan Memory Allocator
# (VMA) header for Windows:
#   third_party/vma/vk_mem_alloc.h - single-header allocator library
# Also defines the `vma` INTERFACE target, linked into gte_core below and
# used by src/Renderer/Vulkan/VulkanAllocator.h/.cpp (which owns the actual
# VmaAllocator) and src/Renderer/RenderTexture.cpp (which allocates its
# VkImage through it via vmaCreateImage()).
fetch_vma()

# Dear ImGui itself is only fetched/built at all when the Editor is enabled
# - a release build configured with -DGTE_ENABLE_EDITOR=OFF never touches
# the network for this, never compiles a single ImGui source file, and
# never links it.
if(GTE_ENABLE_EDITOR)
    include(FetchImGui)

    # Downloads (if not already present) and stages Dear ImGui for Windows
    # (core + SDL3/Vulkan backends), routed through this project's volk
    # loader:
    #   third_party/imgui/*.h, *.cpp                          - core library
    #   third_party/imgui/backends/imgui_impl_sdl3.h/.cpp     - SDL3 platform backend
    #   third_party/imgui/backends/imgui_impl_vulkan.h/.cpp   - Vulkan renderer backend
    fetch_imgui()

    include(FetchImGuizmo)

    # Downloads (if not already present) and stages ImGuizmo (the Editor's
    # Scene-view 3D transform gizmo widget - translate/rotate/scale
    # manipulators), built directly on top of Dear ImGui:
    #   third_party/imguizmo/ImGuizmo.h/.cpp - gizmo widget core
    # Also defines the imguizmo target used below. Must come after
    # fetch_imgui() since it links against the imgui target.
    fetch_imguizmo()
endif()

# GoogleTest is only fetched/built at all when GTE_BUILD_TESTS is ON - see
# cmake/FetchGTest.cmake and tests/CMakeLists.txt.
if(GTE_BUILD_TESTS)
    include(FetchGTest)
    fetch_gtest()
endif()

# --- Core engine library -----------------------------------------------
# Every engine source EXCEPT main.cpp lives in this static library, so both
# the real executable (GreatTamanaEngine, below) and the unit test suite
# (GreatTamanaEngineTests, see tests/CMakeLists.txt) link against the exact
# same compiled engine code - the test binary needs no copy of main()/the
# SDL main loop, and there is no risk of the two ever silently drifting
# apart (e.g. differing compile flags) the way a naively-duplicated file
# list could.
add_library(gte_core STATIC
    src/Math/MathTypes.h
    src/Math/Vec2.h
    src/Math/Vec3.h
    src/Math/Vec4.h
    src/Math/Mat4.cpp
    src/Math/Mat4.h
    src/Math/Quat.cpp
    src/Math/Quat.h
    src/ECS/Entity.h
    src/ECS/EntityManager.cpp
    src/ECS/EntityManager.h
    src/ECS/ComponentStorage.h
    src/ECS/Registry.h
    src/ECS/Components/Transform.h
    src/ECS/Components/MeshRenderer.h
    src/ECS/Components/Camera.h
    src/Memory/SdlMemoryTracker.h
    src/Memory/SdlMemoryTracker.cpp
    src/Application/Application.cpp
    src/Application/Application.h
    src/Application/EventTranslator.cpp
    src/Application/EventTranslator.h
    src/Event/Event.h
    src/Input/InputState.cpp
    src/Input/InputState.h
    src/Window/Window.cpp
    src/Window/Window.h
    src/Renderer/Renderer.cpp
    src/Renderer/Renderer.h
    src/Renderer/FramePresenter.cpp
    src/Renderer/FramePresenter.h
    src/Renderer/FrameRecorder.cpp
    src/Renderer/FrameRecorder.h
    src/Renderer/GpuResourceFactory.cpp
    src/Renderer/GpuResourceFactory.h
    src/Renderer/Buffer.cpp
    src/Renderer/Buffer.h
    src/Renderer/Mesh.h
    src/Renderer/MeshHandle.h
    src/Renderer/PipelineHandle.h
    src/Renderer/ResourcePool.h
    src/Renderer/Pipeline.cpp
    src/Renderer/Pipeline.h
    src/Renderer/Vertex.h
    src/Renderer/Primitives/PrimitiveMeshGenerator.cpp
    src/Renderer/Primitives/PrimitiveMeshGenerator.h
    src/Renderer/Memory/GpuResourceHandle.h
    src/Renderer/Memory/GpuMemoryTracker.cpp
    src/Renderer/Memory/GpuMemoryTracker.h
    src/Renderer/RenderTarget.h
    src/Renderer/RenderTexture.cpp
    src/Renderer/RenderTexture.h
    src/Renderer/Vulkan/VulkanInstance.cpp
    src/Renderer/Vulkan/VulkanInstance.h
    src/Renderer/Vulkan/VulkanSurface.cpp
    src/Renderer/Vulkan/VulkanSurface.h
    src/Renderer/Vulkan/VulkanDevice.cpp
    src/Renderer/Vulkan/VulkanDevice.h
    src/Renderer/Vulkan/VulkanSwapchain.cpp
    src/Renderer/Vulkan/VulkanSwapchain.h
    src/Renderer/Vulkan/VulkanAllocator.cpp
    src/Renderer/Vulkan/VulkanAllocator.h
    src/Renderer/Vulkan/VulkanFrameSync.cpp
    src/Renderer/Vulkan/VulkanFrameSync.h
    src/Editor/EditorLayer.h
    src/Game/Game.cpp
    src/Game/Game.h
    src/Game/RenderSystem.cpp
    src/Game/RenderSystem.h
)

# Exactly one of these two branches gets compiled in, depending on
# GTE_ENABLE_EDITOR - both define the same gte::CreateEditorLayer() factory
# function (see src/Editor/EditorLayer.h), so there's never an ODR conflict
# and Application never needs to know which one it linked against.
#
# Every file listed in the GTE_ENABLE_EDITOR branch below (besides
# ImGuiEditorLayer.cpp itself) is part of the same Editor/ImGui boundary -
# DockLayout.* and Panels/*.* are only ever compiled here, alongside it, and
# are just as free to include ImGui/SDL headers directly - see AGENTS.md,
# "Editor Module Structure".
if(GTE_ENABLE_EDITOR)
    target_sources(gte_core PRIVATE
        src/Editor/EditorContext.h
        src/Editor/EditorCamera.h
        src/Editor/EditorCamera.cpp
        src/Editor/TransformGizmo.h
        src/Editor/TransformGizmo.cpp
        src/Editor/DockLayout.h
        src/Editor/DockLayout.cpp
        src/Editor/ImGuiMemoryTracker.h
        src/Editor/ImGuiMemoryTracker.cpp
        src/Editor/MemoryPanelData.h
        src/Editor/MemoryPanelData.cpp
        src/Editor/Panels/HierarchyPanel.h
        src/Editor/Panels/HierarchyPanel.cpp
        src/Editor/Panels/InspectorPanel.h
        src/Editor/Panels/InspectorPanel.cpp
        src/Editor/Panels/ScenePanel.h
        src/Editor/Panels/ScenePanel.cpp
        src/Editor/Panels/GamePanel.h
        src/Editor/Panels/GamePanel.cpp
        src/Editor/Panels/MemoryPanel.h
        src/Editor/Panels/MemoryPanel.cpp
        src/Editor/ImGuiEditorLayer.cpp
    )
    target_link_libraries(gte_core PRIVATE imgui imguizmo)
else()
    target_sources(gte_core PRIVATE src/Editor/NullEditorLayer.cpp)
endif()

# Exposes GTE_ENABLE_EDITOR to C++ code as an actual preprocessor macro
# (1 or 0), not just a CMake-time source-selection switch - needed by
# src/Renderer/Memory/GpuMemoryTracker.h, which uses `#if GTE_ENABLE_EDITOR`
# to compile debug-name storage into the binary ONLY for Editor builds (see
# AGENTS.md, "GPU resource memory tracking"). $<BOOL:...> normalizes
# CMake's ON/OFF (or TRUE/FALSE/1/0/...) into a plain 1 or 0.
#
# PUBLIC (not PRIVATE) is deliberate here, unlike most of gte_core's other
# compile-time details: GpuMemoryTracker.h's `#if GTE_ENABLE_EDITOR` is seen
# by ANY translation unit that includes that header, including test code in
# GreatTamanaEngineTests (see tests/Memory/GpuMemoryTrackerTests.cpp) - if
# this were PRIVATE, the macro would silently evaluate to 0 (undefined) for
# every consumer of gte_core, regardless of what gte_core itself was
# actually built with, and Editor-only behavior could never be tested.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_EDITOR=$<BOOL:${GTE_ENABLE_EDITOR}>)

# PUBLIC (not PRIVATE) for the same reason as the compile definition above:
# any consumer of gte_core (GreatTamanaEngine's main.cpp, and every test in
# GreatTamanaEngineTests) #includes engine headers that transitively need
# SDL3/volk/vma's include directories and, ultimately, their libraries at
# final-link time - PUBLIC is what propagates both automatically instead of
# every consumer having to repeat them.
target_include_directories(gte_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gte_core PUBLIC SDL3::SDL3 volk vma)

# --- Final executable ----------------------------------------------------
add_executable(GreatTamanaEngine src/main.cpp)
target_link_libraries(GreatTamanaEngine PRIVATE gte_core)

# Compiles the engine's GLSL shader sources to SPIR-V at build time (see
# cmake/CompileShaders.cmake) - staged into "<exe dir>/shaders/" next to the
# built .exe, never committed as a binary (see .gitignore). Pipeline.cpp
# loads these compiled .spv files by relative path at runtime. Only the
# actual game executable needs these staged next to it - the test suite
# never renders anything, see tests/CMakeLists.txt.
gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.vert)
gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.frag)

# Ensures SDL3.dll is copied next to the built .exe after every build.
sdl3_copy_runtime_dll(GreatTamanaEngine)

# --- Unit tests ------------------------------------------------------------
# See tests/CMakeLists.txt for GreatTamanaEngineTests itself (links against
# gte_core + GoogleTest). enable_testing() must be called from this
# top-level CMakeLists.txt (not from within tests/CMakeLists.txt) for CTest
# integration (gtest_discover_tests()) to register properly.
if(GTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
