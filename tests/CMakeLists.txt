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
#   all.
#   - Renderer/GpuSkinning/GpuSkinningTypesTests.cpp - GPU Vertex Skinning
#                                            campaign, Phase 1 (Data & Buffer
#                                            Layout Foundations - see
#                                            task_manager/gpu_skinning/
#                                            GPU_SKINNING_PHASE1_DATA_BUFFER_FOUNDATIONS_STRATEGY_v1.md) -
#                                            PackBindPoseVertices()/PackUvs()/
#                                            PackSkinWeights() (src/Renderer/
#                                            GpuSkinning/GpuSkinningTypes.h)
#                                            pure CPU-side repacking into
#                                            GPU-buffer-ready layouts: exact
#                                            padded position/normal values,
#                                            the mismatched-normals-count
#                                            Vec3::Up() fallback (mirroring
#                                            Animation/VertexSkinning.cpp's
#                                            own SkinVertexRange()), UV
#                                            pass-through, unused skin-weight
#                                            slots translating to bone 0/
#                                            weight 0.0 (never a raw
#                                            bit-reinterpreted -1 index), and
#                                            every struct's documented byte
#                                            size. No Vulkan/Renderer/live GPU
#                                            device involved at all - this
#                                            file is deliberately
#                                            Vulkan-header-free, same
#                                            precedent as GpuTiming.h.
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
#   - Editor/JobsPanelDataTests.cpp          - the Editor "Jobs" panel's
#                                            pure data-shaping logic
#                                            (ColorForJobName()/
#                                            ComputeWorkerUtilizationSummary()/
#                                            FormatWorkerUtilizationSummary()/
#                                            PointsForWorker()/
#                                            JobsTimelineEmptyMessage() - see
#                                            src/Editor/JobsPanelData.h) -
#                                            no ImGui/Renderer/live GPU/real
#                                            JobSystem/FrameProfiler
#                                            singleton involved. Only built
#                                            when GTE_ENABLE_EDITOR is ON,
#                                            same reason as
#                                            EditorCameraTests.cpp above.
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
#   - Editor/ModelRigCacheTests.cpp           - ModelRigCache::GetOrLoad()'s
#                                            mtime-gated load/reload/cache
#                                            behavior for a model's whole
#                                            RigFileData (bones/rigid bodies/
#                                            joints - see
#                                            src/Editor/ModelRigCache.h) -
#                                            no ImGui/Renderer/live GPU/SDL
#                                            video involved, though it DOES
#                                            touch a real temp directory (and
#                                            real *.gta files written via
#                                            WriteGtaFile()) the test itself
#                                            creates/tears down. Only built
#                                            when GTE_ENABLE_EDITOR AND
#                                            GTE_ENABLE_PROJECT_PANEL are both
#                                            ON, same reason as
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
#   Scene/SceneTextFormatTests.cpp          - SerializeSceneDocument()/
#                                            DeserializeSceneDocument()
#                                            round-tripping the new *.gtscene
#                                            hand-rolled text format (src/
#                                            Scene/SceneDocument.h,
#                                            src/Scene/SceneTextFormat.h) -
#                                            empty/single-Primitive/single-
#                                            Asset/mixed documents round-trip
#                                            exactly, malformed input
#                                            (missing/mismatched magic,
#                                            unclosed OBJECT, stray END,
#                                            unrecognized kind/primitiveType,
#                                            wrong CSV field counts,
#                                            non-numeric floats, a
#                                            syntactically malformed
#                                            assetGuid=) is rejected, the v2
#                                            kind=Asset cross-field
#                                            validation (missing assetGuid=
#                                            or one parsing to
#                                            Guid::Invalid() both fail) is
#                                            enforced while kind=Primitive
#                                            tolerates a stray assetGuid=
#                                            line, an unrecognized key is
#                                            silently ignored (forward
#                                            compatibility), and an absent
#                                            optional field (e.g. no name=
#                                            line) simply keeps its default
#                                            rather than failing. No ECS/
#                                            Renderer/filesystem I/O involved
#                                            at all - this module is always
#                                            compiled, mirroring src/Physics/
#                                            and src/Jobs/'s own bootstrap.
#   Scene/SceneBuilderTests.cpp              - BuildSceneDocumentFromRegistry()
#                                            (Registry + AssetDatabase ->
#                                            SceneDocument) and
#                                            ClearSerializableSceneObjects()
#                                            (src/Scene/SceneBuilder.h) -
#                                            a Primitive-tagged root, an
#                                            Asset-tagged root that resolves
#                                            via a real temp-directory
#                                            AssetDatabase (including a
#                                            differently-spelled-but-
#                                            equivalent path proving the
#                                            path-normalization invariant
#                                            holds), an untracked-asset-path
#                                            root being skipped, an
#                                            untagged root (e.g. Camera)
#                                            being skipped, a child entity
#                                            never being independently
#                                            visited, multiple mixed roots
#                                            resolving together, and
#                                            ClearSerializableSceneObjects()
#                                            destroying only what this
#                                            feature owns (roots +
#                                            descendants) while leaving
#                                            everything else alive. Real
#                                            temp directory, no GPU/SDL/
#                                            ImGui involved.
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
#   Profiling/JobScopeTimerTests.cpp        - Job System Phase 5's
#                                            GTE_PROFILE_JOB_SCOPE/JobScopeTimer
#                                            (src/Profiling/JobScopeTimer.h)
#                                            recording a real positive
#                                            duration into
#                                            FrameProfiler::RecordWorkerJobSample()'s
#                                            new, dedicated thread-safe log
#                                            when called from a REAL
#                                            gte::Jobs::Dispatch() batch job
#                                            body (several batches across
#                                            more than one worker, proving
#                                            concurrent writes never
#                                            corrupt each other's reserved
#                                            slot), silently recording
#                                            nothing when called from the
#                                            test's own (non-worker) thread
#                                            directly, and the
#                                            kMaxWorkerJobSamplesPerFrame
#                                            overflow path degrading
#                                            gracefully rather than
#                                            crashing/corrupting memory.
#                                            Always built, same reason as
#                                            ScopeTimerTests.cpp above.
#   Profiling/WorkerTimelineDataTests.cpp   - Job System Phase 5's pure
#                                            "one frame's raw
#                                            WorkerJobSample log -> a
#                                            per-worker timeline" reshape
#                                            (BuildWorkerTimelinePoints()/
#                                            ComputeDistinctWorkerCount() -
#                                            src/Profiling/WorkerTimelineData.h) -
#                                            an empty frame produces an
#                                            empty result, every sample's
#                                            own startMilliseconds is
#                                            computed correctly relative to
#                                            the frame's own start, distinct
#                                            worker counting ignores
#                                            repeats, and ordering is
#                                            preserved exactly as recorded.
#                                            No live clock/JobSystem
#                                            involved - fed entirely with a
#                                            hand-built FrameSample. Always
#                                            built, same reason as
#                                            FrameGraphDataTests.cpp above.
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
#   Jobs/JobDispatchMathTests.cpp            - Job System Phase 2's pure,
#                                            deterministic batch-splitting
#                                            math (gte::Jobs::ComputeBatchRanges() -
#                                            src/Jobs/JobDispatch.h): zero
#                                            itemCount, itemCount smaller
#                                            than/equal to minItemsPerBatch
#                                            (collapses to one batch), evenly/
#                                            non-evenly divisible splits (full
#                                            coverage, no gaps/overlap, sizes
#                                            never differing by more than 1),
#                                            never more batches than
#                                            workerCount, degenerate
#                                            zero-workerCount/zero-
#                                            minItemsPerBatch inputs, plus a
#                                            fuzz-style sweep across many
#                                            (itemCount, workerCount,
#                                            minItemsPerBatch) triples. No
#                                            JobSystem/real threads involved
#                                            at all - always built, same
#                                            reason as JobQueueTests.cpp
#                                            above.
#   Jobs/JobDispatchTests.cpp                - Job System Phase 2's real
#                                            Dispatch()+WaitForJobs() batch
#                                            execution (src/Jobs/JobDispatch.h),
#                                            proving batch-splitting math and
#                                            real parallel execution compose
#                                            correctly: a zero-itemCount call
#                                            is an immediate no-op, an evenly
#                                            divisible workload and a
#                                            deliberately prime-sized
#                                            (non-evenly-divisible) workload
#                                            both complete with every index
#                                            written exactly once, a workload
#                                            smaller than minItemsPerBatch
#                                            exercises the "collapses to one
#                                            batch" path for real, and a
#                                            stress-repeated loop of
#                                            independent dispatches all
#                                            complete correctly. Always
#                                            built, passes identically
#                                            whether GTE_ENABLE_JOB_SYSTEM is
#                                            ON or OFF, same reason as
#                                            JobSystemTests.cpp above.
#
#   Jobs/JobContinuationTests.cpp            - Job System Phase 3's job
#                                            dependencies/continuations
#                                            (ScheduleAfter()/DispatchAfter() -
#                                            src/Jobs/JobContinuation.h),
#                                            using a real JobSystem::Instance():
#                                            an already-complete dependency
#                                            degrades to an ordinary
#                                            Schedule() call with strict
#                                            ordering still verified (many
#                                            repeated iterations, since
#                                            concurrent ordering bugs can pass
#                                            by luck on a single run), a
#                                            genuinely pending dependency
#                                            defers real execution until it
#                                            clears (with WaitForJobs()
#                                            called BEFORE the dependency
#                                            clears, proving the handle is
#                                            correctly incomplete from the
#                                            instant ScheduleAfter() returns,
#                                            not lazily), fan-in (one
#                                            continuation waiting on several
#                                            dependencies) and fan-out
#                                            (several continuations waiting
#                                            on the same dependency) both
#                                            resolve correctly, a
#                                            kMaxWatchersPerHandle-exceeding
#                                            number of dependents on one slow
#                                            handle all still eventually run
#                                            via the documented polling
#                                            fallback, and DispatchAfter()
#                                            defers a real batched Dispatch()
#                                            the same way, with every item
#                                            written exactly once only after
#                                            its own dependencies clear.
#                                            Always built, passes identically
#                                            whether GTE_ENABLE_JOB_SYSTEM is
#                                            ON or OFF, same reason as
#                                            JobDispatchTests.cpp above.
#
#   Jobs/JobSystemSdlClockThreadSafetyTests.cpp - Job System Phase 4's own
#                                            required verification item
#                                            (JOBSYSTEM_PHASE4_THREAD_SAFETY_AUDIT_INTEGRATION_POINTS_v2.md,
#                                            Step 3.2): confirms
#                                            SDL_GetPerformanceCounter()/
#                                            SDL_GetPerformanceFrequency()
#                                            (the one clock
#                                            src/Profiling/ScopeTimer.h is
#                                            built on) are safe to call
#                                            CONCURRENTLY, from several
#                                            threads released at the same
#                                            instant via a shared start
#                                            barrier - every thread observes
#                                            the exact same, non-zero
#                                            frequency, and every thread's
#                                            own sequence of counter reads
#                                            stays non-decreasing under
#                                            concurrent load. No SDL_Init()/
#                                            video subsystem needed (same
#                                            "SDL functions that don't need
#                                            SDL_Init()" pattern as
#                                            Memory/SdlMemoryTrackerTests.cpp).
#                                            Always built - has no dependency
#                                            on GTE_ENABLE_JOB_SYSTEM/
#                                            GTE_ENABLE_PROFILER at all, it
#                                            calls SDL3's own timer API
#                                            directly.
#
#   Network/NetworkRoutesTests.cpp          - network-impl-1 campaign
#                                            (task_manager/network-impl-1/
#                                            PHASE4_AUTOMATED_TESTS_AND_REGRESSION_SAFETY.md),
#                                            Tier 1: HandleHelloWorld()
#                                            (src/Network/NetworkRoutes.h)
#                                            returns the exact contracted
#                                            "hello world" response body, no
#                                            httplib/socket/thread involved at
#                                            all.
#   Network/NetworkServerTests.cpp          - Phase 4's real, end-to-end
#                                            coverage of NetworkServer (src/
#                                            Network/NetworkServer.h): a real
#                                            ephemeral-port bind + background
#                                            std::thread + httplib::Client
#                                            request proves GET
#                                            /http_hello_world actually
#                                            answers "hello world" over a
#                                            real loopback socket, an unknown
#                                            route falls through to a plain
#                                            404, Stop() is safe even without
#                                            a prior Start(), and two
#                                            correctness-critical cases:
#                                            DestructorStopsTheServerWithoutHanging
#                                            (the automated proof of Phase 2's
#                                            "no detached thread, no hang"
#                                            RAII requirement) and
#                                            BindFailureIsNonFatalAndLeavesServerNotRunning
#                                            (the automated proof of the
#                                            documented "a bind failure is
#                                            logged and non-fatal" contract -
#                                            AGENTS.md, "Networking"). Not
#                                            strict Tier 1 (real OS
#                                            socket/thread involved) but needs
#                                            no GPU/live Vulkan device/SDL
#                                            video subsystem, so it lives here
#                                            alongside Jobs/JobSystemTests.cpp
#                                            rather than a Tier-2 GPU-gated
#                                            bucket. Always built - NetworkServer/
#                                            NetworkRoutes always compile
#                                            regardless of GTE_ENABLE_NETWORK
#                                            (only Application's own
#                                            production call site is gated -
#                                            AGENTS.md, "Networking"), so both
#                                            files pass identically whether
#                                            the switch is ON or OFF.
#   Network/CaptureEndpointsEndToEndTests.cpp - network-impl-2 campaign,
#                                            Phase 6
#                                            (task_manager/network-impl-2/
#                                            PHASE6_AUTOMATED_TESTS_DOCS_AND_REGRESSION_SAFETY.md)
#                                            - the campaign's own strongest
#                                            regression proof: a real
#                                            gte::FrameCaptureBridge + a real
#                                            gte::Network::NetworkServer,
#                                            wired together exactly like
#                                            gte::Application does in
#                                            production, hit over a real
#                                            loopback socket via
#                                            httplib::Client for BOTH
#                                            /get_game_view and
#                                            /get_swapchain - default raw-PNG
#                                            response (PNG magic bytes
#                                            checked), ?format=base64's JSON
#                                            envelope decoding back to the
#                                            exact same bytes as the raw
#                                            response, a second concurrent
#                                            request of the same kind
#                                            returning 503 promptly, and the
#                                            bridge's own TimedOut result
#                                            (the same one the HTTP route
#                                            forwards verbatim as a 504) -
#                                            all driven by a FAKE, GPU-free
#                                            capture producer
#                                            (FakeMainThreadStandIn, local to
#                                            this file) standing in for
#                                            Application::Run()'s real
#                                            per-frame capture-servicing
#                                            logic, so no live Renderer/
#                                            Vulkan device is needed. Shares
#                                            NetworkServerTests.cpp's own
#                                            WaitUntilAcceptingConnections()
#                                            poll helper via
#                                            Network/NetworkTestHelpers.h.
#                                            Tier 2's GPU-touching half of
#                                            this campaign (Phases 3-5) stays
#                                            an accepted, documented gap - see
#                                            AGENTS.md, "Testability &
#                                            Regression Safety".
#
#   Physics/VerletIntegrationTests.cpp      - Verlet-integration campaign
#                                            (task_manager/verlet-integration-1/
#                                            PHASE1_CORE_VERLET_PHYSICS_FOUNDATION.md),
#                                            Phase 1's Position-Verlet
#                                            integrator (IntegrateParticle() -
#                                            src/Physics/VerletIntegration.h):
#                                            a particle at rest under zero
#                                            acceleration/zero damping stays
#                                            exactly at rest, an implied
#                                            velocity continues in a straight
#                                            line under zero acceleration/zero
#                                            damping (Newton's first law - the
#                                            core Verlet correctness check),
#                                            damping of 1.0 fully kills implied
#                                            velocity after one step, a pinned
#                                            particle never moves regardless of
#                                            acceleration/damping and its
#                                            previousPosition tracks position
#                                            every call, and gravity-only
#                                            free-fall over many fixed steps
#                                            matches the closed-form
#                                            0.5*g*t^2 within a small
#                                            tolerance. No ECS/GPU/Renderer
#                                            involved at all - src/Physics/ is
#                                            a brand-new, always-compiled,
#                                            engine-data-free module.
#   Physics/ChainConstraintsTests.cpp       - Phase 1's two independent
#                                            constraint kinds
#                                            (SolveDistanceConstraint()/
#                                            SolveGoalConstraint() - src/
#                                            Physics/ChainConstraints.h): two
#                                            particles stretched beyond
#                                            restLength are pulled back
#                                            together by exactly the
#                                            mass-weighted amount the formula
#                                            predicts, two particles already
#                                            at restLength are left untouched,
#                                            a particle with one side pinned
#                                            receives 100% of the correction
#                                            while the pinned side receives
#                                            none, both particles having
#                                            inverseMass == 0 WITHOUT either
#                                            being flagged pinned are left
#                                            completely untouched (proves the
#                                            guard checks invMassSum directly
#                                            and never divides by zero -
#                                            PHASE0 Revision Notes finding #1),
#                                            and SolveGoalConstraint with
#                                            stiffness01 == 0/1/0.5 leaves the
#                                            particle untouched/snaps it
#                                            exactly onto the target/lands
#                                            exactly halfway respectively. No
#                                            ECS/GPU/Renderer involved.
#   Physics/WindFieldTests.cpp              - Phase 1's procedural,
#                                            deterministic global wind field
#                                            (ComputeWindAcceleration() - src/
#                                            Physics/WindField.h): the same
#                                            (settings, worldPosition,
#                                            timeSeconds) triple always
#                                            produces the bit-identical output
#                                            vector (determinism check, no
#                                            RNG/global state of any kind),
#                                            baseStrength alone with zero gust
#                                            produces a constant vector
#                                            regardless of timeSeconds, and two
#                                            different worldPositions with gust
#                                            enabled produce measurably
#                                            different results (proves the
#                                            spatial-hash term is actually
#                                            wired in, not a dead parameter).
#                                            No ECS/GPU/Renderer involved.
#   Animation/BoneWorldMatrixQueryTests.cpp - verlet-integration-1 campaign
#                                            (PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md),
#                                            the shared bone-world-matrix
#                                            query (ComputeBoneWorldMatrix() -
#                                            src/Animation/BoneWorldMatrixQuery.h)
#                                            promoted out of IkSolver.cpp's
#                                            own anonymous namespace: bind-pose
#                                            world matrices match each bone's
#                                            own bind position, a root
#                                            translation offset propagates to
#                                            every descendant, results agree
#                                            EXACTLY with
#                                            ComputeSkinningMatrices() (Skeleton
#                                            Pose.h) for the same resolved
#                                            pose (proving the extraction
#                                            changed nothing behaviorally),
#                                            and an out-of-range bone index
#                                            returns Identity(). No ECS/GPU/
#                                            Renderer involved.
#   Physics/DynamicChainSolverTests.cpp     - Phase 2's chain-level
#                                            orchestration (StepDynamicChain() -
#                                            src/Physics/DynamicChainSolver.h):
#                                            full stiffness (1.0) stays glued
#                                            EXACTLY to the animated target
#                                            every frame regardless of where
#                                            the chain started, zero stiffness
#                                            sags under gravity but never
#                                            stretches past
#                                            sum(restLengths)*1.01, repeated
#                                            steps converge (finite, bounded,
#                                            settling implied velocity) rather
#                                            than diverging/oscillating
#                                            forever, and a dedicated
#                                            regression proves stiffness/
#                                            constraintIterations stay fully
#                                            DECOUPLED (changing
#                                            constraintIterations alone never
#                                            changes the FRACTION of remaining
#                                            distance the goal constraint
#                                            covers - PHASE0 Revision Notes
#                                            finding #3). Phase 5 extends this
#                                            file with: a head collider keeps
#                                            every joint off its surface every
#                                            step despite gravity pulling the
#                                            chain into it; collision is a
#                                            documented no-op whenever
#                                            hasHeadCollider is false even if a
#                                            non-null collider is passed in; an
#                                            implausible root teleport
#                                            re-seeds the chain onto its NEW
#                                            animated target instead of
#                                            whipping across the gap, and does
#                                            NOT re-trigger on the very next
#                                            (stationary-root) call; and a
#                                            manually NaN-poisoned particle
#                                            position comes out finite and
#                                            sane after one call. No ECS/GPU/
#                                            Renderer/SkeletonData involved.
#   Physics/DynamicChainSolverIdleSettlingTests.cpp - task_manager/
#                                            verlet-integration-7 Phase 5
#                                            (PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md,
#                                            Culprit E) - proves the RE-TUNED
#                                            default DynamicJointSettings
#                                            (damping = 0.4f, stiffness = 0.02f
#                                            - src/Physics/
#                                            DynamicChainDefinition.h) plus the
#                                            unchanged default
#                                            GlobalPhysicsSettings (src/Physics/
#                                            GlobalPhysicsSettings.h) produce a
#                                            believable, STABLE idle result for
#                                            a chain that is NEVER animated: a
#                                            perfectly still (bind-pose target
#                                            held perfectly constant, no root
#                                            motion) 5-joint chain sags visibly
#                                            (> 1% of its own total rest length)
#                                            under gravity rather than looking
#                                            "dead"/rigid, genuinely settles
#                                            into a steady shape well within
#                                            half of a 10-simulated-second
#                                            budget (600 steps @ 1/60s) rather
#                                            than oscillating/jittering
#                                            forever, and never diverges/
#                                            explodes (> 1.5x total rest
#                                            length)/NaNs - plus a second test
#                                            proving the same tuned defaults
#                                            generalize to a different joint
#                                            count with non-uniform, sub-unit
#                                            rest lengths, so the tuning isn't
#                                            accidentally overfit to one
#                                            specific chain shape. No ECS/GPU/
#                                            Renderer/SkeletonData involved -
#                                            calls StepDynamicChain() directly,
#                                            same convention as
#                                            DynamicChainSolverTests.cpp above.
#   Physics/BoneChainPhysicsResolverTests.cpp - Phase 2's position -> rotation
#                                            bridge
#                                            (ApplyDynamicChainPhysicsToPose() -
#                                            src/Physics/
#                                            BoneChainPhysicsResolver.h): a
#                                            genuine round-trip test - moving a
#                                            joint's simulated target sideways
#                                            by its own bind length produces a
#                                            rotation that, fed back through
#                                            ComputeBoneWorldMatrix(), actually
#                                            lands the bone at the requested
#                                            position (translation left
#                                            unchanged); a target already
#                                            aligned with the bone's current
#                                            direction leaves its rotation
#                                            untouched; a 3-joint chain applies
#                                            root-to-tip in dependency order;
#                                            and an out-of-range joint bone
#                                            index is skipped gracefully. No
#                                            ECS/GPU/Renderer involved.
#   Physics/FixedTimestepAccumulatorTests.cpp - Phase 3's fixed-timestep
#                                            accumulator
#                                            (ComputeFixedStepCount() -
#                                            src/Physics/
#                                            FixedTimestepAccumulator.h,
#                                            Culprit D): a normal ~60fps delta
#                                            yields exactly one step and zero
#                                            leftover; a delta of exactly two
#                                            fixed timesteps yields exactly
#                                            two steps and zero leftover; a
#                                            huge (5 second) delta is clamped
#                                            to maxStepsPerFrame and leaves
#                                            leftover time bounded to at most
#                                            one fixed timestep (the
#                                            spiral-of-death guard); a delta
#                                            smaller than one fixed timestep
#                                            yields zero steps and simply
#                                            accumulates across calls; and a
#                                            non-positive fixed timestep
#                                            returns zero steps rather than
#                                            dividing by zero/looping forever.
#                                            No ECS/GPU/Renderer involved.
#   Physics/DynamicChainDetectionTests.cpp  - Phase 4's chain auto-detection
#                                            algorithm (DetectDynamicChains() -
#                                            src/Physics/
#                                            DynamicChainDetection.h): a
#                                            simple 3-joint linear run is
#                                            detected with the correct
#                                            rootBoneIndex/joint order/
#                                            bind-pose restLengths; a
#                                            BRANCHING run (one bone with two
#                                            deformAfterPhysics children)
#                                            detects two separate chains,
#                                            each anchored at the branch bone
#                                            and starting at its own child; a
#                                            run shorter than
#                                            minimumChainLength detects
#                                            nothing; a matching Dynamic
#                                            RigidBody overrides only its own
#                                            joint's mass/damping while
#                                            siblings keep the defaults; a
#                                            degenerate skeleton whose OWN
#                                            root bone is itself flagged
#                                            deformAfterPhysics produces ZERO
#                                            chains from that run (never
#                                            rootBoneIndex == -1) while an
#                                            unrelated, properly-anchored
#                                            chain elsewhere in the same
#                                            skeleton is still detected
#                                            normally; and an empty skeleton
#                                            detects nothing. No ECS/GPU/
#                                            Renderer involved.
#   Physics/DynamicChainDefinitionTests.cpp - task_manager/verlet-integration-5,
#                                            Phase 1's shared chain lookup
#                                            (FindDynamicChainJointByBoneIndex() -
#                                            src/Physics/
#                                            DynamicChainDefinition.h): a
#                                            bone index that is one of a
#                                            chain's own jointBoneIndices
#                                            resolves to the correct
#                                            (chainIndex, jointIndexInChain)
#                                            pair for both a first and a
#                                            second chain; a bone index that
#                                            is not a joint of ANY chain
#                                            (including a chain's own
#                                            rootBoneIndex, which is never
#                                            itself a joint) returns
#                                            invalid; a negative bone index
#                                            returns invalid immediately; an
#                                            empty chains list returns
#                                            invalid for any input; and the
#                                            result is never partially valid
#                                            (both fields set or both -1,
#                                            never a mix). No ECS/GPU/
#                                            Renderer involved.
#   Physics/SphereColliderTests.cpp         - Phase 5's bare-bone single-
#                                            sphere collision projection
#                                            (SolveSphereCollision() -
#                                            src/Physics/SphereCollider.h): a
#                                            particle fully outside the sphere
#                                            is untouched (position AND
#                                            previousPosition), a particle
#                                            inside is projected EXACTLY onto
#                                            the surface along the correct
#                                            center->particle direction (both
#                                            at the world origin and off-
#                                            center), a pinned particle is
#                                            never moved even when sitting
#                                            dead center, the degenerate
#                                            at-center case produces a finite
#                                            result rather than NaN, and a
#                                            non-positive radius is a no-op.
#                                            No ECS/GPU/Renderer involved.
#   Physics/BoxColliderTests.cpp,             - verlet-integration-9 PHASE1's
#   Physics/CapsuleColliderTests.cpp,           new Box/Capsule collision
#   Physics/ColliderTests.cpp                   primitives (SolveBoxCollision()/
#                                            SolveCapsuleCollision() -
#                                            src/Physics/BoxCollider.h/
#                                            CapsuleCollider.h - plus the
#                                            unified, shape-tagged
#                                            SolveCollision() dispatcher in
#                                            src/Physics/Collider.h): an
#                                            outside particle is untouched, a
#                                            penetrating particle is pushed to
#                                            the nearest face/surface (Box:
#                                            smallest-escape-distance axis;
#                                            Capsule: nearest point on its own
#                                            central segment, clamped to the
#                                            end caps), rotation is actually
#                                            applied (not ignored), a pinned
#                                            particle is never moved, and every
#                                            documented degenerate/no-op case
#                                            (non-positive half-extent/radius/
#                                            height) produces a finite result.
#                                            ColliderTests.cpp itself is a thin
#                                            dispatch-repacking check, not a
#                                            re-test of the underlying math.
#                                            No ECS/GPU/Renderer involved.
#   Physics/ModelColliderDetectionTests.cpp - verlet-integration-9 PHASE3's
#                                            model-wide collider auto-
#                                            detection (DetectModelColliders() -
#                                            src/Physics/
#                                            ModelColliderDetection.h): every
#                                            RigidBodyMotionType::Static rigid
#                                            body of a non-degenerate shape
#                                            (Sphere/Box/Capsule) becomes a
#                                            ModelColliderDefinition with a
#                                            correctly precomputed bind-pose-
#                                            relative offset (position AND
#                                            rotation) from its own tracked
#                                            bone, round-tripped against a
#                                            hand-derived expected offset; a
#                                            Dynamic/DynamicAndBoneMerge body
#                                            is never included; an unattached
#                                            (boneIndex == -1) or out-of-range
#                                            boneIndex body is skipped; a
#                                            degenerate shape (non-positive
#                                            radius/all-non-positive box half-
#                                            sorted by ascending boneIndex,
#                                            ties broken by original rigid-body
#                                            index; and a null PhysicsData
#                                            pointer returns an empty list
#                                            rather than crashing. No ECS/GPU/
#                                            Renderer involved.
#   Physics/ModelColliderDetectionGroupMaskTests.cpp - task_manager/
#                                            verlet-integration-10 PHASE1:
#                                            DetectModelColliders() now copies
#                                            a Static RigidBody's own
#                                            group/collisionGroupMask
#                                            VERBATIM into the detected
#                                            ModelColliderDefinition's
#                                            group/collisionMask fields -
#                                            including a literal zero mask
#                                            (proving this is a verbatim
#                                            copy, never silently replaced by
#                                            Collider's own "default to
#                                            0xFFFF" convenience). No ECS/GPU/
#                                            Renderer involved.
#   Physics/DynamicChainSolverCollisionGroupFilterTests.cpp - task_manager/
#                                            verlet-integration-10 PHASE1: the
#                                            Bullet-style symmetric group/mask
#                                            AND-test added to
#                                            StepDynamicChain()'s own
#                                            collision loop - same-group
#                                            joint/collider pairs collide,
#                                            disjoint-mask pairs do not (even
#                                            with collisionEnabled == true),
#                                            the filter is genuinely
#                                            symmetric (both sides must
#                                            allow each other), the pre-
#                                            PHASE1 all-default behavior is
#                                            reproduced exactly, and an
#                                            out-of-documented-range group
#                                            byte is masked safely (never
#                                            undefined behavior) and produces
#                                            the same result as its
#                                            in-range low-4-bit equivalent.
#                                            No ECS/GPU/Renderer involved.
#   Physics/DynamicChainDetectionJointRadiusTests.cpp - task_manager/
#                                            verlet-integration-10 PHASE2:
#                                            DetectDynamicChains()'s Step G
#                                            now also derives each joint's
#                                            own DynamicJointSettings::
#                                            collisionRadius from its
#                                            matched Dynamic RigidBody's
#                                            shape/shapeSize
#                                            (DeriveJointCollisionRadius()) -
#                                            Sphere/Capsule use their own
#                                            authored radius directly (a
#                                            Capsule's height never
#                                            contributes), Box uses its
#                                            smallest half-extent (a safe
#                                            inscribed-sphere
#                                            approximation), a degenerate
#                                            negative/zero shape clamps to
#                                            exactly 0.0f, and each joint of
#                                            a multi-joint chain derives its
#                                            own radius independently. No
#                                            ECS/GPU/Renderer involved.
#   Game/Animation/AnimationSystemEvaluatePosesTests.cpp - Phase 3's split of
#                                            AnimationSystem::Update() into
#                                            EvaluatePoses()/SkinAndUpload()
#                                            (src/Game/Animation/
#                                            AnimationSystem.h/.cpp): a hand-
#                                            built Registry + minimal
#                                            SkinnedMeshData + a real temp
#                                            *.gta AssetType::Animation file
#                                            (same convention as
#                                            Game/AnimationClipCacheTests.cpp)
#                                            confirms EvaluatePoses() writes a
#                                            ResolvedAnimationPose component
#                                            matching an independently
#                                            computed sample -> IK -> append
#                                            pose EXACTLY, and that skinning
#                                            matrices computed FROM that
#                                            component are byte-identical to
#                                            what the OLD, single-call
#                                            EvaluateAnimatedSkinningPose()
#                                            would have produced - proving the
#                                            split is a genuine behavioral
#                                            no-op; plus two degrade-
#                                            gracefully cases (SkinAndUpload()
#                                            never crashes with no GPU mesh
#                                            parts registered, and correctly
#                                            skips an entity EvaluatePoses()
#                                            never produced a pose for this
#                                            frame). No live Renderer/GPU
#                                            device involved (RenderSystem/
#                                            MeshInstantiationSystem are both
#                                            default-constructible with no GPU
#                                            dependency).
#   Game/Animation/AnimationSystemSkinAndUploadPhysicsOnlyTests.cpp -
#                                            verlet-integration-7 Phase 2
#                                            (PHASE2_SKIN_AND_UPLOAD_VISIBILITY_FOR_PHYSICS_ONLY_ENTITIES.md):
#                                            proves SkinAndUpload()'s new
#                                            Source 2 (every enabled
#                                            DynamicChainRig entity) actually
#                                            reaches the shared
#                                            SkinAndUploadOneEntity() body -
#                                            a physics-only, never-animated
#                                            entity is processed without
#                                            crashing; an entity carrying
#                                            BOTH a playing SkeletalAnimator
#                                            AND an enabled DynamicChainRig
#                                            is processed safely exactly
#                                            once (never double-uploaded);
#                                            a disabled rig is skipped
#                                            entirely even with a
#                                            ResolvedAnimationPose already
#                                            present; and a rig with no
#                                            ResolvedAnimationPose yet
#                                            degrades gracefully. GPU-compute
#                                            skinning mode itself is not
#                                            exercised (needs a live
#                                            Renderer - Tier 2, see
#                                            TESTING.md).
#   Game/Physics/PhysicsSystemTests.cpp     - Phase 3's brand-new, ECS/
#                                            Renderer-independent
#                                            PhysicsSystem
#                                            (src/Game/Physics/
#                                            PhysicsSystem.h/.cpp): Phase 3's
#                                            own tests confirmed Update() was
#                                            a genuine, safe NO-OP while
#                                            DynamicChainRigCache was still a
#                                            deliberate, provable stub - an
#                                            entity with a DynamicChainRig
#                                            AND a ResolvedAnimationPose has
#                                            its pose left byte-for-byte
#                                            unchanged, an entity with a
#                                            DynamicChainRig but no
#                                            ResolvedAnimationPose yet is
#                                            skipped gracefully (no
#                                            component is ever added), an
#                                            entity with neither component
#                                            and an entirely empty Registry
#                                            are both safe no-ops, and a
#                                            DISABLED DynamicChainRig
#                                            (enabled == false) is skipped
#                                            entirely even though it (and its
#                                            ResolvedAnimationPose) exist.
#                                            Phase 4 adds a REAL, non-stub,
#                                            end-to-end test: registers a
#                                            synthetic rigged model (a hand-
#                                            built SkeletonData with a
#                                            2-joint deformAfterPhysics run)
#                                            through RegisterDynamicChains()/
#                                            AttachDynamicChainRigIfNeeded(),
#                                            drives a pure-FK
#                                            ResolvedAnimationPose directly
#                                            (no AnimationSystem involved at
#                                            all - further proof the two
#                                            systems are genuinely decoupled),
#                                            runs several Update() calls with
#                                            non-zero deltaSeconds and
#                                            non-zero gravity, and asserts
#                                            the resulting pose actually
#                                            DIFFERS from the pure-FK values
#                                            that were written in - the
#                                            end-to-end proof this whole
#                                            campaign exists to deliver. No
#                                            ECS/GPU/Renderer involved beyond
#                                            a plain Registry.
#   Game/Physics/DynamicChainRigCacheTests.cpp - Phase 4's PhysicsSystem-owned
#                                            cache of detected chains
#                                            (src/Game/Physics/
#                                            DynamicChainRigCache.h): a
#                                            trivial Register()/TryGet()
#                                            round-trip confirms both
#                                            `skeleton` and `chains` survive
#                                            intact; an unknown path returns
#                                            nullptr from both TryGet() and
#                                            TryGetMutable(); and
#                                            TryGetMutable() lets a caller
#                                            (the Editor Inspector's "Dynamic
#                                            Chain Physics" section) live-edit
#                                            a cached chain's
#                                            DynamicJointSettings, visible to
#                                            a subsequent TryGet() call. No
#                                            ECS/GPU/Renderer involved.
#   Game/Physics/PhysicsSystemParallelTests.cpp - Phase 5's
#                                            gte::Jobs::Dispatch() parallel
#                                            path for PhysicsSystem::Update()
#                                            (src/Game/Physics/
#                                            PhysicsSystem.cpp, 3.4): a
#                                            hand-built "few chains" model
#                                            (below kMinDynamicJointsToParallelize,
#                                            forcing the SERIAL path) and a
#                                            geometrically/parametrically
#                                            IDENTICAL "many chains" model
#                                            (above that threshold, forcing
#                                            the PARALLEL Dispatch() path),
#                                            registered directly via
#                                            DynamicChainRigCache (no
#                                            DetectDynamicChains()/PMX/
#                                            AnimationSystem/SkeletalAnimator
#                                            involved at all), stepped through
#                                            the SAME PhysicsSystem instance
#                                            for the same number of frames,
#                                            produce results that agree with
#                                            each other to within a tight
#                                            epsilon for EVERY chain,
#                                            regardless of which path
#                                            actually ran; a second test
#                                            confirms every chain WITHIN one
#                                            large parallel dispatch (several
#                                            worker batches) stays internally
#                                            consistent with every other
#                                            symmetric, independent chain. No
#                                            GPU/Renderer/live Vulkan device
#                                            involved - a plain Registry is
#                                            enough.
#   Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp - Phase 3 of the
#                                            verlet-integration-7 campaign
#                                            (src/Game/Physics/
#                                            PhysicsSystem.cpp): proves
#                                            PhysicsSystem::Update() now
#                                            composes the owning ECS entity's
#                                            REAL, resolved world position/
#                                            rotation (scale excluded) with
#                                            the bone-local pose before
#                                            simulating - a Transform
#                                            translation produces genuine
#                                            inertial lag in the tip joint
#                                            (strictly less than the full
#                                            rigid delta, but still nonzero);
#                                            a sequence of small continuous
#                                            drag deltas never trips the
#                                            teleport-guard re-seed while one
#                                            huge single-frame jump still
#                                            does (and re-seeds cleanly, no
#                                            NaN/Inf); an entity with no
#                                            Transform, or an explicit
#                                            identity one, produces
#                                            byte-identical results (the
#                                            backward-compatibility proof);
#                                            an entity parented under a
#                                            moving ancestor reacts to the
#                                            ancestor's own motion through
#                                            the full resolved parent chain;
#                                            and Transform::scale never
#                                            affects the simulated pose or
#                                            the teleport-guard threshold at
#                                            all (scale-invariance). No
#                                            ECS/GPU/Renderer involved beyond
#                                            a plain Registry.
#   Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp - Phase 4 (v2) of the
#                                            verlet-integration-7 campaign
#                                            (src/Game/Physics/
#                                            PhysicsSystem.cpp, src/ECS/
#                                            Components/DynamicChainRig.h):
#                                            proves the new
#                                            DynamicChainRig::frozen opt-out
#                                            stops further integration while
#                                            keeping the chain's last
#                                            simulated shape, that a frozen
#                                            chain still rides along rigidly
#                                            with further entity Transform
#                                            motion, that un-freezing never
#                                            triggers a multi-step catch-up
#                                            burst (accumulatedSeconds pinned
#                                            at 0 while frozen), and that
#                                            `enabled = false`'s pre-existing
#                                            behavior is completely
#                                            unaffected. Also proves the
#                                            Culprit F fix: an ordinary,
#                                            non-frozen chain (both a
#                                            never-animated T-pose entity and
#                                            a genuinely-playing animated
#                                            entity) never visibly "pops"
#                                            back to raw bind/FK pose on a
#                                            render frame where the
#                                            fixed-timestep accumulator
#                                            legitimately produces
#                                            stepCount == 0 (e.g. simulating a
#                                            240 Hz display). No GPU/Renderer/
#                                            ImGui involved beyond a plain
#                                            Registry (one test also touches a
#                                            real temp directory to feed a
#                                            real *.gta AssetType::Animation
#                                            file through AnimationSystem::
#                                            Play()).
#   Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp -
#                                            task_manager/verlet-integration-8
#                                            campaign (src/Physics/
#                                            BoneChainPhysicsResolver.cpp,
#                                            Phase 1): the full end-to-end
#                                            regression test for "whole body
#                                            looks ragdoll-simulated" (see
#                                            task_manager/verlet-integration-7/
#                                            INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md) -
#                                            a synthetic rig whose chain
#                                            anchor is ALSO a real,
#                                            non-participating FK body bone
#                                            (a stand-in for MMD's own
#                                            下半身/legs) with FIVE
#                                            independent Dynamic accessory
#                                            joints hubbed directly off that
#                                            same anchor (a stand-in for the
#                                            reported model's ~23-25
#                                            "(root child)" hair/skirt
#                                            strands), driven through the
#                                            REAL PhysicsSystem::Update()
#                                            for 300 continuous frames and
#                                            across a live
#                                            Transform.position drag -
#                                            proves the "leg" bone's own
#                                            local pose (both rotation AND
#                                            translation) never moves at
#                                            all, regardless of how many
#                                            accessories hub off its parent
#                                            anchor or how the entity's
#                                            Transform is dragged. No
#                                            GPU/Renderer/ImGui involved
#                                            beyond a plain Registry.
#   Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp -
#                                            task_manager/verlet-integration-9
#                                            PHASE4 (Game/Physics/
#                                            PhysicsSystem.cpp): proves every
#                                            one of a model's auto-detected
#                                            Static-rigid-body colliders
#                                            (Physics/ModelColliderDefinition.h,
#                                            PHASE3) is genuinely re-resolved
#                                            to its CURRENT world-space
#                                            center/rotation every frame -
#                                            never a stale/cached value -
#                                            correctly composing the tracked
#                                            bone's own current animated pose,
#                                            the collider's own FIXED
#                                            bind-pose-relative local offset
#                                            from that bone, and the owning
#                                            ECS entity's own resolved world
#                                            Transform. Every other simulation
#                                            force (gravity/goal
#                                            stiffness/structural relaxation)
#                                            is deliberately neutralized so
#                                            only the collision-resolution
#                                            step itself can ever move a
#                                            joint, isolating this resolution
#                                            math from the solver's own
#                                            dynamics (covered separately by
#                                            DynamicChainSolverTests.cpp). No
#                                            GPU/Renderer/ImGui involved
#                                            beyond a plain Registry.
#   Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp -
#                                            task_manager/verlet-integration-9
#                                            PHASE6, the campaign's own final,
#                                            real, FULL-PIPELINE regression
#                                            test: a single dynamic chain
#                                            hanging from a Static PMX anchor
#                                            genuinely collides against a
#                                            Sphere, a Box, AND a Capsule
#                                            collider SIMULTANEOUSLY - all
#                                            three auto-detected from real
#                                            RigidBodyMotionType::Static PMX
#                                            rigid bodies and resolved to
#                                            world space every frame - driven
#                                            end to end through the REAL
#                                            RegisterDynamicChains() ->
#                                            AttachDynamicChainRigIfNeeded()
#                                            -> Update() pipeline (never a
#                                            hand-passed Collider list),
#                                            exactly the scenario the user
#                                            originally reported as broken.
#                                            A second test proves this is a
#                                            genuine regression guard, not
#                                            trivially true: the identical
#                                            fixture, stepped identically but
#                                            with collision left disabled,
#                                            independently confirmed to
#                                            actually penetrate at least one
#                                            collider. No GPU/Renderer/ImGui
#                                            involved beyond a plain Registry.
#   Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp -
#                                            task_manager/verlet-integration-10
#                                            PHASE5, the campaign's own final,
#                                            real, FULL-PIPELINE regression
#                                            test proving PHASE1 (PMX
#                                            collision-group/layer filtering),
#                                            PHASE2 (a joint's own PMX
#                                            rigid-body shape as its collision
#                                            radius), and PHASE3 (collision
#                                            enabled ON by default) all work
#                                            correctly TOGETHER, driven end to
#                                            end through the REAL
#                                            RegisterDynamicChains() ->
#                                            AttachDynamicChainRigIfNeeded()
#                                            -> Update() pipeline: a 2-joint
#                                            chain where one joint's own PMX
#                                            collision-group/mask rule
#                                            genuinely blocks it from one
#                                            Static collider (confirmed to
#                                            still genuinely penetrate it)
#                                            while its sibling joint (an
#                                            unrestricted mask) and both
#                                            joints' own ordinary interaction
#                                            with a second, unrestricted
#                                            collider are resolved normally,
#                                            with PHASE2's own radius
#                                            inflation explicitly measured. A
#                                            second test proves an
#                                            out-of-documented-range PMX
#                                            `group` byte produces an outcome
#                                            IDENTICAL to the same fixture
#                                            authored with the legitimate,
#                                            in-range value sharing its low 4
#                                            bits, through the FULL pipeline
#                                            (not just PHASE1's own isolated
#                                            solver-level unit test). No
#                                            GPU/Renderer/ImGui involved
#                                            beyond a plain Registry.
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
    Encoding/Base64Tests.cpp
    Encoding/PixelConversionTests.cpp
    Encoding/PngEncoderTests.cpp
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
    Scene/SceneTextFormatTests.cpp
    Scene/SceneBuilderTests.cpp
    Memory/GpuMemoryTrackerTests.cpp
    Memory/SdlMemoryTrackerTests.cpp
    Profiling/FrameProfilerTests.cpp
    Profiling/ScopeTimerTests.cpp
    Profiling/FrameGraphDataTests.cpp
    Profiling/JobScopeTimerTests.cpp
    Profiling/WorkerTimelineDataTests.cpp
    Jobs/JobQueueTests.cpp
    Jobs/JobSystemTests.cpp
    Jobs/JobDispatchMathTests.cpp
    Jobs/JobDispatchTests.cpp
    Jobs/JobContinuationTests.cpp
    Jobs/JobSystemSdlClockThreadSafetyTests.cpp
    Network/NetworkRoutesTests.cpp
    Network/NetworkServerTests.cpp
    Network/CaptureEndpointsEndToEndTests.cpp
    Input/InputStateTests.cpp
    Application/EventTranslatorTests.cpp
    Application/MemorySnapshotBuilderTests.cpp
    Application/FrameCaptureBridgeTests.cpp
    Renderer/VertexTests.cpp
    Renderer/PrimitiveMeshGeneratorTests.cpp
    Renderer/ResourcePoolTests.cpp
    Renderer/DrawStatsTests.cpp
    Renderer/GpuTimingTests.cpp
    Renderer/ComputeDispatchTests.cpp
    Renderer/GpuSkinning/GpuSkinningTypesTests.cpp
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
    Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp
    Game/MeshVertexPackingTests.cpp
    Game/MeshMaterialPartitionerTests.cpp
    Game/SkeletalRigCacheTests.cpp
    Game/AnimationClipCacheTests.cpp
    Game/ResolvedAnimationBindingCacheTests.cpp
    Game/Animation/AnimationSystemEvaluatePosesTests.cpp
    Game/Animation/AnimationSystemSkinAndUploadPhysicsOnlyTests.cpp
    Game/GameLoopPhysicsWithoutAnimationTests.cpp
    Game/Physics/PhysicsSystemTests.cpp
    Game/Physics/DynamicChainPhysicsPersistenceTests.cpp
    Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp
    Game/Physics/DynamicChainRigCacheTests.cpp
    Game/Physics/PhysicsSystemParallelTests.cpp
    Game/Physics/PhysicsSystemWorldSpaceRootMotionTests.cpp
    Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp
    Game/Physics/PhysicsSystemFreezeAndCulpritFTests.cpp
    Game/Physics/PhysicsSystemAnchorRigidityRegressionTests.cpp
    Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp
    Game/Physics/PhysicsSystemCollisionGroupAndRadiusEndToEndTests.cpp
    Animation/BoneChainResolverTests.cpp
    Animation/BonePoseMathTests.cpp
    Animation/SkeletonPoseTests.cpp
    Animation/IkSolverTests.cpp
    Animation/BoneWorldMatrixQueryTests.cpp
    Animation/AppendBoneSolverTests.cpp
    Animation/MotionSamplerTests.cpp
    Animation/AnimationPoseEvaluatorTests.cpp
    Animation/VertexSkinningTests.cpp
    Animation/VertexSkinningParityTests.cpp
    Physics/VerletIntegrationTests.cpp
    Physics/ChainConstraintsTests.cpp
    Physics/WindFieldTests.cpp
    Physics/DynamicChainSolverTests.cpp
    Physics/DynamicChainSolverIdleSettlingTests.cpp
    Physics/BoneChainPhysicsResolverTests.cpp
    Physics/FixedTimestepAccumulatorTests.cpp
    Physics/DynamicChainDetectionTests.cpp
    Physics/RigidBodyJointGraphTests.cpp
    Physics/DynamicChainDefinitionTests.cpp
    Physics/SphereColliderTests.cpp
    Physics/BoxColliderTests.cpp
    Physics/CapsuleColliderTests.cpp
    Physics/ColliderTests.cpp
    Physics/ModelColliderDetectionTests.cpp
    Physics/ModelColliderDetectionGroupMaskTests.cpp
    Physics/DynamicChainSolverCollisionGroupFilterTests.cpp
    Physics/DynamicChainDetectionJointRadiusTests.cpp
    Physics/JointPhysicsOverrideApplicationTests.cpp
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
        Editor/JobsPanelDataTests.cpp
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
            Editor/ModelRigCacheTests.cpp
            Editor/RigidBodyWireframeTests.cpp
            Editor/RigidBodySelectionAlgorithmsTests.cpp
            Editor/FlatListRangeSelectionTests.cpp
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
