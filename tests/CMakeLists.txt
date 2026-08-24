# tests/CMakeLists.txt
#
# GreatTamanaEngineTests - GoogleTest-based unit test suite for gte_core (see
# the root CMakeLists.txt, which only add_subdirectory()'s this when
# GTE_BUILD_TESTS is ON). GoogleTest itself is fetched/built by
# cmake/FetchGTest.cmake, following the exact same "no submodule, no package
# manager, download+stage on first configure" convention as
# SDL3/Vulkan/VMA/ImGui (see cmake/Fetch*.cmake).
#
# --- Test taxonomy ---------------------------------------------------------
#
# Every test file below is "Tier 1": pure logic, with NO live Vulkan device,
# GPU, or SDL window/video subsystem required to run - safe to execute on
# every commit/PR, including a CI runner with no GPU at all:
#
#   - Memory/GpuResourceHandleTests.cpp   - GpuResourceHandle value semantics.
#   - Memory/GpuMemoryTrackerTests.cpp    - GpuMemoryTracker's Track()/
#                                            Untrack()/totals/snapshot
#                                            bookkeeping (takes plain enums +
#                                            a byte count - never touches a
#                                            real VmaAllocation).
#   - Input/InputStateTests.cpp           - InputState's held/pressed/
#                                            released/delta semantics, fed
#                                            entirely with hand-built
#                                            gte::Event values.
#   - Application/EventTranslatorTests.cpp - SDL_Event -> gte::Event mapping,
#                                            using hand-built SDL_Event
#                                            values (SDL_Event is a plain
#                                            struct/union - no SDL_Init() or
#                                            video subsystem needed to
#                                            construct or translate one).
#   - Renderer/VertexTests.cpp             - Vertex's Vulkan binding/
#                                            attribute description metadata.
#   - Renderer/PrimitiveMeshGeneratorTests.cpp - PrimitiveMeshGenerator's
#                                            pure geometry generation
#                                            (Cube/Sphere/Capsule/Cone/Plane)
#                                            - checked against hand-derived
#                                            geometric invariants (surface
#                                            distance from center/core
#                                            segment, bounding box, ...), no
#                                            GPU/Renderer/ECS involved.
#   - Math/Vec2Tests.cpp, Vec3Tests.cpp    - Vec2/Vec3 arithmetic and
#                                            geometric ops (dot/cross/
#                                            length/normalize/lerp), using
#                                            exact hand-computed values.
#   - Math/Mat4Tests.cpp                   - Mat4 multiply/transpose/
#                                            inverse/LookAtLH/
#                                            PerspectiveFovLH_ZO, using
#                                            hand-verified exact values.
#   - Math/QuatTests.cpp                   - Quat multiply/slerp/nlerp/
#                                            axis-angle/matrix and Euler
#                                            round-trip conversions.
#   - ECS/EntityManagerTests.cpp            - EntityManager's Create()/
#                                            Destroy()/IsAlive() handle
#                                            allocation+recycling+generation
#                                            semantics.
#   - ECS/ComponentStorageTests.cpp          - ComponentStorage<T>'s
#                                            sparse-set Add()/Remove()/Has()/
#                                            Get() and swap-with-last
#                                            removal, fed with hand-built
#                                            Entity values.
#   - ECS/RegistryTests.cpp                  - Registry gluing EntityManager
#                                            + ComponentStorage<T> together:
#                                            multi-component-type entities,
#                                            DestroyEntity() clearing every
#                                            pool, and Transform (the first
#                                            real component).
#   - ECS/CameraTests.cpp                    - Camera's pure math helpers
#                                            (ProjectionMatrix()/ViewMatrix()),
#                                            checked against
#                                            Mat4::PerspectiveFovLH_ZO()/
#                                            LookAtLH() directly.
#   - Renderer/ResourcePoolTests.cpp         - ResourcePool<T, HandleT>'s
#                                            generic insert/remove/lookup +
#                                            generation-guard semantics (see
#                                            AGENTS.md, "Entity-Component-
#                                            System"), backing MeshHandle/
#                                            PipelineHandle.
#   - Game/RenderSystemTests.cpp             - RenderSystem::CollectRenderables()
#                                            (the pure ECS -> DrawCommand step)
#                                            and ResolveActiveCameraViewProjection()
#                                            (the pure ECS -> camera
#                                            view-projection step) - no
#                                            Renderer/live GPU involved.
#                                            RenderSystem::Draw() itself is
#                                            Tier 2 (needs a live Renderer),
#                                            see the note below.
#   - Editor/EditorCameraTests.cpp           - EditorCamera's pure pan/
#                                            rotate/dolly math (see
#                                            src/Editor/EditorCamera.h) -
#                                            no ImGui/SDL/Vulkan involved.
#                                            Only built when GTE_ENABLE_EDITOR
#                                            is ON (see below), since
#                                            EditorCamera itself is only
#                                            compiled into gte_core then.
#   - Editor/SelectionTests.cpp               - Selection's pure
#                                            SelectEntity()/SelectAsset()/
#                                            ClearAssetIfPath()/Kind() gate-
#                                            keeper logic (see
#                                            src/Editor/Selection.h) - no
#                                            ImGui/SDL/Vulkan involved. Only
#                                            built when GTE_ENABLE_EDITOR is
#                                            ON, same reason as
#                                            EditorCameraTests.cpp above.
#   - Editor/MemoryPanelDataTests.cpp        - the Editor "Memory" panel's
#                                            pure data-shaping logic
#                                            (BuildMemoryRows()/FormatBytes()/
#                                            ToString() - see
#                                            src/Editor/MemoryPanelData.h) -
#                                            no ImGui/Renderer/live GPU
#                                            involved. Only built when
#                                            GTE_ENABLE_EDITOR is ON, same
#                                            reason as EditorCameraTests.cpp
#                                            above.
#   - Editor/ProjectPanelDataTests.cpp        - the Editor "Project" panel's
#                                            pure data-shaping/filesystem
#                                            logic (ScanProjectDirectory()/
#                                            EnsureProjectRootExists()/
#                                            ResolveDropTargetDirectory()/
#                                            MakeUniqueDestinationPath()/
#                                            PathToUtf8()/Utf8ToPath() - see
#                                            src/Editor/ProjectPanelData.h) -
#                                            no ImGui/Renderer/live GPU/SDL
#                                            video involved, though it DOES
#                                            touch a real temp directory the
#                                            test itself creates/tears down.
#                                            Only built when GTE_ENABLE_EDITOR
#                                            AND GTE_ENABLE_PROJECT_PANEL are
#                                            both ON, same reason as
#                                            EditorCameraTests.cpp above.
#   - Assets/AssetTypesTests.cpp           - Guid value semantics/generation/
#                                            string round-trip, AssetFlags bit
#                                            ops, and AssetTypeFromExtension()
#                                            (src/Assets/AssetTypes.h).
#   - Assets/GtaFileTests.cpp               - WriteGtaFile()/ReadGtaHeader()/
#                                            ReadGtaFile() round-tripping the
#                                            *.gta binary format's 64-byte
#                                            header + metadata/payload
#                                            sections (src/Assets/GtaFile.h).
#   - Assets/AssetDatabaseTests.cpp         - AssetDatabase's directory scan
#                                            (RefreshFromDirectory()), import
#                                            (ImportAsset()/ImportRawFile()),
#                                            and Guid/path lookups
#                                            (src/Assets/AssetDatabase.h).
#   - Assets/Ktx2EncoderTests.cpp           - EncodeImageBytesToKtx2()/
#                                            EncodeImageFileToKtx2() decoding
#                                            a source image (via stb_image)
#                                            and re-encoding it as an
#                                            in-memory KTX2 container (via
#                                            the statically-linked libktx) -
#                                            no GPU device involved
#                                            (src/Assets/Ktx2Encoder.h).
#   - Assets/Ktx2DecoderTests.cpp           - DecodeKtx2ToRgba8() round-
#                                            tripping against
#                                            EncodeImageBytesToKtx2()'s own
#                                            output (pixel-exact), plus
#                                            malformed/empty input rejection
#                                            - no GPU device involved
#                                            (src/Assets/Ktx2Decoder.h).
#   - Assets/AssetImporterTests.cpp         - IsImportableAsKtx2Texture()'s
#                                            extension gating and
#                                            ImportAssetFile()'s PNG/JPG ->
#                                            KTX2 -> *.gta conversion vs.
#                                            plain-copy-fallback behavior
#                                            (src/Assets/AssetImporter.h).
#   Assets/PmxLoaderTests.cpp              - LoadPmxModel()'s extraction of
#                                            vertex positions/normals/UVs/
#                                            triangle indices from a MikuMiku
#                                            Dance .pmx model file, against a
#                                            hand-built minimal binary fixture
#                                            (BuildMinimalTrianglePmx(), same
#                                            "construct the exact binary
#                                            format by hand" approach as
#                                            GtaFileTests.cpp) - no GPU device
#                                            involved (src/Assets/PmxLoader.h).
#                                            Also includes one machine-gated
#                                            smoke test against a real,
#                                            non-vendored MMD model (GTEST_SKIP()s
#                                            everywhere that model isn't present).
#   Assets/VmdLoaderTests.cpp               - LoadVmdMotion()'s extraction of
#                                            bone/morph/camera/light/shadow/IK
#                                            keyframe tracks from a MikuMiku
#                                            Dance .vmd motion file, against
#                                            hand-built minimal/rich binary
#                                            fixtures (BuildMinimalOneBoneKeyframeVmd()/
#                                            BuildRichVmd(), same "construct
#                                            the exact binary format by hand"
#                                            approach as PmxLoaderTests.cpp) -
#                                            no GPU device involved
#                                            (src/Assets/VmdLoader.h). Also
#                                            includes one machine-gated smoke
#                                            test against a real, non-vendored
#                                            MMD motion (GTEST_SKIP()s
#                                            everywhere that motion isn't
#                                            present).
#   Assets/MeshFileTests.cpp               - EncodeMeshDataToBytes()/
#                                            DecodeMeshDataFromBytes()
#                                            round-tripping the *.gta
#                                            AssetType::Mesh payload's binary
#                                            layout, plus malformed/truncated/
#                                            empty input rejection - no GPU
#                                            device involved (src/Assets/
#                                            MeshFile.h).
#   Assets/MotionFileTests.cpp              - EncodeMotionDataToBytes()/
#                                            DecodeMotionDataFromBytes()
#                                            round-tripping the *.gta
#                                            AssetType::Animation payload's
#                                            binary layout (model name plus
#                                            bone/morph/camera/light/shadow/
#                                            IK keyframe tracks), plus
#                                            malformed/truncated/empty input
#                                            rejection - no GPU device
#                                            involved (src/Assets/MotionFile.h).
#   Assets/RigFileTests.cpp                - EncodeRigDataToBytes()/
#                                            DecodeRigDataFromBytes()
#                                            round-tripping the *.gta
#                                            AssetType::Mesh METADATA
#                                            section's binary layout (per-
#                                            vertex skin weights, bones,
#                                            morphs, rigid bodies/joints),
#                                            plus malformed/truncated/empty
#                                            input rejection - no GPU device
#                                            involved (src/Assets/RigFile.h).
#
# Note that the test BINARY still dynamically depends on SDL3.dll at process
# load time regardless of the above (gte_core's Window.cpp references SDL
# symbols like SDL_CreateWindow, even though no Tier 1 test ever calls
# SDL_Init() or opens a window) - sdl3_copy_runtime_dll() below handles that,
# same as for the real GreatTamanaEngine.exe.
#
# --- Tier 2 (GPU-dependent) tests: intentionally not implemented yet -------
#
# Buffer/RenderTexture/Pipeline/GpuResourceFactory all need a real
# VkDevice + VmaAllocator to construct at all, and today's
# VulkanDevice::PickPhysicalDevice() additionally requires a real
# VkSurfaceKHR (to query present support), which normally comes from an
# actual OS window (Window + VulkanSurface). A future GpuTestFixture could
# build a headless VkSurfaceKHR via VK_EXT_headless_surface (supported by
# most desktop drivers, and by software implementations like Mesa
# lavapipe/SwiftShader) instead of a real Window, then GTEST_SKIP() the
# whole fixture at runtime if instance/device creation fails - i.e. those
# tests would still PASS (skipped, not failed) on a GPU-less CI runner, and
# only actually exercise the GPU on a developer machine or a self-hosted
# runner that has one. Left as a documented follow-up rather than
# implemented here, since it needs its own fixture/CMake wiring
# (GTE_BUILD_GPU_TESTS or similar) rather than being bolted onto Tier 1.
set(GTE_TEST_SOURCES
    Memory/GpuResourceHandleTests.cpp
    Assets/AssetTypesTests.cpp
    Assets/GtaFileTests.cpp
    Assets/AssetDatabaseTests.cpp
    Assets/Ktx2EncoderTests.cpp
    Assets/Ktx2DecoderTests.cpp
    Assets/AssetImporterTests.cpp
    Assets/PmxLoaderTests.cpp
    Assets/VmdLoaderTests.cpp
    Assets/MeshFileTests.cpp
    Assets/MotionFileTests.cpp
    Assets/RigFileTests.cpp
    Memory/GpuMemoryTrackerTests.cpp
    Memory/SdlMemoryTrackerTests.cpp
    Input/InputStateTests.cpp
    Application/EventTranslatorTests.cpp
    Renderer/VertexTests.cpp
    Renderer/PrimitiveMeshGeneratorTests.cpp
    Renderer/ResourcePoolTests.cpp
    Math/Vec2Tests.cpp
    Math/Vec3Tests.cpp
    Math/Mat4Tests.cpp
    Math/QuatTests.cpp
    ECS/EntityManagerTests.cpp
    ECS/ComponentStorageTests.cpp
    ECS/RegistryTests.cpp
    ECS/CameraTests.cpp
    ECS/TransformHierarchyTests.cpp
    Game/RenderSystemTests.cpp
)

# Editor/EditorCamera.h/.cpp (see src/Editor/EditorCamera.h) is only
# compiled into gte_core when GTE_ENABLE_EDITOR is ON - the whole
# src/Editor/ folder besides EditorLayer.h/NullEditorLayer.cpp is gated on
# it (see the root CMakeLists.txt's "Editor Module Structure" comment) -
# so its test file follows the exact same "zero-touch when off" rule: a
# GTE_ENABLE_EDITOR=OFF configure never even tries to compile/link a test
# against a class that doesn't exist in that build. EditorCamera itself is
# still genuinely Tier 1 (see its own file comment) - this guard is purely
# about it not being COMPILED IN at all when the Editor is off, not about
# it needing a live Vulkan/SDL/ImGui dependency.
if(GTE_ENABLE_EDITOR)
    list(APPEND GTE_TEST_SOURCES
        Editor/EditorCameraTests.cpp
        Editor/SelectionTests.cpp
        Editor/ImGuiMemoryTrackerTests.cpp
        Editor/MemoryPanelDataTests.cpp
    )

    # ProjectPanelData.h/.cpp (see src/Editor/ProjectPanelData.h) is only
    # compiled into gte_core when GTE_ENABLE_PROJECT_PANEL is ALSO ON (a
    # separate switch from GTE_ENABLE_EDITOR - see the root CMakeLists.txt) -
    # same "zero-touch when off" rule as EditorCameraTests.cpp above.
    # ProjectPanelData's own functions are genuinely Tier 1 despite touching
    # the real filesystem: they take/return plain paths/strings and operate
    # against a real temp directory the test itself creates/tears down, with
    # no ImGui/Renderer/live-Vulkan-device/SDL-video involved at all.
    if(GTE_ENABLE_PROJECT_PANEL)
        list(APPEND GTE_TEST_SOURCES
            Editor/ProjectPanelDataTests.cpp
            Editor/AssetInspectorDataTests.cpp
        )
    endif()
endif()

add_executable(GreatTamanaEngineTests ${GTE_TEST_SOURCES})

target_link_libraries(GreatTamanaEngineTests PRIVATE
    gte_core
    GTest::gtest
    GTest::gtest_main
)

# Editor/ImGuiMemoryTrackerTests.cpp (above) includes <imgui.h> directly -
# gte_core links imgui PRIVATE (see the root CMakeLists.txt), so that
# include path isn't propagated transitively to consumers of gte_core; this
# test executable needs its own link against the imgui target for its
# include directories. Only linked when GTE_ENABLE_EDITOR is ON, since the
# imgui target itself only exists then (matching every other GTE_ENABLE_EDITOR
# guard in this file).
if(GTE_ENABLE_EDITOR)
    target_link_libraries(GreatTamanaEngineTests PRIVATE imgui)
endif()

# See the file-level comment above for why this is still needed even though
# no test here ever calls SDL_Init().
sdl3_copy_runtime_dll(GreatTamanaEngineTests)

# gtest_discover_tests() (from CMake's own bundled GoogleTest.cmake module,
# distinct from cmake/FetchGTest.cmake which only fetches GoogleTest's
# SOURCE) runs the built test binary with --gtest_list_tests as a post-build
# step to register every individual TEST()/TEST_F() as its own CTest test,
# so `ctest` output shows real test names instead of one opaque
# "GreatTamanaEngineTests passed/failed" line. This is registered AFTER
# sdl3_copy_runtime_dll() above so SDL3.dll is already staged next to the
# .exe (POST_BUILD steps run in the order they were added) by the time
# discovery tries to run it.
include(GoogleTest)
gtest_discover_tests(GreatTamanaEngineTests)
