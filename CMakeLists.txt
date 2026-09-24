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

# Master switch for the Editor's Unity-style "Project" panel (a live view of
# a "Project" folder created next to the built .exe, plus external
# drag-and-drop file import - see src/Editor/Panels/ProjectPanel.h/.cpp and
# src/Editor/ProjectPanelData.h/.cpp). Kept as its own switch, distinct from
# the always-built Editor module itself, because this is a newer, still-evolving feature
# (live filesystem access from inside the Editor, plus OS-level drag-and-
# drop) that a developer may want to disable independently while it's
# fleshed out further, without giving up the rest of the Editor - flipping
# this OFF removes the panel and every bit of its filesystem access
# entirely, with no #ifdef soup anywhere else (just the small, explicit
# `#if GTE_ENABLE_PROJECT_PANEL` guards in ImGuiEditorLayer.cpp/
# DockLayout.cpp). The
# Project panel is part of the ImGui-backed Editor UI and doesn't exist
# without it.
option(GTE_ENABLE_PROJECT_PANEL "Build the Editor's Project panel (Project-folder file browser + drag-and-drop import)" ON)

# Master switch for the GoogleTest-based unit test suite (tests/) - see
# cmake/FetchGTest.cmake and tests/CMakeLists.txt. Turning this OFF means a
# configure never touches the network for GoogleTest and never compiles a
# single test/GoogleTest source file - same "zero-touch when off" philosophy
# as every other "zero-touch when off" switch below.
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

# Master switch for the always-compiled Job System module (src/Jobs/) - a
# small, fixed-size pool of persistent OS worker threads plus a
# Schedule()/JobHandle/WaitForJobs() API (see
# task_manager/job_system/JOBSYSTEM_PHASE0_MASTER_STRATEGY_v2.md and
# AGENTS.md, "Job System"). As of Phase 1, nothing in the engine actually
# calls Jobs::JobSystem::Schedule() yet - this switch only controls whether
# JobSystem's Schedule()/WaitForJobs() run work on a REAL worker-thread pool
# (ON, the default) or immediately, synchronously, inline on the calling
# thread with no std::thread ever created at all (OFF) - the public API
# shape/observable contract (a JobHandle eventually becomes complete;
# WaitForJobs() returns once it is) is identical either way, same
# "class always compiles, only its internal behavior is gated" precedent
# GTE_ENABLE_PROFILER already establishes for ScopeTimer above.
option(GTE_ENABLE_JOB_SYSTEM "Compile in the Job System's real worker-thread pool" ON)

# Master switch for the always-compiled Network module (src/Network/) - an
# embedded, loopback-only HTTP server (gte::Network::NetworkServer, built on
# the already-fetched cpp-httplib - see cmake/FetchHttplib.cmake) that lets an
# external tool/script send the engine a plain HTTP request without ever
# blocking Application::Run()'s own frame loop (see
# task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md). Same "class always
# compiles, only the production call site is gated" precedent as
# GTE_ENABLE_JOB_SYSTEM above: turning this OFF means Application never calls
# NetworkServer::Start() (so a release build never opens a socket at all, at
# zero cost), while src/Network/'s own classes/tests still compile and pass
# either way.
option(GTE_ENABLE_NETWORK "Compile in and auto-start the embedded, loopback-only HTTP server" ON)

# editor-core-separation-1 campaign, PHASE18
# (PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md) - a
# CI-only/manually-invocable switch, NEVER meant to be set by a normal
# developer configure. When ON, this file configures ONLY the gte_core
# static library target - gte_editor, the main executable, ImGui/ImGuizmo/
# SDL3 fetching, and the test suite are all skipped entirely (see the
# matching if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) guards further down this
# file). This is the ONLY place in this repo where gte_editor is
# intentionally not configured - it exists purely so
# tools/ci/gte_core_standalone_probe/ can prove gte_core genuinely builds
# and links standalone, with zero gte_editor/SDL/ImGui involvement (design
# doc Section 7.3/9, Phase 10; PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #5). Never appears as a user-facing option in any normal build -
# it is not advertised in BUILDING.md and must never be toggled ON for
# ordinary development.
option(GTE_CORE_STANDALONE_PROBE_ONLY "CI-ONLY: configure ONLY gte_core, skipping gte_editor/the executable/tests entirely. Never set this manually." OFF)

# Master switch for the dynamic, runtime-loadable plugin system
# (plugins/gte_plugin_abi + src/Core/Plugins/PluginHost, editor-core-
# separation-3 campaign - task_manager/editor-core-separation-3/
# PHASE0_MASTER_STRATEGY.md). Same "always compiles, only the runtime call
# site is gated" precedent as GTE_ENABLE_PROFILER/GTE_ENABLE_JOB_SYSTEM/
# GTE_ENABLE_NETWORK above: turning this OFF means Core::LoadPlugins() is
# never actually called with a real, existing plugins/ folder at runtime (no
# .dll is ever scanned/loaded, at zero cost), while gte_plugin_abi/PluginHost/
# every capability interface still compiles and is still testable. Also
# controls whether gte_apply_plugin_shared_crt_linkage() (cmake/
# MingwRuntime.cmake) actually flips a target's CRT linkage at all - see
# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md's own Locked Design Decision #4.
option(GTE_ENABLE_PLUGINS "Compile in the dynamic plugin-loading system (gte_plugin_abi + PluginHost)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(FetchSDL3)
include(FetchVulkan)
include(FetchVMA)
include(FetchSTB)
include(FetchSTBImageWrite)
include(FetchHttplib)
include(FetchJson)
include(FetchKTX)
include(CompileShaders)
# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md, Step 3.4 (PHASE0_MASTER_STRATEGY.md's
# Locked Design Decision #4) - defines mingw_copy_runtime_dll()/
# gte_apply_plugin_shared_crt_linkage(), the reusable CRT-linkage-flip +
# runtime-DLL-staging helpers every target on either side of the plugin ABI
# boundary must call. Visible process-wide once included here (CMake
# function definitions are not directory-scoped), including from a future
# plugins/*/CMakeLists.txt subdirectory.
include(MingwRuntime)

if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) # editor-core-separation-1 PHASE18
# Downloads (if not already present) and stages SDL3 for Windows:
#   include/SDL3/*.h  - headers
#   SDL3.dll          - runtime binary
#   lib/SDL3.lib      - import library, for linking
# Also defines the SDL3::SDL3 imported target used below.
fetch_sdl3()
endif() # GTE_CORE_STANDALONE_PROBE_ONLY - gte_core dropped SDL3::SDL3 at link time entirely (editor-core-separation-1, PHASE14); the probe needs it no more than gte_core itself does.

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

# Downloads (if not already present) and stages stb_image_write.h for
# Windows:
#   third_party/stb/stb_image_write.h - single-header image *encoding*
#   library (PNG/BMP/TGA/JPG/HDR), the write-side counterpart of stb_image.h
#   above - used by src/Encoding/PngEncoder.h/.cpp to turn a raw RGBA8 pixel
#   buffer (e.g. a captured Game view/swapchain frame) into an in-memory PNG.
# Also defines the `stb_image_write` INTERFACE target, linked into gte_core
# below.
fetch_stb_image_write()

# Downloads (if not already present) and stages cpp-httplib for Windows:
#   third_party/httplib/httplib.h - single-header HTTP/HTTPS server+client
#   library, brought in purely as a debugging/tooling aid (e.g. a lightweight
#   in-process HTTP endpoint for inspecting engine state) - not yet used by
#   any engine source file.
# Also defines the `httplib` INTERFACE target, linked into gte_core below.
fetch_httplib()

# Downloads (if not already present) and stages nlohmann/json for Windows:
#   third_party/json/nlohmann/json.hpp - single-header, header-only JSON
#   parsing/serialization library, used by src/Network/NetworkRoutes.cpp to
#   parse POST /instantiate_primitive and POST /delete_entity request bodies
#   and build their JSON responses (see
#   task_manager/network-impl-3/PHASE1_JSON_DEPENDENCY_AND_REQUEST_PARSING.md).
# Also defines the `nlohmann_json` INTERFACE target, linked into gte_core
# below.
fetch_json()

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

# Dear ImGui itself is always fetched/built - the Editor module is always
# configured in this repo (editor-core-separation-1 campaign, PHASE8 -
# GTE_ENABLE_EDITOR no longer exists anywhere in this codebase; PHASE9 does
# the real gte_editor CMake target split this `if(TRUE)` is a placeholder
# for).
if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) # editor-core-separation-1 PHASE18 - skip ImGui/ImGuizmo entirely for the standalone-core probe.
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
# the real executable (GreatTamanaEditor, below - editor-core-separation-1
# campaign PHASE17 renamed it away from GreatTamanaEngine) and the unit test
# suite (GreatTamanaEngineTests, see tests/CMakeLists.txt) link against the exact
# same compiled engine code - the test binary needs no copy of main()/the
# SDL main loop, and there is no risk of the two ever silently drifting
# apart (e.g. differing compile flags) the way a naively-duplicated file
# list could.
add_library(gte_core STATIC
    src/Core/Time.h
    src/Core/Time.cpp
    src/Core/EngineContext.h
    # editor-core-separation-1 campaign, PHASE3
    # (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - the global,
    # gte_core-owned logging types/macros (Logging.h) and the registrable
    # ILogSink mechanism they route through (LogSink.h/.cpp), replacing the
    # old direct dependency every one of these files used to have on
    # src/Editor/Logger.h.
    src/Core/Logging.h
    src/Core/LogSink.h
    src/Core/LogSink.cpp
    # editor-core-separation-1 campaign, PHASE5
    # (PHASE5_EDITOR_CAPABILITY_INTERFACES_DESIGN.md) - Bucket B capability
    # interfaces (header-only, no .cpp - every interface here is pure
    # virtual). See this header's own top-of-file comment for exactly which
    # interfaces live here (and, just as importantly, which candidate
    # capabilities were investigated and found NOT to need one).
    src/Core/EditorCapabilities.h
    # editor-core-separation-2 campaign, PHASE2
    # (PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md) - the new
    # gte_core-owned pure-virtual interface FrameDebuggerCaptureContext
    # (src/Editor/FrameDebuggerCapture.h) implements (header-only, no .cpp -
    # every method is pure virtual). Closes Defects A and B (see
    # PHASE0_MASTER_STRATEGY.md).
    src/Core/FrameDebuggerCaptureRecorder.h
    # editor-core-separation-3 campaign, PHASE2
    # (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md) - gte::PluginHost,
    # the real gte_core-owned mechanism that scans a folder for *.dll,
    # LoadLibraryW()s each, checks the ABI fingerprint, and holds the
    # resulting registry. Always compiled (see this class's own doc comment
    # for why it is mechanical/capability-agnostic and unconditional -
    # GTE_ENABLE_PLUGINS only gates the real runtime CALL SITE,
    # EditorHost.cpp's m_core.LoadPlugins() call).
    src/Core/Plugins/PluginHost.h
    src/Core/Plugins/PluginHost.cpp
    # editor-core-separation-4 campaign, PHASE6
    # (PHASE6_DEFENSIVE_MODULE_INFO_BUFFER_READS.md) - a small, header-only,
    # bounded-read helper for GtePluginModuleInfo's fixed-size char[]
    # buffers (strnlen(buf, sizeof(buf)) + std::string(buf, len), never
    # trusting a plugin to self-terminate). Header-only/inline so it is
    # directly Tier-1-testable from tests/Core/FixedBufferReaderTests.cpp -
    # used by PluginHost.cpp's own "Loaded plugin '...'" log line.
    src/Core/Plugins/FixedBufferReader.h
    # editor-core-separation-3 campaign, PHASE3
    # (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md) - PluginRenderPassBuilderAdapter,
    # the gte_core-side implementation of IPluginRenderPassBuilder, constructed
    # fresh per-view, per-frame, by the "PluginRenderFeatures" provider
    # (Core.cpp's own RegisterOffscreenRenderPipelineProviders()).
    src/Core/Plugins/PluginRenderPassBuilderAdapter.h
    src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp
    # editor-core-separation-4 campaign, PHASE5
    # (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
    # small, pure, Tier-1-testable helper counting how many loaded plugin
    # modules implement IRenderFeatureModule_v1 - used by Core::LoadPlugins()
    # to log a warning when 2+ such plugins are loaded (their render output
    # silently overwrites each other today - see docs/conventions/plugin-
    # architecture.md).
    src/Core/Plugins/PluginRenderFeatureDiagnostics.h
    src/Core/Plugins/PluginRenderFeatureDiagnostics.cpp
    # editor-core-separation-1 campaign, PHASE10
    # (PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md) - the
    # abstract surface-provider interface Renderer/VulkanSurface now depend on
    # instead of the concrete Window class (header-only, no .cpp - every
    # method is pure virtual). Window implements it (src/Window/Window.h).
    src/Core/ISurfaceProvider.h
    # editor-core-separation-1 campaign, PHASE12
    # (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - the new gte::Core
    # class (skeleton only this phase - no real per-frame orchestration
    # logic yet, that's PHASE13's job), plus its own small, dedicated
    # supporting headers (InputFrame.h/IHostServices.h), mirroring
    # ISurfaceProvider.h's own "one small header per concept" precedent
    # immediately above.
    src/Core/InputFrame.h
    src/Core/IHostServices.h
    src/Core/Core.h
    src/Core/Core.cpp
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
    src/ECS/EntityQuery.h
    src/ECS/EntityQuery.cpp
    src/ECS/Components/Transform.h
    src/ECS/Components/MeshRenderer.h
    src/ECS/Components/Camera.h
    src/ECS/Components/DirectionalLight.h
    src/ECS/Components/Name.h
    src/ECS/Components/MeshAssetSource.h
    src/ECS/Components/PrimitiveSource.h
    src/ECS/Components/SkeletalAnimator.h
    src/ECS/Components/ResolvedAnimationPose.h
    src/ECS/Components/DynamicChainRig.h
    src/ECS/Reflection/MathJsonAdapters.h
    src/ECS/Reflection/ComponentTypeDescriptor.h
    src/ECS/Reflection/ComponentTypeRegistry.h
    src/ECS/Reflection/ComponentTypeRegistry.inl
    src/ECS/Reflection/ComponentTypeRegistry.cpp
    src/ECS/Reflection/ReflectFieldMacros.h
    src/ECS/Reflection/BuiltinComponentReflection.cpp
    src/Animation/BoneLocalOffset.h
    src/Animation/BoneChainResolver.h
    src/Animation/BonePoseMath.h
    src/Animation/SkeletonPose.h
    src/Animation/SkeletonPose.cpp
    src/Animation/IkSolver.h
    src/Animation/IkSolver.cpp
    src/Animation/BoneWorldMatrixQuery.h
    src/Animation/BoneWorldMatrixQuery.cpp
    src/Animation/AppendBoneSolver.h
    src/Animation/AppendBoneSolver.cpp
    src/Animation/MotionSampler.h
    src/Animation/MotionSampler.cpp
    src/Animation/AnimationPoseEvaluator.h
    src/Animation/AnimationPoseEvaluator.cpp
    src/Animation/VertexSkinning.h
    src/Animation/VertexSkinning.cpp
    src/Physics/VerletParticle.h
    src/Physics/VerletIntegration.h
    src/Physics/VerletIntegration.cpp
    src/Physics/ChainConstraints.h
    src/Physics/ChainConstraints.cpp
    src/Physics/WindField.h
    src/Physics/WindField.cpp
    src/Physics/DynamicChainDefinition.h
    src/Physics/DynamicChainDefinition.cpp
    src/Physics/ModelColliderDefinition.h
    src/Physics/ModelColliderDetection.h
    src/Physics/ModelColliderDetection.cpp
    src/Physics/DynamicChainRuntimeState.h
    src/Physics/DynamicChainSolver.h
    src/Physics/DynamicChainSolver.cpp
    src/Physics/BoneChainPhysicsResolver.h
    src/Physics/BoneChainPhysicsResolver.cpp
    src/Physics/GlobalPhysicsSettings.h
    src/Physics/FixedTimestepAccumulator.h
    src/Physics/FixedTimestepAccumulator.cpp
    src/Physics/DynamicChainDetection.h
    src/Physics/DynamicChainDetection.cpp
    src/Physics/RigidBodyJointGraph.h
    src/Physics/RigidBodyJointGraph.cpp
    src/Physics/SphereCollider.h
    src/Physics/SphereCollider.cpp
    src/Physics/BoxCollider.h
    src/Physics/BoxCollider.cpp
    src/Physics/CapsuleCollider.h
    src/Physics/CapsuleCollider.cpp
    src/Physics/Collider.h
    src/Physics/Collider.cpp
    src/Physics/JointPhysicsOverrideApplication.h
    src/Physics/JointPhysicsOverrideApplication.cpp
    src/Scene/SceneDocument.h
    src/Scene/SceneJsonFormat.h
    src/Scene/SceneJsonFormat.cpp
    src/Scene/SceneBuilder.h
    src/Scene/SceneBuilder.cpp
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
    src/Assets/StlLoader.h
    src/Assets/StlLoader.cpp
    src/Assets/MeshNormalRecompute.h
    src/Assets/MeshNormalRecompute.cpp
    src/Assets/MeshNormalRecomputePersistence.h
    src/Assets/MeshNormalRecomputePersistence.cpp
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
    # editor-core-separation-1 campaign, PHASE14
    # (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) - live re-investigation
    # found SdlMemoryTracker.h/.cpp (#include <SDL3/SDL_stdinc.h>),
    # Application.cpp/.h (#include <SDL3/SDL.h>, SdlContext's SDL_Init()/
    # SDL_Quit(), Run()'s own SDL_Event pump loop), and
    # Application/EventTranslator.cpp/.h (#include <SDL3/SDL.h>, the SDL_Event
    # -> gte::Event translator) ALL directly touch real SDL3 headers/
    # functions - not just Window.cpp/SdlContext, as this phase's own plan
    # originally assumed. All three relocated into gte_editor's own
    # target_sources() below, alongside Window.cpp/.h (see that block's own
    # comment for the full rationale) - see PHASE14_COMPLETION_REPORT.md.
    src/Application/MemorySnapshotBuilder.h
    src/Application/FrameCaptureBridge.h
    src/Application/FrameCaptureBridge.cpp
    src/Application/EngineCommandBridge.h
    src/Application/EngineCommandBridge.cpp
    src/Application/EditorUiCommandBridge.h
    src/Application/EditorUiCommandBridge.cpp
    src/Application/AssetImportCommandBridge.h
    src/Application/AssetImportCommandBridge.cpp
    src/Application/FrameDebuggerCommandBridge.h
    src/Application/FrameDebuggerCommandBridge.cpp
    # editor-core-separation-3 campaign, PHASE4
    # (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) - EditorPanelRegistry,
    # replacing EditorPanelCatalog.h's former fixed kKnownEditorPanelNames[]
    # array with a real, runtime-populated registry (built-in panels PLUS
    # every loaded plugin's own IEditorPanelModule_v1::GetPanelName()).
    src/Core/EditorPanelRegistry.h
    src/Core/EditorPanelRegistry.cpp
    # editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
    # Locked Design Decision #9) - unlike every other file under src/Editor/,
    # NullEditorLayer.cpp belongs in gte_core's OWN unconditional source list,
    # not gte_editor's: a future Player host (linking gte_core alone, never
    # seeing gte_editor's source) still needs gte::CreateNullEditorLayer()
    # (see EditorLayer.h) to resolve to something, with zero ImGui/SDL
    # dependency. This is the ONE Editor-tree file gte_core itself compiles.
    src/Editor/NullEditorLayer.cpp
    src/Application/EngineCommandDispatch.h
    src/Application/EngineCommandDispatch.cpp
    src/Application/RenderPasses.h
    src/Application/RenderPasses.cpp
    src/Application/AtmospherePassSequence.h
    src/Application/AtmospherePassSequence.cpp
    src/Event/Event.h
    src/Input/InputState.cpp
    src/Input/InputState.h
    # editor-core-separation-1 campaign, PHASE14 - Window.cpp/.h relocated
    # into gte_editor's own target_sources() below (see that block's own
    # comment) - gte_core drops SDL3::SDL3 at link time entirely this phase.
    src/Renderer/Renderer.cpp
    src/Renderer/Renderer.h
    src/Renderer/FramePresenter.cpp
    src/Renderer/FramePresenter.h
    src/Renderer/FrameRecorder.cpp
    src/Renderer/FrameRecorder.h
    src/Renderer/DrawStats.cpp
    src/Renderer/DrawStats.h
    src/Renderer/GpuTiming.cpp
    src/Renderer/GpuTiming.h
    src/Renderer/GpuTimingService.cpp
    src/Renderer/GpuTimingService.h
    src/Renderer/SwapchainCaptureService.cpp
    src/Renderer/SwapchainCaptureService.h
    src/Renderer/GpuResourceFactory.cpp
    src/Renderer/GpuResourceFactory.h
    src/Renderer/Buffer.cpp
    src/Renderer/Buffer.h
    src/Renderer/Texture2D.cpp
    src/Renderer/Texture2D.h
    src/Renderer/VolumeTexture.cpp
    src/Renderer/VolumeTexture.h
    src/Renderer/VolumeTarget.h
    src/Renderer/VolumeTexturePreviewMath.cpp
    src/Renderer/VolumeTexturePreviewMath.h
    src/Renderer/VolumeTexturePreviewRenderer.cpp
    src/Renderer/VolumeTexturePreviewRenderer.h
    src/Renderer/TextureHandle.h
    src/Renderer/MaterialTexture.h
    src/Renderer/Mesh.h
    src/Renderer/MeshHandle.h
    src/Renderer/PipelineHandle.h
    src/Renderer/ResourcePool.h
    src/Renderer/Pipeline.cpp
    src/Renderer/Pipeline.h
    src/Renderer/ComputePipeline.cpp
    src/Renderer/ComputePipeline.h
    src/Renderer/ComputeDescriptorSet.cpp
    src/Renderer/ComputeDescriptorSet.h
    src/Renderer/ComputeDispatch.h
    src/Renderer/Vertex.h
    src/Renderer/MeshVertex.h
    src/Renderer/GpuSkinning/GpuSkinningTypes.h
    src/Renderer/GpuSkinning/GpuSkinningTypes.cpp
    src/Renderer/GpuSkinning/GpuSkinningPipelines.h
    src/Renderer/GpuSkinning/GpuSkinningPipelines.cpp
    src/Renderer/Culling/CullingTypes.h
    src/Renderer/Culling/CullingTypes.cpp
    src/Renderer/Culling/CullingPipelines.h
    src/Renderer/Culling/CullingPipelines.cpp
    src/Renderer/Culling/GpuDrivenBatchCache.h
    src/Renderer/Culling/GpuDrivenBatchCache.cpp
    src/Renderer/Culling/GpuDrivenBatchDebugInfo.h
    src/Renderer/IndirectDrawTypes.h
    src/Renderer/Atmosphere/AtmosphereTypes.h
    src/Renderer/Atmosphere/AtmosphereParameters.h
    src/Renderer/Atmosphere/AtmosphereParameters.cpp
    src/Renderer/Atmosphere/AtmosphereMath.h
    src/Renderer/Atmosphere/AtmosphereMath.cpp
    src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h
    src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.cpp
    src/Renderer/Atmosphere/DirectionalLightResolver.h
    src/Renderer/Atmosphere/DirectionalLightResolver.cpp
    src/Renderer/Atmosphere/AtmosphereLutRenderer.h
    src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp
    src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h
    src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.cpp
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
    src/Renderer/Vulkan/VulkanQueryPool.cpp
    src/Renderer/Vulkan/VulkanQueryPool.h
    src/Renderer/Vulkan/FormatCapabilities.cpp
    src/Renderer/Vulkan/FormatCapabilities.h
    src/Renderer/Vulkan/ShaderModule.cpp
    src/Renderer/Vulkan/ShaderModule.h
    src/Renderer/Vulkan/DescriptorSetLayoutBuilder.cpp
    src/Renderer/Vulkan/DescriptorSetLayoutBuilder.h
    src/Renderer/RenderGraph/RenderGraphTypes.h
    src/Renderer/RenderGraph/RenderGraphTypes.cpp
    src/Renderer/RenderGraph/RenderGraphBuilder.h
    src/Renderer/RenderGraph/RenderGraphBuilder.cpp
    src/Renderer/RenderGraph/RenderGraphCompiler.h
    src/Renderer/RenderGraph/RenderGraphCompiler.cpp
    src/Renderer/RenderGraph/RenderGraphResourcePool.h
    src/Renderer/RenderGraph/RenderGraphResourcePool.cpp
    src/Renderer/RenderGraph/RenderGraphBarrierPlanner.h
    src/Renderer/RenderGraph/RenderGraphBarrierPlanner.cpp
    src/Renderer/RenderGraph/RenderGraphNameSlotTable.h
    src/Renderer/RenderGraph/RenderGraphTimestampPool.h
    src/Renderer/RenderGraph/RenderGraphTimestampPool.cpp
    src/Renderer/RenderGraph/RenderGraph.h
    src/Renderer/RenderGraph/RenderGraph.cpp
    src/Renderer/RenderGraph/RenderGraphSnapshot.h
    src/Renderer/RenderGraph/RenderGraphSnapshot.cpp
    src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h
    src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.cpp
    src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h
    src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.cpp
    src/Renderer/RenderGraph/RenderPipeline.h
    src/Renderer/RenderGraph/RenderPipeline.cpp
    src/Renderer/RenderGraph/RenderPassGroupRegistry.h
    src/Renderer/RenderGraph/RenderPassGroupRegistry.cpp
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
    src/Game/Instantiation/MeshAssetPartGrouping.cpp
    src/Game/Instantiation/MeshAssetPartGrouping.h
    src/Game/Instantiation/MeshInstantiationSystem.cpp
    src/Game/Instantiation/MeshInstantiationSystem.h
    src/Game/Animation/SkeletalRigCache.h
    src/Game/Animation/GpuSkinningRigCache.cpp
    src/Game/Animation/GpuSkinningRigCache.h
    src/Game/Animation/AnimationClipCache.cpp
    src/Game/Animation/AnimationClipCache.h
    src/Game/Animation/ResolvedAnimationBindingCache.h
    src/Game/Animation/AnimationSystem.cpp
    src/Game/Animation/AnimationSystem.h
    src/Game/Physics/DynamicChainRigCache.h
    src/Game/Physics/DynamicChainPhysicsPersistence.cpp
    src/Game/Physics/DynamicChainPhysicsPersistence.h
    src/Game/Physics/PhysicsSystem.cpp
    src/Game/Physics/PhysicsSystem.h
    src/Game/Game.cpp
    src/Game/Game.h
    src/Game/EngineCommandResults.h
    src/Game/DrawCommand.h
    src/Game/RenderBatching.h
    src/Game/RenderBatching.cpp
    src/Game/RenderSystem.cpp
    src/Game/RenderSystem.h
    src/Profiling/ProfilingTypes.h
    src/Profiling/ProfilingClock.h
    src/Profiling/ProfilingClock.cpp
    src/Profiling/FrameProfiler.h
    src/Profiling/FrameProfiler.cpp
    src/Profiling/ScopeTimer.h
    src/Profiling/JobScopeTimer.h
    src/Profiling/FrameGraphData.h
    src/Profiling/FrameGraphData.cpp
    src/Profiling/WorkerTimelineData.h
    src/Profiling/WorkerTimelineData.cpp
    src/Jobs/JobTypes.h
    src/Jobs/JobQueue.h
    src/Jobs/JobQueue.cpp
    src/Jobs/JobSystem.h
    src/Jobs/JobSystem.cpp
    src/Jobs/JobDispatch.h
    src/Jobs/JobDispatch.cpp
    src/Jobs/JobContinuation.h
    src/Jobs/JobContinuation.cpp
    src/Network/NetworkRoutes.h
    src/Network/NetworkRoutes.cpp
    src/Network/NetworkServer.h
    src/Network/NetworkServer.cpp
    src/Encoding/Base64.h
    src/Encoding/Base64.cpp
    src/Encoding/PixelConversion.h
    src/Encoding/PixelConversion.cpp
    src/Encoding/PngEncoder.h
    src/Encoding/PngEncoder.cpp
    src/Encoding/DepthVisualization.h
    src/Encoding/DepthVisualization.cpp
    src/Encoding/HdrColorVisualization.h
    src/Encoding/HdrColorVisualization.cpp
)

# PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.5/3.6 - the
# RUNTIME output folder every built plugin .dll (demo_hello_world here;
# demo_render_feature/demo_editor_panel in later phases) is copied into. A
# plain CMake variable, NOT a hardcoded $<TARGET_FILE_DIR:GreatTamanaEditor>
# generator expression, because GreatTamanaEditor does not exist as a target
# at all in a GTE_CORE_STANDALONE_PROBE_ONLY=ON configuration (see the guard
# around its own add_executable() call further down this file) - BOTH this
# phase's own new standalone handshake probe AND a future isolation probe
# configure with GTE_CORE_STANDALONE_PROBE_ONLY=ON specifically so they need
# demo_hello_world.dll to still build successfully there. Placed IMMEDIATELY
# after gte_core's own add_library() block's closing parenthesis (so it is
# reached unconditionally, in BOTH a normal configure AND a
# GTE_CORE_STANDALONE_PROBE_ONLY=ON one) and BEFORE the
# if(NOT GTE_CORE_STANDALONE_PROBE_ONLY) guard immediately below, which wraps
# the gte_editor library block - demo_hello_world must build even when
# gte_editor itself is skipped, and it depends on nothing from gte_core at
# all (Locked Design Decision #2 - it links ONLY gte_plugin_abi).
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    set(GTE_PLUGIN_RUNTIME_OUTPUT_DIR "${CMAKE_BINARY_DIR}/plugins")
else()
    set(GTE_PLUGIN_RUNTIME_OUTPUT_DIR "$<TARGET_FILE_DIR:GreatTamanaEditor>/plugins")
endif()

# editor-core-separation-4 campaign, PHASE2
# (PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md) - gte_plugin_abi is an
# unconditional add_subdirectory() call, ALWAYS run regardless of
# GTE_ENABLE_PLUGINS. It is a plain, header-only INTERFACE library (cheap to
# configure - one configure_file() call, zero real compilation), and gte_core
# unconditionally links against it (see target_link_libraries(gte_core
# PUBLIC gte_plugin_abi) below) because src/Core/Plugins/PluginHost.h/.cpp
# are themselves in gte_core's own UNCONDITIONAL source file list - they
# always compile and always need this target's include directories, in
# EVERY configuration, including GTE_ENABLE_PLUGINS=OFF (where PluginHost's
# class still compiles and is still testable, it is simply never CALLED at
# runtime - EditorHost.cpp's own m_core.LoadPlugins() call site is what is
# actually gated, via #if GTE_ENABLE_PLUGINS). Confirmed, real, previously-
# broken bug this phase fixes: before this change, this whole
# add_subdirectory() call (including gte_plugin_abi's own) was gated behind
# `if(GTE_ENABLE_PLUGINS)`, so a GTE_ENABLE_PLUGINS=OFF configure meant
# gte_plugin_abi never existed as a CMake target at all, while
# target_link_libraries(gte_core PUBLIC gte_plugin_abi) further down this
# file referenced it completely unconditionally - a guaranteed configure/link
# failure, never exercised before this phase (see
# task_manager/editor-core-separation-4/PHASE2_GTE_ENABLE_PLUGINS_OFF_BUILD_FIX.md).
add_subdirectory(plugins/gte_plugin_abi)

if(GTE_ENABLE_PLUGINS)
    add_subdirectory(plugins/demo_hello_world)
    # editor-core-separation-3 campaign, PHASE3
    # (PHASE3_RUNTIME_RENDER_FEATURE_CAPABILITY.md, Step 3.5) - the
    # IRenderFeatureModule_v1 throwaway demo (Milestone 1).
    add_subdirectory(plugins/demo_render_feature)
    # editor-core-separation-4 campaign, PHASE5
    # (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) - a
    # SECOND, genuinely independent throwaway demo implementing
    # IRenderFeatureModule_v1, so the "2+ plugins implementing the same
    # capability" path is exercised for real, not just theorized.
    add_subdirectory(plugins/demo_render_feature_second)
    # editor-core-separation-3 campaign, PHASE4
    # (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md, Step 3.9) - the
    # IEditorPanelModule_v1 throwaway demo (Milestone 2). Only depends on
    # gte_plugin_abi (Locked Design Decision #2), so it builds correctly even
    # in a GTE_CORE_STANDALONE_PROBE_ONLY=ON configuration where gte_editor
    # itself is skipped entirely.
    add_subdirectory(plugins/demo_editor_panel)
endif()

# editor-core-separation-1 campaign, PHASE18
# (PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md) - gte_editor
# itself (and everything it needs - ImGui/ImGuizmo/SDL3) is skipped entirely
# when GTE_CORE_STANDALONE_PROBE_ONLY is ON, so
# tools/ci/gte_core_standalone_probe/ can prove gte_core links standalone with
# zero gte_editor/SDL/ImGui involvement (design doc Section 7.3/9, Phase 10).
# Never set this manually for normal development - it is used exclusively by
# that probe project.
if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)
# --- Editor library ------------------------------------------------------
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md) -
# a real, separately-linked static library, depending on gte_core, owning
# everything that used to live behind the old GTE_ENABLE_EDITOR macro
# (see PHASE8_COMPLETION_REPORT.md) - ImGui/ImGuizmo, gizmos, every panel,
# the Frame Debugger, and every Editor-only validation/debug tool.
# ALWAYS built, unconditionally, every single time this repo is configured -
# there is no CMake option anywhere that skips it (Locked Design Decision #9/
# Rule 4, PHASE0_MASTER_STRATEGY.md). Every file listed below (besides
# ImGuiEditorLayer.cpp itself) is part of the same Editor/ImGui boundary -
# DockLayout.* and Panels/*.* are only ever compiled here, alongside it, and
# are just as free to include ImGui/SDL headers directly - see AGENTS.md,
# "Editor Module Structure".
#
# ODR/link-order hazard fix (Locked Design Decision #9): gte_core's own
# NullEditorLayer.cpp (added to gte_core's unconditional source list above)
# and this target's own ImGuiEditorLayer.cpp used to BOTH define
# gte::CreateEditorLayer() - safe only while exactly one of the two ever
# compiled into any given build (the old GTE_ENABLE_EDITOR on/off switch).
# Now that gte_editor is ALWAYS linked alongside gte_core into this repo's
# one executable, both files being in the SAME final link would have been a
# real ODR violation with no guaranteed compiler/linker error. Fixed by
# renaming NullEditorLayer.cpp's own factory function to
# gte::CreateNullEditorLayer() (see EditorLayer.h/NullEditorLayer.cpp) -
# gte::CreateEditorLayer() now has exactly ONE definition in the entire
# repository, right here, in ImGuiEditorLayer.cpp.
add_library(gte_editor STATIC
    # editor-core-separation-1 campaign, PHASE14
    # (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) - the composition root
    # (Application) plus every file that directly touches real SDL3 headers/
    # functions, relocated here from gte_core's own unconditional source list
    # above so gte_core.a can drop SDL3::SDL3 at link time entirely:
    #   - Window.cpp/.h - the RAII SDL_Window wrapper (SDL_CreateWindow/
    #     SDL_DestroyWindow/SDL_Vulkan_GetInstanceExtensions/
    #     SDL_Vulkan_CreateSurface).
    #   - Memory/SdlMemoryTracker.h/.cpp - SDL_SetMemoryFunctions() hook (only
    #     ever installed by SdlContext, below, and only ever read by the
    #     Editor's own "Memory" panel - src/Editor/Panels/MemoryPanel.cpp,
    #     already gte_editor).
    #   - Application/EventTranslator.h/.cpp - the SDL_Event -> gte::Event
    #     translator.
    #   - Application/Application.h/.cpp - PHASE14 relocated this file here;
    #     PHASE17 (PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md)
    #     deleted it outright once EditorHost (below) fully replaced it as
    #     this repo's real composition root - confirmed via search_in_dir
    #     that nothing else in this codebase ever referenced it.
    src/Window/Window.h
    src/Window/Window.cpp
    src/Memory/SdlMemoryTracker.h
    src/Memory/SdlMemoryTracker.cpp
    src/Application/EventTranslator.h
    src/Application/EventTranslator.cpp
    # editor-core-separation-1 campaign, PHASE15
    # (PHASE15_EDITORHOST_COMPOSITION_ROOT_CORE_CONSTRUCTION.md) - the new
    # composition root REPLACING Application's role (structure only this
    # phase - Run() is a deliberate, temporary stub; the real main loop +
    # automation bridges are PHASE16's job). main.cpp (this executable's own
    # entry point) now constructs EditorHost instead of Application.
    src/Editor/EditorHostServices.h
    src/Editor/EditorHostServices.cpp
    src/Editor/EditorHost.h
    src/Editor/EditorHost.cpp
    src/Editor/EditorContext.h
    src/Editor/ProjectRootPath.h
    src/Editor/ProjectRootPath.cpp
    src/Editor/SceneIO.h
    src/Editor/SceneIO.cpp
    # editor-core-separation-1 campaign, PHASE6
    # (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - the real
    # ISceneIOCapability implementation wrapping SceneIO.h above.
    src/Editor/EditorSceneIOCapability.h
    src/Editor/EditorSceneIOCapability.cpp
    # editor-core-separation-2 campaign, PHASE3
    # (PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md) - the real
    # ILogQueryCapability implementation wrapping Logger.h above.
    src/Editor/EditorLogQueryCapability.h
    src/Editor/EditorLogQueryCapability.cpp
    src/Editor/Selection.h
    src/Editor/Selection.cpp
    src/Editor/EditorCamera.h
    src/Editor/EditorCamera.cpp
    src/Editor/SceneGridMath.h
    src/Editor/SceneGridMath.cpp
    src/Editor/SceneGridRenderer.h
    src/Editor/SceneGridRenderer.cpp
    src/Editor/ComputeBlurValidation.h
    src/Editor/ComputeBlurValidation.cpp
    src/Editor/GBufferValidation.h
    src/Editor/GBufferValidation.cpp
    src/Editor/GpuDrivenBatchTestSpawner.h
    src/Editor/GpuDrivenBatchTestSpawner.cpp
    src/Editor/GpuSkinningValidation.h
    src/Editor/GpuSkinningValidation.cpp
    src/Editor/AtmosphereTransmittanceLutValidation.h
    src/Editor/AtmosphereTransmittanceLutValidation.cpp
    src/Editor/AtmosphereAerialPerspectiveLutInspection.h
    src/Editor/AtmosphereAerialPerspectiveLutInspection.cpp
    src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h
    src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.cpp
    src/Editor/TransformGizmo.h
    src/Editor/TransformGizmo.cpp
    src/Editor/DockLayout.h
    src/Editor/DockLayout.cpp
    src/Editor/PlaybackControls.h
    src/Editor/PlaybackControls.cpp
    src/Editor/ImGuiMemoryTracker.h
    src/Editor/ImGuiMemoryTracker.cpp
    src/Editor/Logger.h
    src/Editor/Logger.cpp
    src/Editor/MemoryPanelData.h
    src/Editor/MemoryPanelData.cpp
    src/Editor/ProfilerPanelData.h
    src/Editor/ProfilerPanelData.cpp
    src/Editor/JobsPanelData.h
    src/Editor/JobsPanelData.cpp
    src/Editor/LogPanelData.h
    src/Editor/LogPanelData.cpp
    src/Editor/FrameDebuggerData.h
    src/Editor/FrameDebuggerData.cpp
    src/Editor/FrameDebuggerCapture.h
    src/Editor/FrameDebuggerCapture.cpp
    # task_manager/editor-core-separation-1 campaign, PHASE2 - the two files
    # holding what used to be #if GTE_ENABLE_EDITOR-guarded code inside CORE,
    # always-compiled files (RenderPasses.cpp/RenderSystem.cpp) - see
    # PHASE2_FRAME_DEBUGGER_CAPTURE_POINTER_SAFETY_FIX.md.
    src/Editor/FrameDebuggerReplayPasses.cpp
    src/Editor/FrameDebuggerDrawRecording.cpp
    # editor-core-separation-1 campaign, PHASE4
    # (PHASE4_GPU_MEMORY_TRACKER_BUCKET_A_EXTRACTION.md) - the Editor-owned
    # overlay that gives GPU resources human-readable debug names now that
    # GpuMemoryTracker itself stores none.
    src/Editor/EditorGpuMemoryNameOverlay.h
    src/Editor/EditorGpuMemoryNameOverlay.cpp
    src/Editor/FrameDebuggerHistory.h
    src/Editor/FrameDebuggerHistory.cpp
    src/Editor/FrameDebuggerPreviewProcessing.h
    src/Editor/FrameDebuggerPreviewProcessing.cpp
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
    src/Editor/Panels/ProfilerPanel.h
    src/Editor/Panels/ProfilerPanel.cpp
    src/Editor/Panels/RenderGraphPanel.h
    src/Editor/Panels/RenderGraphPanel.cpp
    src/Editor/Panels/JobsPanel.h
    src/Editor/Panels/JobsPanel.cpp
    src/Editor/Panels/LogPanel.h
    src/Editor/Panels/LogPanel.cpp
    src/Editor/Panels/AtmospherePanel.h
    src/Editor/Panels/AtmospherePanel.cpp
    src/Editor/Panels/FrameDebuggerPanel.h
    src/Editor/Panels/FrameDebuggerPanel.cpp
    # editor-core-separation-3 campaign, PHASE4
    # (PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) -
    # PluginPanelDrawContextAdapter, gte_editor's own implementation of
    # IPluginPanelDrawContext, closing the ImGui-shared-global-context hazard
    # (PHASE0_MASTER_STRATEGY.md, Step 2.4).
    src/Editor/Plugins/PluginPanelDrawContextAdapter.h
    src/Editor/Plugins/PluginPanelDrawContextAdapter.cpp
    src/Editor/ImGuiEditorLayer.cpp
)

# The "Project" panel (see GTE_ENABLE_PROJECT_PANEL above) is independently
# toggleable even within the always-built Editor library - only
# compiled/linked in when ALSO on. ProjectPanelData.h/.cpp is the pure,
# Tier-1-testable logic (see tests/Editor/ProjectPanelDataTests.cpp);
# Panels/ProjectPanel.h/.cpp is the ImGui-facing class itself.
if(GTE_ENABLE_PROJECT_PANEL)
    target_sources(gte_editor PRIVATE
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
        src/Editor/ModelRigCache.h
        src/Editor/ModelRigCache.cpp
        src/Editor/RigidBodyWireframe.h
        src/Editor/RigidBodyWireframe.cpp
        src/Editor/RigidBodyGroupSelection.h
        src/Editor/RigidBodyGroupSelection.cpp
        src/Editor/FlatListRangeSelection.h
        src/Editor/FlatListRangeSelection.cpp
        src/Editor/BoneViewerWindow.h
        src/Editor/BoneViewerWindow.cpp
    )
endif()

# gte_editor depends on gte_core - ONE WAY, never the reverse (Rule 2,
# PHASE0_MASTER_STRATEGY.md). PUBLIC so any consumer of gte_editor
# (GreatTamanaEditor, GreatTamanaEngineTests) transitively sees gte_core's
# own PUBLIC include directories/compile definitions/link libraries too,
# without repeating them.
target_link_libraries(gte_editor PUBLIC gte_core)
target_link_libraries(gte_editor PRIVATE imgui imguizmo)
# editor-core-separation-1 campaign, PHASE14
# (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) - SDL3 linkage moves here now
# that Window.cpp/SdlContext (inside EditorHost.cpp)/EventTranslator.cpp all
# physically compile as part of gte_editor (see this target's own
# target_sources() block above). PUBLIC (matching gte_core's own former
# visibility) since gte_editor's public headers (EditorHost.h, transitively
# via EventTranslator.h/Window.h) leak real SDL3 types/declarations to this
# target's own consumers (GreatTamanaEditor's main.cpp, GreatTamanaEngineTests).
target_link_libraries(gte_editor PUBLIC SDL3::SDL3)
endif() # GTE_CORE_STANDALONE_PROBE_ONLY - matches the if() right before the "--- Editor library ---" comment above.

# Exposes GTE_ENABLE_PROJECT_PANEL to C++ code as an actual 1/0 preprocessor
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

# Same idea again - exposes GTE_ENABLE_JOB_SYSTEM to C++ code as an actual
# 1/0 preprocessor macro, needed by src/Jobs/JobSystem.h/.cpp's own
# `#if GTE_ENABLE_JOB_SYSTEM` guards (see AGENTS.md, "Job System"). PUBLIC
# for the same reason as the two definitions above: GreatTamanaEngineTests
# (tests/Jobs/*.cpp) must see the exact same macro value gte_core itself was
# built with.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_JOB_SYSTEM=$<BOOL:${GTE_ENABLE_JOB_SYSTEM}>)

# Same idea again - exposes GTE_ENABLE_NETWORK to C++ code as an actual 1/0
# preprocessor macro, needed by EditorHost.cpp's own
# `#if GTE_ENABLE_NETWORK` guard around NetworkServer::Start() (see
# AGENTS.md, "Networking"). PUBLIC for the same reason as the three
# definitions above: GreatTamanaEngineTests must see the exact same macro
# value gte_core itself was built with.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_NETWORK=$<BOOL:${GTE_ENABLE_NETWORK}>)

# Same idea again - exposes GTE_ENABLE_PLUGINS to C++ code as an actual 1/0
# preprocessor macro, needed by EditorHost.cpp's own
# `#if GTE_ENABLE_PLUGINS` guard around Core::LoadPlugins() (see
# PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6). PUBLIC for the
# same reason as the definitions above.
target_compile_definitions(gte_core PUBLIC GTE_ENABLE_PLUGINS=$<BOOL:${GTE_ENABLE_PLUGINS}>)

# PUBLIC (not PRIVATE) for the same reason as the compile definition above:
# any consumer of gte_core (GreatTamanaEditor's main.cpp, and every test in
# GreatTamanaEngineTests) #includes engine headers that transitively need
# volk/vma/stb_image/KTX/httplib's include directories and, ultimately, their
# libraries at final-link time - PUBLIC is what propagates both automatically
# instead of every consumer having to repeat them. httplib is included here
# purely as a debugging/tooling dependency (see cmake/FetchHttplib.cmake) -
# nothing in gte_core uses it yet.
#
# editor-core-separation-1 campaign, PHASE14
# (PHASE14_WINDOW_SDL_RELOCATION_TO_EDITOR.md) - SDL3::SDL3 REMOVED from this
# line entirely (it used to be the first entry here). gte_core.a's own
# translation units contain ZERO #include of any real SDL3 header and ZERO
# reference to any real SDL3 function symbol as of this phase (Window.cpp/
# Application.cpp/EventTranslator.cpp/SdlMemoryTracker.cpp all relocated into
# gte_editor; FrameProfiler.cpp/WorkerTimelineData.cpp fixed to route through
# Profiling/ProfilingClock.cpp instead, mirroring ScopeTimer.h/JobScopeTimer.h's
# own Phase 11 fix) - see tests/Build/SdlLinkageRegressionTests.cpp's new
# GteCoreArchiveNoLongerReferencesRealSdl3FunctionSymbols test, which
# mechanically proves this against the real, built libgte_core.a bytes.
target_include_directories(gte_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(gte_core PUBLIC volk vma stb_image stb_image_write KTX::ktx httplib nlohmann_json)

# editor-core-separation-3 campaign, PHASE2
# (PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.3) -
# src/Core/Plugins/PluginHost.cpp needs gte_plugin_abi's own include
# directories (both the plain committed headers AND the CMake-generated
# GtePluginAbiFingerprintGenerated.h - see plugins/gte_plugin_abi/CMakeLists.txt).
# PUBLIC for the same reason as everything else linked into gte_core PUBLIC
# above - Core.h itself #includes Plugins/PluginHost.h, so any consumer of
# gte_core needs this propagated too.
target_link_libraries(gte_core PUBLIC gte_plugin_abi)

# src/Jobs/JobSystem.cpp/.h use std::thread/std::mutex/std::condition_variable
# unconditionally (the class itself always compiles - see JobSystem.h's own
# comment - only its INTERNAL behavior is gated by GTE_ENABLE_JOB_SYSTEM).
# On some toolchains (notably MinGW/GCC) linking a real threading
# implementation needs an explicit Threads::Threads - find_package(Threads)
# is the portable, CMake-blessed way to find whatever that toolchain needs
# (a plain compiler flag on MSVC, -lpthread-equivalent elsewhere) rather
# than hardcoding a library name. PUBLIC for the same reason as everything
# else linked into gte_core PUBLIC above: GreatTamanaEngineTests also
# includes/links against this code path directly (tests/Jobs/*.cpp).
find_package(Threads REQUIRED)
target_link_libraries(gte_core PUBLIC Threads::Threads)

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

# editor-core-separation-2 campaign, PHASE4 - the REAL, mechanical proof
# that gte_core.a links standalone (design doc Section 1.3, Rule 3) - a
# genuinely separate concern from GTE_CORE_STANDALONE_PROBE_ONLY's own
# original job (skipping gte_editor/the main executable/tests so
# tools/ci/gte_core_standalone_probe/ can build ONLY the gte_core ARCHIVE).
# This executable target only exists in THIS exact configuration
# (GTE_CORE_STANDALONE_PROBE_ONLY=ON) - it is never part of a normal
# `cmake -S . -B build` configure, exactly like gte_editor/GreatTamanaEditor
# are never part of a GTE_CORE_STANDALONE_PROBE_ONLY=ON configure (the
# inverse of the guard immediately below this block). See
# tools/ci/gte_core_player_link_probe/README.md for the exact command to
# build+link this by hand.
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    add_executable(gte_core_player_link_probe tools/ci/gte_core_player_link_probe/main.cpp)
    target_link_libraries(gte_core_player_link_probe PRIVATE gte_core)
    # editor-core-separation-3 campaign, PHASE5
    # (PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md, Step 3.2) - this probe's
    # main.cpp now genuinely CONSTRUCTS a real, headless gte::Core and calls
    # Core::LoadPlugins() against a real plugins/ folder (its own bonus check,
    # self-skipping on a machine without VK_EXT_headless_surface) - it
    # therefore participates in the plugin ABI boundary for real, not just as
    # an inert link target, and must get the same CRT-linkage treatment every
    # other plugin-ABI-boundary binary gets (PHASE0_MASTER_STRATEGY.md's
    # Locked Design Decision #4's own restated "JOINT requirement" rule).
    gte_apply_plugin_shared_crt_linkage(gte_core_player_link_probe)
    # Needs all three demo plugin .dlls to genuinely exist in its own
    # <inner-build-dir>/plugins/ folder before its bonus check runs (same
    # reasoning as gte_plugin_abi_handshake_probe's own
    # add_dependencies(... demo_hello_world) below - CMake has no way to know
    # a runtime-only LoadLibraryW()/PluginHost::LoadPlugins() dependency on
    # its own).
    add_dependencies(gte_core_player_link_probe demo_hello_world demo_render_feature demo_editor_panel)

    # PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.6 - a
    # tiny, PERMANENT, checked-in probe proving the whole plugin ABI
    # handshake works end-to-end OUTSIDE GreatTamanaEditor.exe entirely -
    # needs zero gte_core/gte_editor at all, only gte_plugin_abi + a real,
    # already-built demo_hello_world.dll to LoadLibraryW() at runtime.
    add_executable(gte_plugin_abi_handshake_probe tools/ci/gte_plugin_abi_handshake_probe/main.cpp)
    target_link_libraries(gte_plugin_abi_handshake_probe PRIVATE gte_plugin_abi)
    gte_apply_plugin_shared_crt_linkage(gte_plugin_abi_handshake_probe)
    # This probe LoadLibraryW()s demo_hello_world.dll at runtime rather than
    # linking it - CMake has no way to know that dependency on its own, so
    # it must be spelled out explicitly or a `cmake --build` of only this
    # probe's own target will never actually produce the .dll it needs.
    add_dependencies(gte_plugin_abi_handshake_probe demo_hello_world)

    # PHASE5_PLAYER_PROCESS_PLUGIN_ISOLATION_PROBE.md, Step 3.1 - a tiny,
    # PERMANENT, checked-in probe proving "always all-in" (source design doc
    # Section 0/7) stays editor-clean from a genuinely Player-shaped process
    # (Milestone 3) - links gte_core.a ALONE (no gte_editor/SDL/ImGui), uses
    # gte::PluginHost directly, and queries ONLY IRenderFeatureModule_v1 -
    # this probe's own main.cpp never references IEditorPanelModule_v1
    # anywhere in its own source, which IS the isolation proof itself.
    add_executable(gte_plugin_isolation_probe tools/ci/gte_plugin_isolation_probe/main.cpp)
    target_link_libraries(gte_plugin_isolation_probe PRIVATE gte_core gte_plugin_abi)
    gte_apply_plugin_shared_crt_linkage(gte_plugin_isolation_probe)
    # editor-core-separation-4 campaign, PHASE5
    # (PHASE5_MULTI_RENDER_FEATURE_PLUGIN_WARNING_AND_REGRESSION_LOCK.md) -
    # this probe's own main.cpp now requires LoadedModuleCount() == 4 exactly
    # (was 3) - needs all FOUR demo plugin .dlls (including the new
    # demo_render_feature_second) to genuinely exist in its own
    # <inner-build-dir>/plugins/ folder before it runs.
    add_dependencies(gte_plugin_isolation_probe demo_hello_world demo_render_feature demo_editor_panel demo_render_feature_second)
endif()

# editor-core-separation-1 campaign, PHASE18
# (PHASE18_HEADLESS_TEST_FIXTURE_AND_CORE_STANDALONE_PROBE.md) - the entire
# executable (which always links gte_editor, never gte_core alone - Rule 4,
# PHASE0_MASTER_STRATEGY.md) is skipped when GTE_CORE_STANDALONE_PROBE_ONLY is
# ON, so tools/ci/gte_core_standalone_probe/ can configure ONLY gte_core.
if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)
# --- Final executable ----------------------------------------------------
add_executable(GreatTamanaEditor src/main.cpp)
# editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
# Rule 4) - links gte_editor, NEVER gte_core directly; gte_editor's own
# PUBLIC dependency on gte_core (above) brings it in transitively. There is
# no configuration of this repo's own CMakeLists.txt in which this
# executable links gte_core alone.
#
# gte_editor is linked with a plain, single-pass target_link_libraries()
# call because gte_core's own object files carry zero unresolved references
# to any gte_editor-only symbol - see
# task_manager/editor-core-separation-2/PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md
# and PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md for the two
# fixes that made this true, and tools/ci/gte_core_player_link_probe/ for
# the permanent, mechanical proof (a real executable link against gte_core.a
# alone, no gte_editor involved at all).
target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)

# Compiles the engine's GLSL shader sources to SPIR-V at build time (see
# cmake/CompileShaders.cmake) - staged into "<exe dir>/shaders/" next to the
# built .exe, never committed as a binary (see .gitignore). Pipeline.cpp
# loads these compiled .spv files by relative path at runtime. Only the
# actual game executable needs these staged next to it - the test suite
# never renders anything, see tests/CMakeLists.txt.
gte_add_shader(GreatTamanaEditor src/Shaders/Triangle.vert)
gte_add_shader(GreatTamanaEditor src/Shaders/Triangle.frag)

# The shared "imported mesh" draw path (src/Renderer/MeshVertex.h,
# Pipeline.h's VertexLayout::PositionNormal, Game::CreateMeshEntityFromGtaFile()
# - src/Game/Game.h/.cpp) needs its own shader pair, compiled/staged the
# exact same way as Triangle.vert/frag above. Unconditional (not gated
# behind GTE_ENABLE_PROJECT_PANEL, unlike MeshPreview.vert/frag below) -
# Game.cpp (which loads these) is always compiled into gte_core, regardless
# of whether the Editor's "Project" panel (today's only way to actually
# trigger a spawn - see Panels/HierarchyPanel.cpp/ScenePanel.cpp) is built.
gte_add_shader(GreatTamanaEditor src/Shaders/Mesh.vert)
gte_add_shader(GreatTamanaEditor src/Shaders/Mesh.frag)

# GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
# PHASE2 (task_manager/render-pass-5/
# PHASE2_INSTANCED_DRAW_PRIMITIVE_AND_INDIRECT_SUBMIT.md) - the new
# instanced-draw vertex shader (src/Renderer/Pipeline.h's
# VertexLayout::PositionNormalInstanced), reusing Mesh.frag above UNCHANGED
# as its fragment shader. Unconditional, for the same reason as Mesh.vert/
# frag above - GpuResourceFactory.cpp (which loads this) is always
# compiled into gte_core.
gte_add_shader(GreatTamanaEditor src/Shaders/MeshInstanced.vert)

# GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
# PHASE3 (task_manager/render-pass-5/
# PHASE3_FRUSTUM_CULL_COMPUTE_SHADER_AND_PIPELINE.md) - the real culling
# compute shader (src/Renderer/Culling/CullingPipelines.h/.cpp). Unconditional
# for the same reason as SkinVerticesPositionNormal.comp below - this is a
# genuine gameplay/runtime feature (batched-entity frustum culling), not a
# debug/Editor-only tool, so it must work in a non-Editor, release build too.
gte_add_shader(GreatTamanaEditor src/Shaders/FrustumCull.comp)

# The TEXTURED half of that same "imported mesh" draw path (src/Renderer/
# MeshVertex.h's MeshVertexUv, Pipeline.h's VertexLayout::PositionNormalUv,
# Game::EnsureMeshAsset() - src/Game/Game.h/.cpp) - a per-material PMX
# submesh whose material resolves to a real texture file uses this pair
# instead of Mesh.vert/.frag above. Unconditional for the same reason as
# Mesh.vert/.frag above.
gte_add_shader(GreatTamanaEditor src/Shaders/TexturedMesh.vert)
gte_add_shader(GreatTamanaEditor src/Shaders/TexturedMesh.frag)

# GPU Vertex Skinning, Phase 2 (compute kernels) -
# task_manager/gpu_skinning/GPU_SKINNING_PHASE2_COMPUTE_KERNEL_STRATEGY_v1.md.
# Unconditional (NOT gated behind GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL,
# unlike BoxBlur.comp below) - src/Renderer/GpuSkinning/GpuSkinningPipelines.cpp,
# the only thing that ever loads these, is compiled into gte_core
# unconditionally (see gte_core's own target_sources() list above) since GPU
# skinning is a genuine gameplay/runtime feature, not a debug/Editor-only
# tool - it must work in a non-Editor, release build too.
gte_add_shader(GreatTamanaEditor src/Shaders/SkinVerticesPositionNormal.comp)
gte_add_shader(GreatTamanaEditor src/Shaders/SkinVerticesPositionNormalUv.comp)

# The Inspector's Mesh Asset 3D preview viewer (src/Editor/AssetPreviewMesh.h)
# needs its own small shader pair, compiled/staged the exact same way as
# Triangle.vert/frag above - see that file's class comment for why it needs
# a dedicated pipeline/vertex layout rather than reusing
# Renderer::CreatePipeline()'s built-in one. Only compiled/staged when
# GTE_ENABLE_PROJECT_PANEL is ON, since AssetPreviewMesh.cpp - the only thing
# that ever loads these - isn't compiled into gte_core otherwise (see
# GTE_ENABLE_PROJECT_PANEL's target_sources() block above).
if(GTE_ENABLE_PROJECT_PANEL)
    gte_add_shader(GreatTamanaEditor src/Shaders/MeshPreview.vert)
    gte_add_shader(GreatTamanaEditor src/Shaders/MeshPreview.frag)
endif()

# The Editor's "Scene" panel infinite ground grid (Unity-style procedural
# shader grid - see task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md).
# Always compiled/staged, since src/Editor/SceneGridRenderer.cpp (PHASE3) -
# the only thing that ever loads these - is always compiled into gte_core
# today (unlike MeshPreview.vert/frag above, this does NOT additionally need
# GTE_ENABLE_PROJECT_PANEL - SceneGridRenderer has no dependency on the
# Project panel at all). editor-core-separation-1 campaign, PHASE8 - this
# `if(TRUE)` is a temporary placeholder, see the main Editor source list's
# own comment above for the full reasoning.
if(TRUE)
    gte_add_shader(GreatTamanaEditor src/Shaders/SceneGrid.vert)
    gte_add_shader(GreatTamanaEditor src/Shaders/SceneGrid.frag)
endif()

# Phase 7 of the compute-shader campaign
# (COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md) - the compute
# box-blur validation shader (src/Editor/ComputeBlurValidation.h/.cpp).
# Always compiled/staged, since ComputeBlurValidation.cpp - the only thing
# that ever loads it - is always compiled into gte_core today.
# editor-core-separation-1 campaign, PHASE8 - this `if(TRUE)` is a temporary
# placeholder, see the main Editor source list's own comment above.
if(TRUE)
    gte_add_shader(GreatTamanaEditor src/Shaders/BoxBlur.comp)
endif()

# task_manager/mrt-1 campaign (Multi-Render-Target / G-Buffer support),
# PHASE4 (PHASE4_GBUFFER_VALIDATION_PASS_AND_SHADER.md) - the "GBuffer
# Validation" debug pass's own shaders (src/Editor/GBufferValidation.h/.cpp):
# GBufferValidation.vert/.frag (the 2-color-attachment MRT pass itself) and
# GBufferCopy.comp (the second pass's own trivial imageLoad/imageStore
# copy). Always compiled/staged, since GBufferValidation.cpp - the only
# thing that ever loads these - is always compiled into gte_core today
# (same precedent as BoxBlur.comp/SceneGrid.vert/frag above - this is the
# CORRECT precedent for an Editor-only debug pass, NOT Mesh.frag/
# TexturedMesh.frag's own unconditional registration, which is for core,
# always-compiled gameplay shaders instead). editor-core-separation-1
# campaign, PHASE8 - this `if(TRUE)` is a temporary placeholder.
if(TRUE)
    gte_add_shader(GreatTamanaEditor src/Shaders/GBufferValidation.vert)
    gte_add_shader(GreatTamanaEditor src/Shaders/GBufferValidation.frag)
    gte_add_shader(GreatTamanaEditor src/Shaders/GBufferCopy.comp)
endif()

# Atmosphere Scattering + Aerial Perspective campaign, Phase 3
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md)
# - the Transmittance LUT compute shader (src/Renderer/Atmosphere/
# AtmosphereLutRenderer.h/.cpp). Unconditional (NOT gated behind
# GTE_ENABLE_EDITOR, unlike BoxBlur.comp above) - this campaign's own Locked
# Design Decision 4 (ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md) makes atmosphere
# scattering an always-compiled core rendering feature, the same tier as the
# Render Graph or GPU Vertex Skinning, with no on/off switch of its own -
# AtmosphereLutRenderer.cpp (the only thing that ever loads this shader) is
# compiled into gte_core unconditionally (see gte_core's own target_sources()
# list above). EXTRA_DEPENDS lists the shared AtmosphereCommon.glsl include
# this shader #includes, so an edit to it correctly triggers glslc to
# recompile this (and every future atmosphere shader that also lists it).
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereTransmittanceLut.comp
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)

# Atmosphere Scattering + Aerial Perspective campaign, Phase 4
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md)
# - the Multi-Scattering LUT compute shader. Same unconditional (NOT
# GTE_ENABLE_EDITOR-gated) treatment as the Transmittance LUT shader above,
# for the exact same reason (Locked Design Decision 4 - always-compiled core
# rendering feature).
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereMultiScatteringLut.comp
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)

# Atmosphere Scattering + Aerial Perspective campaign, Phase 5
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE5_SKYVIEW_LUT_v1.md)
# - the Sky-View LUT compute shader. Same unconditional (NOT
# GTE_ENABLE_EDITOR-gated) treatment as the Transmittance/Multi-Scattering
# LUT shaders above, for the exact same reason (Locked Design Decision 4 -
# always-compiled core rendering feature).
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereSkyViewLut.comp
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)

# Atmosphere Scattering + Aerial Perspective campaign, Phase 6
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md)
# - the Aerial Perspective froxel-volume compute shader. Same unconditional
# (NOT GTE_ENABLE_EDITOR-gated) treatment as the Transmittance/Multi-
# Scattering/Sky-View LUT shaders above, for the exact same reason (Locked
# Design Decision 4 - always-compiled core rendering feature).
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereAerialPerspectiveVolume.comp
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)

# Atmosphere Scattering + Aerial Perspective campaign, Phase 7
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
# - the Sky Background pass (a graphics vert/frag pair, drawn via
# AtmosphereSkyBackgroundRenderer.h/.cpp) and the Aerial Perspective
# Composite pass (a compute shader). Same unconditional (NOT
# GTE_ENABLE_EDITOR-gated) treatment as every other atmosphere shader above,
# for the exact same reason (Locked Design Decision 4 - always-compiled core
# rendering feature) - this pass runs in BOTH the Game View (which still
# renders even in a -DGTE_ENABLE_EDITOR=OFF release build) and the
# Editor-only Scene View.
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereSkyBackground.vert)
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereSkyBackground.frag
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereAerialPerspectiveComposite.comp
    EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)

# Atmosphere Scattering + Aerial Perspective campaign, Phase 9
# (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE9_VALIDATION_DEBUG_TOOLING_AND_DOCS_v1.md,
# Step 3.2) - the volume-texture debug-visibility gap Phase 2 deliberately
# deferred. Same unconditional (NOT GTE_ENABLE_EDITOR-gated) treatment as
# every other atmosphere shader above - this pass is wired into the SAME
# always-running Game View pass sequence as the rest of this campaign's
# shaders. Does NOT #include AtmosphereCommon.glsl, so no EXTRA_DEPENDS.
gte_add_shader(GreatTamanaEditor src/Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp)

# network-impl-6 campaign, Phase 3
# (task_manager/network-impl-6/PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md) -
# the raymarch preview thumbnail compute pass behind GET /get_texture's
# volume-texture branch (Phase 4). Same unconditional (NOT
# GTE_ENABLE_EDITOR-gated) treatment as every other core Renderer-tier
# shader above (Locked Design Decision 4) - this feature follows GET
# /get_texture's own gating exactly (GTE_ENABLE_NETWORK only, at the HTTP
# route level, not here). Does NOT #include AtmosphereCommon.glsl, so no
# EXTRA_DEPENDS.
gte_add_shader(GreatTamanaEditor src/Shaders/VolumeTexturePreview.comp)

# task_manager/frame-debugger-3 campaign, PHASE6
# (PHASE6_CHANNELS_AND_LEVELS_REAL_PREVIEW.md) - the Frame Debugger's own
# Channels/Levels preview-compositing compute shader
# (src/Editor/FrameDebuggerPreviewProcessing.h/.cpp). Always compiled/staged,
# since FrameDebuggerPreviewProcessing.cpp - the only thing that ever loads
# it - is always compiled into gte_core today (same precedent as
# BoxBlur.comp/SceneGrid.vert/frag above). Does NOT #include
# AtmosphereCommon.glsl, so no EXTRA_DEPENDS. editor-core-separation-1
# campaign, PHASE8 - this `if(TRUE)` is a temporary placeholder.
if(TRUE)
    gte_add_shader(GreatTamanaEditor src/Shaders/FrameDebuggerPreview.comp)
endif()



# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md, Step 3.4 (PHASE0_MASTER_STRATEGY.md's
# Locked Design Decision #4) - flips this executable to shared (DLL)
# libgcc/libstdc++ linkage and stages the resulting three runtime DLLs next
# to it, so it stays a valid "matching pair" with every plugin .dll on the
# other side of the plugin ABI boundary. Must be called AFTER
# add_executable(GreatTamanaEditor ...) (it is, by construction, this far
# down the file) and BEFORE sdl3_copy_runtime_dll() below (ordering between
# the two POST_BUILD steps themselves does not matter, but this keeps both
# "stage a runtime dependency next to the .exe" steps textually adjacent).
gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)

# Ensures SDL3.dll is copied next to the built .exe after every build.
sdl3_copy_runtime_dll(GreatTamanaEditor)
endif() # GTE_CORE_STANDALONE_PROBE_ONLY - matches the if() right before the "--- Final executable ---" comment below.

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
