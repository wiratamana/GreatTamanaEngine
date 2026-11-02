# Unit tests (GoogleTest) and the CI guardrail scans. Included from the root CMakeLists.txt.

# ---- helpers ------------------------------------------------------------------------------

# Adds compile definitions NAME="<base_dir>/<relative path>" from "NAME=relative path" entries.
function(gte_test_path_defines target base_dir)
    foreach(_entry ${ARGN})
        if(NOT _entry MATCHES "^([^=]+)=(.+)$")
            message(FATAL_ERROR "gte_test_path_defines: bad entry '${_entry}'")
        endif()
        target_compile_definitions(${target} PRIVATE "${CMAKE_MATCH_1}=\"${base_dir}/${CMAKE_MATCH_2}\"")
    endforeach()
endfunction()

# Registers a gte_ci_scan run over a source folder. The scan data lives in tools/ci/<CHECK>/.
#   ROOT <dir under the repo>   SKIP / SKIP_DIR / ONLY <paths...>   (all optional)
#   TOKENS <file>               INCLUDES <file> (optional)   COMMENTS / STRINGS <skip|scan>
#   IGNORE_CASE                 CHECK <folder name>
function(gte_add_scan_test name)
    cmake_parse_arguments(A "IGNORE_CASE" "CHECK;ROOT;TOKENS;INCLUDES;COMMENTS;STRINGS" "SKIP;SKIP_DIR;ONLY" ${ARGN})
    set(_data "${CMAKE_SOURCE_DIR}/tools/ci/${A_CHECK}")
    set(_args --root "${CMAKE_SOURCE_DIR}/${A_ROOT}")
    foreach(_path ${A_SKIP})
        list(APPEND _args --skip "${_path}")
    endforeach()
    foreach(_path ${A_SKIP_DIR})
        list(APPEND _args --skip-dir "${_path}")
    endforeach()
    foreach(_path ${A_ONLY})
        list(APPEND _args --only "${_path}")
    endforeach()
    list(APPEND _args --tokens "${_data}/${A_TOKENS}")
    if(A_INCLUDES)
        list(APPEND _args --includes "${_data}/${A_INCLUDES}")
    endif()
    list(APPEND _args --allowlist "${_data}/allowlist.txt" --comments ${A_COMMENTS} --strings ${A_STRINGS})
    if(A_IGNORE_CASE)
        list(APPEND _args --ignore-case)
    endif()
    add_test(NAME ${name} COMMAND gte_ci_scan ${_args})
endfunction()

# Registers the comment-rule scan over <root> limited to the given paths.
function(gte_add_comment_rules_test name root)
    set(_args "")
    foreach(_path ${ARGN})
        list(APPEND _args --only "${_path}")
    endforeach()
    add_test(NAME ${name} COMMAND gte_ci_scan --root "${CMAKE_SOURCE_DIR}/${root}" ${_args} --comment-rules 3)
endfunction()

# Registers a test that builds one EXCLUDE_FROM_ALL target.
function(gte_add_build_probe_test name target)
    add_test(NAME ${name} COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ${target})
endfunction()

# ---- test executable ----------------------------------------------------------------------

# Every test .cpp under tests/, except the two probe sources built as OBJECT libraries below.
file(GLOB_RECURSE GTE_TEST_SOURCES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(FILTER GTE_TEST_SOURCES EXCLUDE REGEX "^Tools/(HeaderPurityProbe|ProjectDefineProbe)\\.cpp$")
if(NOT GTE_ENABLE_PROJECT_PANEL)
    list(FILTER GTE_TEST_SOURCES EXCLUDE REGEX
        "^Editor/(ProjectPanelData|ProjectScanRunner|AssetInspectorData|ModelRigCache|RigidBodyWireframe|RigidBodySelectionAlgorithms|FlatListRangeSelection)Tests\\.cpp$")
endif()

add_executable(GreatTamanaEngineTests ${GTE_TEST_SOURCES})

target_compile_definitions(GreatTamanaEngineTests PRIVATE
    GTE_REPO_SRC_DIR="${CMAKE_SOURCE_DIR}/src"
    GTE_TEST_SPONZA_DIR="${GTE_SPONZA_DIR}"
    GTE_CORE_ARCHIVE_PATH="$<TARGET_FILE:gte_core>")

# Compiled shader outputs the tests load.
gte_test_path_defines(GreatTamanaEngineTests "${CMAKE_BINARY_DIR}/shaders"
    GTE_TEST_BOXBLUR_SPV_PATH=BoxBlur.comp.spv
    GTE_TEST_VOLUME_PREVIEW_SPV_PATH=VolumeTexturePreview.comp.spv
    GTE_TEST_FRAME_PREVIEW_SPV_PATH=FrameDebuggerPreview.comp.spv
    GTE_TEST_SHADOW_MASK_SPV_PATH=ShadowMask.comp.spv
    GTE_TEST_SHADOW_COMPOSITE_VERT_SPV_PATH=ShadowComposite.vert.spv
    GTE_TEST_SHADOW_COMPOSITE_FRAG_SPV_PATH=ShadowComposite.frag.spv
    GTE_TEST_GTAO_MAIN_SPV_PATH=GtaoMain.comp.spv
    GTE_TEST_GTAO_RESOLVE_SPV_PATH=GtaoResolve.comp.spv
    GTE_TEST_GTAO_COMPOSITE_VERT_SPV_PATH=GtaoComposite.vert.spv
    GTE_TEST_GTAO_COMPOSITE_FRAG_SPV_PATH=GtaoComposite.frag.spv
    GTE_TEST_SSGI_PREPARE_SPV_PATH=SsgiPrepare.comp.spv
    GTE_TEST_SSGI_TRACE_SPV_PATH=SsgiTrace.comp.spv
    GTE_TEST_SSGI_RESOLVE_SPV_PATH=SsgiResolve.comp.spv
    GTE_TEST_SSGI_COMPOSITE_VERT_SPV_PATH=SsgiComposite.vert.spv
    GTE_TEST_SSGI_COMPOSITE_FRAG_SPV_PATH=SsgiComposite.frag.spv
    GTE_TEST_VOLUMETRIC_INJECT_SPV_PATH=VolumetricLightInject.comp.spv
    GTE_TEST_VOLUMETRIC_TEMPORAL_SPV_PATH=VolumetricLightTemporal.comp.spv
    GTE_TEST_VOLUMETRIC_INTEGRATE_SPV_PATH=VolumetricLightIntegrate.comp.spv
    GTE_TEST_VOLUMETRIC_COMPOSITE_SPV_PATH=VolumetricLightComposite.comp.spv
    GTE_TEST_TONEMAPPING_SPV_PATH=ToneMapping.comp.spv
    GTE_TEST_RENDER_FEATURE_BLEND_SPV_PATH=RenderFeatureBlend.comp.spv
    GTE_TEST_SKIN_SPV_PATH=SkinVerticesPositionNormal.comp.spv
    GTE_TEST_SKIN_UV_SPV_PATH=SkinVerticesPositionNormalUv.comp.spv
    GTE_TEST_FROXEL_PERCELL_SPV_PATH=VolumetricFroxelExamplePerCellFill.comp.spv
    GTE_TEST_FROXEL_PERCOLUMN_SPV_PATH=VolumetricFroxelExamplePerColumnFill.comp.spv
    GTE_TEST_REFLECT_COMP_SPV_PATH=ReflectFixture.comp.spv
    GTE_TEST_REFLECT_VERT_SPV_PATH=ReflectFixture.vert.spv
    GTE_TEST_REFLECT_FRAG_SPV_PATH=ReflectFixture.frag.spv
    GTE_TEST_REFLECT_UNIFORM_SPV_PATH=ReflectFixtureUniform.comp.spv
    GTE_TEST_BUFFER_FIXTURE_SPV_PATH=BufferFixture.comp.spv)

# Shader sources the tests read as text.
gte_test_path_defines(GreatTamanaEngineTests "${CMAKE_SOURCE_DIR}/src"
    GTE_TEST_SHADERS_DIR=Shaders
    GTE_TEST_SHADOW_SHADER_DIR=Features/Shadow/Shaders
    GTE_TEST_ATMOSPHERE_SHADER_DIR=Features/Atmosphere/Shaders
    GTE_TEST_VOLUMETRIC_SHADER_DIR=Features/VolumetricLight/Shaders
    GTE_TEST_LIGHTING_GLSL_PATH=Shaders/DirectionalLighting.glsl
    GTE_TEST_MATERIAL_PARAMS_GLSL_PATH=Shaders/MaterialParams.glsl
    GTE_TEST_PBR_BRDF_GLSL_PATH=Shaders/PbrBrdf.glsl
    GTE_TEST_PBR_MATERIAL_GLSL_PATH=Shaders/PbrMaterial.glsl
    GTE_TEST_SCENE_DEPTH_CONSTANTS_GLSL_PATH=Shaders/SceneDepthConstants.glsl
    GTE_TEST_CLUSTER_CULL_SHADER_PATH=Shaders/ClusterCull.comp
    GTE_TEST_HIZ_BUILD_BASE_SHADER_PATH=Shaders/HizBuildBase.comp
    GTE_TEST_HIZ_BUILD_TOP_SHADER_PATH=Shaders/HizBuildTop.comp
    GTE_TEST_SSGI_COMMON_GLSL_PATH=Features/Ssgi/Shaders/SsgiCommon.glsl
    GTE_TEST_SSGI_COMPOSITE_GLSL_PATH=Features/Ssgi/Shaders/SsgiComposite.frag
    GTE_TEST_TONEMAPPING_GLSL_PATH=Features/ToneMapping/Shaders/ToneMapping.comp)
gte_test_path_defines(GreatTamanaEngineTests "${CMAKE_BINARY_DIR}/shaders"
    GTE_TEST_CLUSTER_CULL_CONSTANTS_GLSL_PATH=gen/ClusterCullConstants.glsl)

# Shaders already registered by src/ are copied next to the test exe. Then the test-only fixtures
# are compiled and staged (gte_add_shader stages what it compiles).
get_property(_gte_compiled_shaders GLOBAL PROPERTY GTE_COMPILED_SHADERS)
foreach(_shader ${_gte_compiled_shaders})
    gte_stage_shader(GreatTamanaEngineTests "${_shader}")
endforeach()
foreach(_fixture ReflectFixture.comp ReflectFixture.vert ReflectFixture.frag ReflectFixtureUniform.comp
                 BufferFixture.comp GpuParityFill.comp GpuParityDump.comp)
    gte_add_shader(GreatTamanaEngineTests tests/Renderer/Shaders/${_fixture})
endforeach()

target_link_libraries(GreatTamanaEngineTests PRIVATE gte_editor GTest::gtest GTest::gtest_main imgui)
# Keeps self-registering translation units in the engine archives.
target_link_options(GreatTamanaEngineTests PRIVATE
    "-Wl,--whole-archive" "$<TARGET_FILE:gte_core>" "$<TARGET_FILE:gte_editor>" "-Wl,--no-whole-archive")
sdl3_copy_runtime_dll(GreatTamanaEngineTests)

include(GoogleTest)
gtest_discover_tests(GreatTamanaEngineTests)

# ---- CI guardrail scans -------------------------------------------------------------------

add_executable(gte_ci_scan "${CMAKE_SOURCE_DIR}/tools/ci/common/CiScan.cpp")
target_compile_features(gte_ci_scan PRIVATE cxx_std_20)
if(MINGW)
    target_link_options(gte_ci_scan PRIVATE -static)
endif()
add_dependencies(GreatTamanaEngineTests gte_ci_scan)
target_include_directories(GreatTamanaEngineTests PRIVATE "${CMAKE_SOURCE_DIR}/tools/ci/common")

add_test(NAME AtmosphereCoreDecouplingCheck
    COMMAND "${CMAKE_COMMAND}" -D "GTE_REPO_ROOT=${CMAKE_SOURCE_DIR}"
            -P "${CMAKE_SOURCE_DIR}/tools/ci/atmosphere_core_decoupling_check/CheckAtmosphereCoreDecoupling.cmake")

gte_add_scan_test(VolkLeakCheck CHECK volk_leak_check ROOT src SKIP Renderer/
    TOKENS tokens.txt COMMENTS skip STRINGS scan)
gte_add_scan_test(BlockingCallCheck CHECK blocking_call_check ROOT src SKIP Renderer/Vulkan/GpuWaitService.cpp
    TOKENS tokens.txt COMMENTS skip STRINGS scan)
gte_add_scan_test(RemovedSymbolCheckSrc CHECK removed_symbol_check ROOT src
    TOKENS tokens.txt COMMENTS scan STRINGS scan)
gte_add_scan_test(RemovedSymbolCheckTests CHECK removed_symbol_check ROOT tests
    TOKENS tokens.txt COMMENTS scan STRINGS scan)
gte_add_scan_test(NativeLeakCheck CHECK native_leak_check ROOT src SKIP Renderer/
    TOKENS tokens.txt INCLUDES includes.txt COMMENTS skip STRINGS scan)
gte_add_scan_test(FeaturePurityCheck CHECK feature_purity_check ROOT src/Features SKIP_DIR Editor
    TOKENS tokens.txt INCLUDES includes.txt COMMENTS skip STRINGS skip)
gte_add_scan_test(EngineVocabularyCheck CHECK engine_vocabulary_check ROOT src
    ONLY Renderer/ Core/ Profiling/ Assets/MeshData.h Assets/MeshFile. Assets/MeshCluster Assets/AtomicFile.
         Assets/GtaFile. Assets/MeshMaterialPartitioner. Assets/MeshNormalRecompute Assets/Stl
         Assets/AssetImporter. Assets/Crc32.h Assets/MeshClustering
    TOKENS tokens.txt COMMENTS scan STRINGS scan IGNORE_CASE)
gte_add_scan_test(ProfilerVocabularyCheck CHECK profiler_vocabulary_check ROOT src
    ONLY Profiling/ Renderer/RenderGraph/ Renderer/GpuTiming Renderer/FramePresenter
    TOKENS tokens.txt COMMENTS scan STRINGS scan)
gte_add_scan_test(ProfilerVocabularyCheckPlumbing CHECK profiler_vocabulary_check ROOT src
    ONLY Core/ Renderer/ Profiling/ Application/RenderPassViewData.h Editor/EditorLayer.h Editor/NullEditorLayer.cpp
    TOKENS plumbing_tokens.txt COMMENTS scan STRINGS scan)

include("${CMAKE_CURRENT_SOURCE_DIR}/CommentRuleScopes.cmake")
gte_add_comment_rules_test(CommentRulesSrc src ${GTE_COMMENT_RULE_SRC})
gte_add_comment_rules_test(CommentRulesTests tests ${GTE_COMMENT_RULE_TESTS})

# ---- compile-only probes (built on demand by the tests below) -----------------------------

add_library(gte_header_purity_probe OBJECT EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/tests/Tools/HeaderPurityProbe.cpp")
target_include_directories(gte_header_purity_probe PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_features(gte_header_purity_probe PRIVATE cxx_std_20)
target_compile_definitions(gte_header_purity_probe PRIVATE GTE_ENABLE_PROFILER=1)
gte_add_build_probe_test(HeaderPurity gte_header_purity_probe)

add_library(gte_project_define_probe OBJECT EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/tests/Tools/ProjectDefineProbe.cpp")
gte_project_assembly_apply_header_paths(gte_project_define_probe)
target_compile_features(gte_project_define_probe PRIVATE cxx_std_20)
gte_add_build_probe_test(ProjectDefineProbe gte_project_define_probe)
