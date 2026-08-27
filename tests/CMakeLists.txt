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
#   - Renderer/DrawStatsTests.cpp             - Phase 3's pure per-item
#                                            draw-call/triangle-count
#                                            accumulator (AccumulateDrawStats()/
#                                            CountDrawStats() -
#                                            src/Renderer/DrawStats.h) - no
#                                            Vulkan/Renderer/live GPU
#                                            involved at all.
#   - Renderer/GpuTimingTests.cpp            - Phase 4A's pure GPU-timestamp
#                                            capability interpretation
#                                            (InterpretTimestampCapability())
#                                            and tick-delta -> millisecond/
#                                            slot-indexing math
#                                            (ConvertTimestampDeltaToMilliseconds()/
#                                            PresentTimestampSlotBase() -
#                                            src/Renderer/GpuTiming.h), PLUS
#                                            Phase 4B's pure tri-state
#                                            priority resolution
#                                            (ResolveGpuTimingStatus()) - no
#                                            Vulkan/Renderer/live GPU device
#                                            involved at all (no VkQueryPool
#                                            is created anywhere in Phase 4A;
#                                            Phase 4B's own GpuTimingService/
#                                            VulkanQueryPool are Tier 2, see
#                                            the note below).
#   - Renderer/ComputeDispatchTests.cpp      - the compute-shader campaign's
#                                            Phase 4 pure dispatch math
#                                            (ComputeGroupCount()/
#                                            ComputeGroupCount3D() -
#                                            src/Renderer/ComputeDispatch.h) -
#                                            exact multiples, the
#                                            non-exact-multiple ceiling-
#                                            division regression case (100
#                                            items at group size 64 must
#                                            yield group count 2, never the
#                                            truncating-division 1), zero
#                                            items, and the 3D per-axis
#                                            overload - no Vulkan/Renderer/
#                                            live GPU device involved at
#                                            all.
#   - Renderer/RenderGraph/RenderGraphTypesTests.cpp - Render Graph Phase 1's
#                                            pure vocabulary (handles/
#                                            ResourceAccess/TextureDesc/
#                                            BufferDesc/PassRecord - see
#                                            src/Renderer/RenderGraph/
#                                            RenderGraphTypes.h) - handle
#                                            validity/equality, desc value
#                                            equality (including a v2
#                                            regression test proving no
#                                            field is compared by identity),
#                                            and IsWriteAccess()/ToString()
#                                            covering every ResourceAccess
#                                            enumerator. No Vulkan device/
#                                            Renderer/Registry involved at
#                                            all - only VkFormat/VkDeviceSize/
#                                            VkBufferUsageFlags Vulkan-header
#                                            TYPES are used, never a Vulkan
#                                            call.
#   - Renderer/RenderGraph/RenderGraphBuilderTests.cpp - Render Graph Phase 2's
#                                            declarative builder API (see
#                                            src/Renderer/RenderGraph/
#                                            RenderGraphBuilder.h) -
#                                            CreateTexture()/CreateBuffer()
#                                            handle-vs-physical-identity
#                                            (including a v2 regression test
#                                            proving two different names
#                                            with an identical desc still
#                                            compare equal), AddPass()'s
#                                            setup-runs-now/execute-never-
#                                            runs-yet timing, every
#                                            PassBuilder read/write method,
#                                            ImportTexture()'s IsImported
#                                            tagging + required currentLayout
#                                            threading, and the null/empty
#                                            name assertion guard (debug
#                                            builds only). No Vulkan device
#                                            involved at all - RenderTarget
#                                            values are hand-built with fake,
#                                            never-dereferenced Vulkan handle
#                                            values.
#   - Renderer/RenderGraph/RenderGraphCompilerTests.cpp - Render Graph Phase 3's
#                                            pure "Smart Planner" compilation
#                                            algorithm (see src/Renderer/
#                                            RenderGraph/RenderGraphCompiler.h) -
#                                            dependency-graph construction
#                                            (RAW/WAW edges), backward-
#                                            reachability culling from a
#                                            required finalOutputs root set,
#                                            deterministic Kahn's-algorithm
#                                            topological sort (declaration-
#                                            order tie-breaking), and
#                                            per-resource lifetime bounds -
#                                            linear chains, a dead/culled
#                                            branch, a diamond dependency
#                                            (with a sibling-order
#                                            determinism check), multiple
#                                            writers to the same resource,
#                                            lifetime off-by-one bounds,
#                                            cross-call determinism, a
#                                            self-loop (read+write of the
#                                            same resource), a fully
#                                            disconnected pass, and a
#                                            finalOutputs handle nothing
#                                            ever writes. No Vulkan device
#                                            involved at all - fixtures are
#                                            built through a real
#                                            RenderGraphBuilder.
#   - Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp - Render Graph
#                                            Phase 5's pure barrier-synthesis
#                                            decision logic (see src/Renderer/
#                                            RenderGraph/RenderGraphBarrierPlanner.h) -
#                                            RequiredStateFor() covering every
#                                            ResourceAccess enumerator (with a
#                                            direct regression check against
#                                            FrameRecorder.cpp's own hardcoded
#                                            depth-attachment barrier fields),
#                                            RequiresBarrier()'s identical-
#                                            states-need-no-barrier
#                                            optimization, a hand-simulated
#                                            three-pass ping-pong sequence
#                                            proving the exact expected
#                                            barrier COUNT (2, not 3 or 1),
#                                            BuildImageMemoryBarrier2()/
#                                            BuildBufferMemoryBarrier2()'s
#                                            plain field population, and -
#                                            the single most important test
#                                            in this phase - a field-for-
#                                            field regression match against
#                                            FrameRecorder.cpp's own existing
#                                            Present()/RenderOffscreen()
#                                            barrier shapes. EmitImageBarrier()/
#                                            EmitBufferBarrier() (the thin
#                                            Vulkan-call half - a real
#                                            vkCmdPipelineBarrier2 call) are
#                                            deliberately Tier 2 and not
#                                            exercised here. No live VkDevice/
#                                            VkCommandBuffer involved at all.
#   - Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp - Render Graph
#                                            Phase 6's pure name -> slot
#                                            assignment/reuse/overflow-
#                                            degradation logic (see
#                                            src/Renderer/RenderGraph/
#                                            RenderGraphNameSlotTable.h) -
#                                            first-call assignment, distinct
#                                            names get increasing slots,
#                                            reuse by identical pointer AND
#                                            by equal-content-different-
#                                            pointer (strcmp fallback), a
#                                            null name returning the
#                                            sentinel, budget overflow
#                                            degrading gracefully for a
#                                            brand-new name while an
#                                            already-assigned name keeps
#                                            resolving correctly, and two
#                                            independent tables (simulating
#                                            RenderGraph's own two
#                                            ExecuteTimingMode regimes) never
#                                            colliding on the same name. No
#                                            Vulkan device involved at all -
#                                            this class has no VkQueryPool of
#                                            its own.
#   - Renderer/RenderGraph/RenderGraphSnapshotTests.cpp - Render Graph Phase
#                                            8's pure "flatten a compiled
#                                            graph into something
#                                            displayable" reshape (see
#                                            src/Renderer/RenderGraph/
#                                            RenderGraphSnapshot.h) -
#                                            surviving passes appear in real
#                                            execution order with resolved
#                                            read/write resource names and
#                                            per-pass stats sourced from a
#                                            hand-fabricated statsLookup
#                                            (never a real RenderGraph),
#                                            culled passes are appended after
#                                            them with a default (never-
#                                            queried) stats value, imported
#                                            vs. transient texture resources
#                                            and buffer resources (never
#                                            reported as imported - no
#                                            ImportBuffer() concept exists)
#                                            report the correct kind/
#                                            lifetime, and an empty
#                                            statsLookup never crashes. No
#                                            Vulkan device involved at all -
#                                            RenderGraph.h/.cpp themselves
#                                            remain Tier 2, see the note
#                                            below.
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
#   - Editor/ProfilerPanelDataTests.cpp     - the Editor "Profiler" panel's
#                                            pure data-shaping/formatting
#                                            logic (BuildSortedCpuScopeRows()/
#                                            FormatDuration()/
#                                            FormatFrameTimeSummary()/
#                                            FormatCount()/
#                                            ResolveGpuPassCounts()/
#                                            FormatGpuTimingLine()/
#                                            CpuScopeTableEmptyMessage() - see
#                                            src/Editor/ProfilerPanelData.h) -
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
#   Profiling/FrameProfilerTests.cpp        - FrameProfiler's BeginFrame()/
#                                            EndFrame()/RecordCpuScope()/
#                                            SetGpuPassSample()/
#                                            SetMemorySnapshot() ring-buffer
#                                            and flat-aggregation bookkeeping
#                                            (src/Profiling/FrameProfiler.h) -
#                                            fed entirely with hand-called
#                                            timings, no live clock/Vulkan/
#                                            SDL involved. Always built
#                                            (this module has no
#                                            GTE_ENABLE_EDITOR dependency at
#                                            all - see AGENTS.md, "Profiling").
#   Profiling/ScopeTimerTests.cpp           - ScopeTimer/GTE_PROFILE_SCOPE's
#                                            actual RAII behavior (records a
#                                            real positive duration, nests
#                                            correctly, and skips entirely
#                                            when capture is disabled) -
#                                            src/Profiling/ScopeTimer.h.
#                                            Always built, same reason as
#                                            above.
#   Profiling/FrameGraphDataTests.cpp       - Phase 2's pure "FrameProfiler
#                                            history -> plottable points"
#                                            reshape (BuildFrameGraphPoints()/
#                                            ComputeCpuMillisecondsRange()/
#                                            ComputeGpuMillisecondsRange() -
#                                            src/Profiling/FrameGraphData.h) -
#                                            exact frame-index ordering
#                                            (including ring-buffer
#                                            wraparound), all three GPU
#                                            passes/tri-states round-tripping
#                                            without cross-contamination, an
#                                            Absent/Unsupported sample never
#                                            being mistaken for real data
#                                            even when it carries a stale
#                                            non-zero value, an out-of-range
#                                            GpuPass reporting no data rather
#                                            than reading out of bounds, and
#                                            an in-progress (not yet
#                                            EndFrame()'d) frame never being
#                                            observed. Always built, same
#                                            reason as the two entries above.
#   Jobs/JobQueueTests.cpp                  - Job System Phase 1's fixed-
#                                            capacity ring buffer
#                                            (gte::Jobs::detail::JobQueue -
#                                            src/Jobs/JobQueue.h): FIFO
#                                            push/pop order, the full-queue
#                                            TryPush() failure case, reuse
#                                            of a slot after it drains, and
#                                            Shutdown()'s "drain what's
#                                            already queued, THEN report
#                                            empty" behavior. No real
#                                            threads/JobSystem singleton
#                                            involved - always built,
#                                            regardless of
#                                            GTE_ENABLE_JOB_SYSTEM (this
#                                            class always compiles - see
#                                            JobQueue.h's own comment).
#   Jobs/JobSystemTests.cpp                 - Job System Phase 1's real
#                                            worker-thread pool
#                                            (gte::Jobs::JobSystem - src/
#                                            Jobs/JobSystem.h):
#                                            WorkerCount() is always >= 1,
#                                            a default-constructed
#                                            JobHandle is already complete,
#                                            Schedule()+WaitForJobs()
#                                            actually runs the job, many
#                                            jobs scheduled against ONE
#                                            shared handle all complete
#                                            with no missed/corrupted
#                                            writes (stress-repeated - see
#                                            AGENTS.md, "Job System"), a
#                                            copied JobHandle observes the
#                                            same completion state as the
#                                            original, and two independent
#                                            handles don't interfere with
#                                            each other. Always built,
#                                            passes identically whether
#                                            GTE_ENABLE_JOB_SYSTEM is ON
#                                            (real worker threads) or OFF
#                                            (Schedule() runs inline,
#                                            synchronously) - see
#                                            JobSystem.h's own comment.
#
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
#
# Render Graph Phase 4 (RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md
# - src/Renderer/RenderGraph/RenderGraphResourcePool.h/.cpp) joins this same
# Tier 2 bucket for the same reason: AcquireTexture()/AcquireBuffer() call
# straight into Renderer::CreateRenderTexture()/CreateBuffer(), which need a
# real VkDevice+VmaAllocator to construct anything at all. Its own matching
# logic (a linear scan for `desc == entry.desc && !claimedThisFrame`) is
# simple enough to review by inspection; the accepted verification bar is
# manual (a debug build, checked against the Editor's "Memory" panel once
# Phase 7 wires this into production, to confirm the GPU resource count
# stays stable frame-to-frame at steady state) - see that phase's own
# strategy document, Step 5.
#
# Render Graph Phase 6 (RENDERGRAPH_PHASE6_EXECUTION_ENGINE_STRATEGY_v2.md -
# src/Renderer/RenderGraph/RenderGraph.h/.cpp) joins this same Tier 2 bucket
# for the same reason: RenderGraph::Execute() resolves textures/buffers
# through RenderGraphResourcePool (above) and records real
# vkCmdBeginRendering/vkCmdPipelineBarrier2/vkCmdSetViewport/vkCmdSetScissor/
# vkCmdEndRendering calls against a live VkCommandBuffer - none of which can
# run without a real VkDevice. The accepted verification bar (per that
# phase's own strategy document, Step 3.4/5) is manual: a throwaway,
# multi-pass test scene run against a real device with validation layers
# enabled, exercised in BOTH ExecuteTimingMode regimes, once a real
# Application-level call site exists to drive it (Phase 7). Only this
# file's own pure, name-keyed slot-assignment helper
# (RenderGraphNameSlotTable, see above) is pulled out as Tier-1-testable.
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
    Profiling/FrameProfilerTests.cpp
    Profiling/ScopeTimerTests.cpp
    Profiling/FrameGraphDataTests.cpp
    Jobs/JobQueueTests.cpp
    Jobs/JobSystemTests.cpp
    Input/InputStateTests.cpp
    Application/EventTranslatorTests.cpp
    Application/MemorySnapshotBuilderTests.cpp
    Renderer/VertexTests.cpp
    Renderer/PrimitiveMeshGeneratorTests.cpp
    Renderer/ResourcePoolTests.cpp
    Renderer/DrawStatsTests.cpp
    Renderer/GpuTimingTests.cpp
    Renderer/ComputeDispatchTests.cpp
    Renderer/RenderGraph/RenderGraphTypesTests.cpp
    Renderer/RenderGraph/RenderGraphBuilderTests.cpp
    Renderer/RenderGraph/RenderGraphCompilerTests.cpp
    Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp
    Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp
    Renderer/RenderGraph/RenderGraphSnapshotTests.cpp
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
    Game/EntityInstantiatorTests.cpp
    Game/MeshVertexPackingTests.cpp
    Game/MeshMaterialPartitionerTests.cpp
    Game/SkeletalRigCacheTests.cpp
    Game/AnimationClipCacheTests.cpp
    Game/ResolvedAnimationBindingCacheTests.cpp
    Animation/BoneChainResolverTests.cpp
    Animation/BonePoseMathTests.cpp
    Animation/SkeletonPoseTests.cpp
    Animation/IkSolverTests.cpp
    Animation/AppendBoneSolverTests.cpp
    Animation/MotionSamplerTests.cpp
    Animation/AnimationPoseEvaluatorTests.cpp
    Animation/VertexSkinningTests.cpp
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
        Editor/ProfilerPanelDataTests.cpp
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
