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
# Also defines the `vma` INTERFACE target, linked into GreatTamanaEngine
# below and used by src/Renderer/Vulkan/VulkanAllocator.h/.cpp (which owns
# the actual VmaAllocator) and src/Renderer/RenderTexture.cpp (which
# allocates its VkImage through it via vmaCreateImage()).
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
    # Also defines the imgui target used below.
    fetch_imgui()
endif()

add_executable(GreatTamanaEngine
    src/main.cpp
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
    src/Renderer/Pipeline.cpp
    src/Renderer/Pipeline.h
    src/Renderer/Vertex.h
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
)

# Compiles the engine's GLSL shader sources to SPIR-V at build time (see
# cmake/CompileShaders.cmake) - staged into "<exe dir>/shaders/" next to the
# built .exe, never committed as a binary (see .gitignore). Pipeline.cpp
# loads these compiled .spv files by relative path at runtime.
gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.vert)
gte_add_shader(GreatTamanaEngine src/Shaders/Triangle.frag)

# Exactly one of these two gets compiled in, depending on GTE_ENABLE_EDITOR -
# both define the same gte::CreateEditorLayer() factory function (see
# src/Editor/EditorLayer.h), so there's never an ODR conflict and
# Application never needs to know which one it linked against.
if(GTE_ENABLE_EDITOR)
    target_sources(GreatTamanaEngine PRIVATE src/Editor/ImGuiEditorLayer.cpp)
    target_link_libraries(GreatTamanaEngine PRIVATE imgui)
else()
    target_sources(GreatTamanaEngine PRIVATE src/Editor/NullEditorLayer.cpp)
endif()

# Exposes GTE_ENABLE_EDITOR to C++ code as an actual preprocessor macro
# (1 or 0), not just a CMake-time source-selection switch - needed by
# src/Renderer/Memory/GpuMemoryTracker.h, which uses `#if GTE_ENABLE_EDITOR`
# to compile debug-name storage into the binary ONLY for Editor builds (see
# AGENTS.md, "GPU resource memory tracking"). $<BOOL:...> normalizes
# CMake's ON/OFF (or TRUE/FALSE/1/0/...) into a plain 1 or 0.
target_compile_definitions(GreatTamanaEngine PRIVATE GTE_ENABLE_EDITOR=$<BOOL:${GTE_ENABLE_EDITOR}>)

target_include_directories(GreatTamanaEngine PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(GreatTamanaEngine PRIVATE SDL3::SDL3 volk vma)

# Ensures SDL3.dll is copied next to the built .exe after every build.
sdl3_copy_runtime_dll(GreatTamanaEngine)
