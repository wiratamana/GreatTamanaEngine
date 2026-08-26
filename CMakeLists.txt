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

# Master switch for the Editor's Unity-style "Project" panel (a live view of
# a "Project" folder created next to the built .exe, plus external
# drag-and-drop file import - see src/Editor/Panels/ProjectPanel.h/.cpp and
# src/Editor/ProjectPanelData.h/.cpp). Kept as its OWN switch, separate from
# GTE_ENABLE_EDITOR above, because this is a newer, still-evolving feature
# (live filesystem access from inside the Editor, plus OS-level drag-and-
# drop) that a developer may want to disable independently while it's
# fleshed out further, without giving up the rest of the Editor - flipping
# this OFF removes the panel and every bit of its filesystem access
# entirely, with no #ifdef soup anywhere else (just the small, explicit
# `#if GTE_ENABLE_PROJECT_PANEL` guards in ImGuiEditorLayer.cpp/
# DockLayout.cpp). Only meaningful when GTE_ENABLE_EDITOR is also ON - the
# Project panel is part of the ImGui-backed Editor UI and doesn't exist
# without it.
option(GTE_ENABLE_PROJECT_PANEL "Build the Editor's Project panel (Project-folder file browser + drag-and-drop import)" ON)

# Master switch for the GoogleTest-based unit test suite (tests/) - see
# cmake/FetchGTest.cmake and tests/CMakeLists.txt. Turning this OFF means a
# configure never touches the network for GoogleTest and never compiles a
# single test/GoogleTest source file - same "zero-touch when off" philosophy
# as GTE_ENABLE_EDITOR above.
option(GTE_BUILD_TESTS "Build the GreatTamanaEngineTests unit test suite (GoogleTest)" ON)

# Master switch for the always-compiled Profiling module (src/Profiling/) -
# CPU scope timers (ScopeTimer/FrameProfiler) instrumenting a handful of
# existing per-frame call sites (Application::Run(), Game::Update(),
# AnimationSystem::Update(), RenderSystem::CollectRenderables()/Draw(),
# Renderer::RenderOffscreen()/Present(), IEditorLayer::BuildUI() - see
# PROFILER_STRATEGY_v2.md, Phase 1). Turning this OFF compiles every
# GTE_PROFILE_SCOPE() call site (src/Profiling/ScopeTimer.h) down to a
# literal no-op with zero clock reads - the "genuinely zero cost, not just
# small" branch a true minimal-size release build can opt into (see
# AGENTS.md, "Profiling"). The underlying FrameProfiler data model/ring
# buffer (src/Profiling/FrameProfiler.h/.cpp) stays compiled in either way
# (same "class always compiles, only its production call sites are gated"
# precedent as SdlMemoryTracker - see AGENTS.md, "CPU Dependency Memory
# Tracking") so it stays available/testable in a GTE_ENABLE_PROFILER=OFF
# configuration too. A SEPARATE runtime capture-enabled flag
# (FrameProfiler::SetCaptureEnabled()) exists for turning capture on/off at
# runtime without a second build - see PROFILER_STRATEGY_v2.md, Phase 0b.
option(GTE_ENABLE_PROFILER "Compile in CPU scope-timer profiling instrumentation" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchSDL3)
include(FetchVulkan)
include(FetchVMA)
include(FetchSTB)
include(FetchKTX)
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

# Downloads (if not already present) and stages stb_image.h for Windows:
#   third_party/stb/stb_image.h - single-header image loading library
#   (PNG/JPEG/BMP/GIF/TGA/PSD/HDR/...), used for loading image assets (e.g.
#   textures imported via the Editor's Project panel).
# Also defines the `stb_image` INTERFACE target, linked into gte_core below.
fetch_stb()

# Builds KTX-Software from source in-tree (headers + a STATIC `ktx` library,
# with Basis Universal/astc-encoder support baked in - no ktx.dll needed at
# runtime) - see cmake/FetchKTX.cmake for the full reasoning (KTX-Software's
# GitHub Releases only ship an NSIS .exe installer for Windows, no plain
# .zip SDK, so a from-source build is the only non-interactive option):
#   third_party/ktx/   - KTX-Software's full source tree
# Also defines the KTX::ktx target, linked into gte_core below.
fetch_ktx()

# Downloads (if not already present) and stages saba's raw PMX-model-file +
# VMD-motion-file reading layer (Saba/Base/*, Saba/Model/MMD/{PMXFile,VMDFile,
# MMDFileString,SjisToUnicode}.*) plus its one real header dependency, glm -
# used to parse MMD (MikuMikuDance) .pmx model files and extract vertex
# positions/normals/UVs/bones/morphs/physics (see src/Assets/PmxLoader.h/.cpp)
# and .vmd motion files and extract bone/morph/camera/light/shadow/IK
# keyframe tracks (see src/Assets/VmdLoader.h/.cpp). See cmake/FetchSaba.cmake
# for the full reasoning (why only this curated subset is vendored, not
# saba's Bullet-dependent skinning/physics runtime or its GLFW/ImGui-based
# viewer):
#   third_party/saba/src/   - saba's curated source subset (+ full LICENCE)
#   third_party/glm/glm/    - glm's header-only include tree
# Also defines the `saba_pmx` (STATIC) and `glm` (INTERFACE) targets, linked
# into gte_core below.
include(FetchSaba)
fetch_saba()

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
    src/ECS/TransformHierarchy.h
    src/ECS/TransformHierarchy.cpp
    src/ECS/Components/Transform.h
    src/ECS/Components/MeshRenderer.h
    src/ECS/Components/Camera.h
    src/ECS/Components/Name.h
    src/ECS/Components/MeshAssetSource.h
    src/ECS/Components/SkeletalAnimator.h
    src/Animation/BoneLocalOffset.h
    src/Animation/BoneChainResolver.h
    src/Animation/BonePoseMath.h
    src/Animation/SkeletonPose.h
    src/Animation/SkeletonPose.cpp
    src/Animation/IkSolver.h
    src/Animation/IkSolver.cpp
    src/Animation/AppendBoneSolver.h
    src/Animation/AppendBoneSolver.cpp
    src/Animation/MotionSampler.h
    src/Animation/MotionSampler.cpp
    src/Animation/AnimationPoseEvaluator.h
    src/Animation/AnimationPoseEvaluator.cpp
    src/Animation/VertexSkinning.h
    src/Animation/VertexSkinning.cpp
    src/Assets/AssetTypes.h
    src/Assets/AssetTypes.cpp
    src/Assets/GtaFile.h
    src/Assets/GtaFile.cpp
    src/Assets/AssetDatabase.h
    src/Assets/AssetDatabase.cpp
    src/Assets/StbImageImpl.cpp
    src/Assets/Ktx2Encoder.h
    src/Assets/Ktx2Encoder.cpp
    src/Assets/Ktx2Decoder.h
    src/Assets/Ktx2Decoder.cpp
    src/Assets/AssetImporter.h
    src/Assets/AssetImporter.cpp
    src/Assets/PmxLoader.h
    src/Assets/PmxLoader.cpp
    src/Assets/VmdLoader.h
    src/Assets/VmdLoader.cpp
    src/Assets/MaterialData.h
    src/Assets/ImageFileDecoder.h
    src/Assets/ImageFileDecoder.cpp
    src/Assets/MeshData.h
    src/Assets/SkeletonData.h
    src/Assets/MorphData.h
    src/Assets/PhysicsData.h
    src/Assets/MotionData.h
    src/Assets/MeshFile.h
    src/Assets/MeshFile.cpp
    src/Assets/MotionFile.h
    src/Assets/MotionFile.cpp
    src/Assets/RigFile.h
    src/Assets/RigFile.cpp
    src/Memory/SdlMemoryTracker.h
    src/Memory/SdlMemoryTracker.cpp
    src/Application/Application.cpp
    src/Application/Application.h
    src/Application/EventTranslator.cpp
    src/Application/EventTranslator.h
    src/Application/MemorySnapshotBuilder.h
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
    src/Renderer/DrawStats.cpp
    src/Renderer/DrawStats.h
    src/Renderer/GpuResourceFactory.cpp
    src/Renderer/GpuResourceFactory.h
    src/Renderer/Buffer.cpp
    src/Renderer/Buffer.h
    src/Renderer/Texture2D.cpp
    src/Renderer/Texture2D.h
    src/Renderer/TextureHandle.h
    src/Renderer/MaterialTexture.h
    src/Renderer/Mesh.h
    src/Renderer/MeshHandle.h
    src/Renderer/PipelineHandle.h
    src/Renderer/ResourcePool.h
    src/Renderer/Pipeline.cpp
    src/Renderer/Pipeline.h
    src/Renderer/Vertex.h
    src/Renderer/MeshVertex.h
    src/Renderer/Primitives/PrimitiveMeshGenerator.cpp
    src/Renderer/Primitives/PrimitiveMeshGenerator.h
    src/Renderer/Memory/GpuResourceHandle.h
    src/Renderer/Memory/GpuMemoryTracker.cpp
    src/Renderer/Memory/GpuMemoryTracker.h
    src/Renderer/RenderTarget.h
    src/Renderer/RenderTexture.cpp
    src/Renderer/DepthBuffer.cpp
    src/Renderer/DepthBuffer.h
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
    src/Game/Instantiation/EntityBlueprint.h
    src/Game/Instantiation/EntityInstantiator.cpp
    src/Game/Instantiation/EntityInstantiator.h
    src/Game/Instantiation/MeshVertexPacking.cpp
    src/Game/Instantiation/MeshVertexPacking.h
    src/Game/Instantiation/MeshMaterialPartitioner.cpp
    src/Game/Instantiation/MeshMaterialPartitioner.h
    src/Game/Instantiation/PrimitiveGpuCatalog.cpp
    src/Game/Instantiation/PrimitiveGpuCatalog.h
    src/Game/Instantiation/MaterialTextureGpuCache.cpp
    src/Game/Instantiation/MaterialTextureGpuCache.h
    src/Game/Instantiation/MeshAssetGpuCatalog.cpp
    src/Game/Instantiation/MeshAssetGpuCatalog.h
    src/Game/Instantiation/MeshInstantiationSystem.cpp
    src/Game/Instantiation/MeshInstantiationSystem.h
    src/Game/Animation/SkeletalRigCache.h
    src/Game/Animation/AnimationClipCache.cpp
    src/Game/Animation/AnimationClipCache.h
    src/Game/Animation/ResolvedAnimationBindingCache.h
    src/Game/Animation/AnimationSystem.cpp
    src/Game/Animation/AnimationSystem.h
    src/Game/Game.cpp
    src/Game/Game.h
    src/Game/RenderSystem.cpp
    src/Game/RenderSystem.h
    src/Profiling/ProfilingTypes.h
    src/Profiling/FrameProfiler.h
    src/Profiling/FrameProfiler.cpp
    src/Profiling/ScopeTimer.h
    src/Profiling/FrameGraphData.h
    src/Profiling/FrameGraphData.cpp
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
        src/Editor/Selection.h
        src/Editor/Selection.cpp
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

    # The "Project" panel (see GTE_ENABLE_PROJECT_PANEL above) is
    # independently toggleable even within an Editor build - only
    # compiled/linked in when ALSO on. ProjectPanelData.h/.cpp is the pure,
    # Tier-1-testable logic (see tests/Editor/ProjectPanelDataTests.cpp);
    # Panels/ProjectPanel.h/.cpp is the ImGui-facing class itself.
    if(GTE_ENABLE_PROJECT_PANEL)
        target_sources(gte_core PRIVATE
            src/Editor/ProjectPanelData.h
            src/Editor/ProjectPanelData.cpp
            src/Editor/Panels/ProjectPanel.h
            src/Editor/Panels/ProjectPanel.cpp
            src/Editor/AssetInspectorData.h
            src/Editor/AssetInspectorData.cpp
            src/Editor/AssetPreviewTexture.h
            src/Editor/AssetPreviewTexture.cpp
            src/Editor/AssetPreviewMesh.h
            src/Editor/AssetPreviewMesh.cpp
            src/Editor/BoneViewerWindow.h
            src/Editor/BoneViewerWindow.cpp
        )
    endif()
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

# Same idea as GTE_ENABLE_EDITOR's compile definition immediately above -
# exposes GTE_ENABLE_PROJECT_PANEL to C++ code as an actual 1/0 preprocessor
# macro, needed by ImGuiEditorLayer.cpp/DockLayout.cpp's own
# `#if GTE_ENABLE_PROJECT_PANEL` guards. PUBLIC for the same reason: this
# header-level guard must be seen identically by gte_core itself and by
# GreatTamanaEngineTests (see tests/Editor/ProjectPanelDataTests.cpp).
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_PROJECT_PANEL=$<BOOL:${GTE_ENABLE_PROJECT_PANEL}>)

# Same idea again - exposes GTE_ENABLE_PROFILER to C++ code as an actual
# 1/0 preprocessor macro, needed by src/Profiling/ScopeTimer.h's own
# `#if GTE_ENABLE_PROFILER` guard (see AGENTS.md, "Profiling"). PUBLIC for
# the same reason as the two definitions above: GreatTamanaEngineTests
# (tests/Profiling/FrameProfilerTests.cpp/ScopeTimerTests.cpp) must see the
# exact same macro value gte_core itself was built with.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_PROFILER=$<BOOL:${GTE_ENABLE_PROFILER}>)

# PUBLIC (not PRIVATE) for the same reason as the compile definition above:
# any consumer of gte_core (GreatTamanaEngine's main.cpp, and every test in
# GreatTamanaEngineTests) #includes engine headers that transitively need
# SDL3/volk/vma/stb_image/KTX's include directories and, ultimately, their
# libraries at final-link time - PUBLIC is what propagates both
# automatically instead of every consumer having to repeat them.
target_include_directories(gte_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gte_core PUBLIC SDL3::SDL3 volk vma stb_image KTX::ktx)

# PRIVATE (not PUBLIC) deliberately: saba_pmx/glm are an implementation
# detail confined entirely to src/Assets/PmxLoader.cpp/VmdLoader.cpp (see
# FetchSaba.cmake's header comment) - PmxLoader.h's/VmdLoader.h's own public
# APIs only ever expose this engine's plain gte::Vec3/Quat/MeshData/
# MotionData types, never a saba:: or glm:: type, so no consumer of gte_core
# needs saba_pmx's or glm's include directories propagated to it. CMake still
# statically links saba_pmx's/glm's object code into any final binary that
# links gte_core regardless of this PRIVATE-ness (PUBLIC vs PRIVATE only
# changes usage-requirement propagation, not whether a STATIC library's own
# archive gets linked transitively).
target_link_libraries(gte_core PRIVATE saba_pmx)

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

# The shared "imported mesh" draw path (src/Renderer/MeshVertex.h,
# Pipeline.h's VertexLayout::PositionNormal, Game::CreateMeshEntityFromGtaFile()
# - src/Game/Game.h/.cpp) needs its own shader pair, compiled/staged the
# exact same way as Triangle.vert/frag above. Unconditional (not gated
# behind GTE_ENABLE_PROJECT_PANEL, unlike MeshPreview.vert/frag below) -
# Game.cpp (which loads these) is always compiled into gte_core, regardless
# of whether the Editor's "Project" panel (today's only way to actually
# trigger a spawn - see Panels/HierarchyPanel.cpp/ScenePanel.cpp) is built.
gte_add_shader(GreatTamanaEngine src/Shaders/Mesh.vert)
gte_add_shader(GreatTamanaEngine src/Shaders/Mesh.frag)

# The TEXTURED half of that same "imported mesh" draw path (src/Renderer/
# MeshVertex.h's MeshVertexUv, Pipeline.h's VertexLayout::PositionNormalUv,
# Game::EnsureMeshAsset() - src/Game/Game.h/.cpp) - a per-material PMX
# submesh whose material resolves to a real texture file uses this pair
# instead of Mesh.vert/.frag above. Unconditional for the same reason as
# Mesh.vert/.frag above.
gte_add_shader(GreatTamanaEngine src/Shaders/TexturedMesh.vert)
gte_add_shader(GreatTamanaEngine src/Shaders/TexturedMesh.frag)

# The Inspector's Mesh Asset 3D preview viewer (src/Editor/AssetPreviewMesh.h)
# needs its own small shader pair, compiled/staged the exact same way as
# Triangle.vert/frag above - see that file's class comment for why it needs
# a dedicated pipeline/vertex layout rather than reusing
# Renderer::CreatePipeline()'s built-in one. Only compiled/staged when
# GTE_ENABLE_PROJECT_PANEL is ON, since AssetPreviewMesh.cpp - the only thing
# that ever loads these - isn't compiled into gte_core otherwise (see
# GTE_ENABLE_PROJECT_PANEL's target_sources() block above).
if(GTE_ENABLE_PROJECT_PANEL)
    gte_add_shader(GreatTamanaEngine src/Shaders/MeshPreview.vert)
    gte_add_shader(GreatTamanaEngine src/Shaders/MeshPreview.frag)
endif()

# Ensures SDL3.dll is copied next to the built .exe after every build.
sdl3_copy_runtime_dll(GreatTamanaEngine)

# Note: no equivalent ktx_copy_runtime_dll() call is needed - KTX-Software is
# built from source as a STATIC library (see cmake/FetchKTX.cmake), so there
# is no ktx.dll to stage next to the built .exe at all.

# --- Unit tests ------------------------------------------------------------
# See tests/CMakeLists.txt for GreatTamanaEngineTests itself (links against
# gte_core + GoogleTest). enable_testing() must be called from this
# top-level CMakeLists.txt (not from within tests/CMakeLists.txt) for CTest
# integration (gtest_discover_tests()) to register properly.
if(GTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
