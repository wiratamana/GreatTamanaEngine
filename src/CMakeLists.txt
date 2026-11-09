# Engine targets:
#   gte_core           static library, no SDL3 or ImGui
#   gte_editor         static library, SDL3 window + ImGui editor (skipped in probe-only mode)
#   GreatTamanaEditor  executable, plus shaders and project assemblies

# Sources are globbed relative to this folder. main.cpp is the executable's only own source.
file(GLOB_RECURSE GTE_ALL_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl")
list(FILTER GTE_ALL_SOURCES EXCLUDE REGEX "^main\\.cpp$")

# gte_editor owns everything that touches SDL3 or ImGui. These three Editor files stay in gte_core.
set(_editor_re "^(Window/|Memory/SdlMemoryTracker\\.|Application/EventTranslator\\.|Editor/|Features/[^/]+/Editor/)")
set(_core_in_editor_re "^Editor/(NullEditorLayer|OverlayRaster|SceneViewAxes)\\.")
set(_project_panel_re "^Editor/(ProjectPanelData|ProjectScanRunner|Panels/ProjectPanel|AssetInspectorData|AssetPreviewTexture|AssetPreviewMesh|ModelRigCache|RigidBodyWireframe|RigidBodyGroupSelection|FlatListRangeSelection|BoneViewerWindow)\\.")

set(GTE_EDITOR_SOURCES ${GTE_ALL_SOURCES})
list(FILTER GTE_EDITOR_SOURCES INCLUDE REGEX "${_editor_re}")
list(FILTER GTE_EDITOR_SOURCES EXCLUDE REGEX "${_core_in_editor_re}")
set(GTE_CORE_SOURCES ${GTE_ALL_SOURCES})
list(REMOVE_ITEM GTE_CORE_SOURCES ${GTE_EDITOR_SOURCES})
if(NOT GTE_ENABLE_PROJECT_PANEL)
    list(FILTER GTE_EDITOR_SOURCES EXCLUDE REGEX "${_project_panel_re}")
endif()

# --- gte_core ----------------------------------------------------------------
add_library(gte_core STATIC ${GTE_CORE_SOURCES})
target_include_directories(gte_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(_feature PROJECT_PANEL PROFILER JOB_SYSTEM NETWORK PROJECT_ASSEMBLIES)
    target_compile_definitions(gte_core PUBLIC GTE_ENABLE_${_feature}=$<BOOL:${GTE_ENABLE_${_feature}}>)
endforeach()
target_link_libraries(gte_core
    PUBLIC  volk vma stb_image stb_image_write KTX::ktx httplib nlohmann_json Threads::Threads
    PRIVATE spirv_reflect saba_pmx cgltf mikktspace meshoptimizer)

# --- Probe-only: a CI link probe instead of the editor ---------------------------
if(GTE_CORE_STANDALONE_PROBE_ONLY)
    add_executable(gte_core_player_link_probe
        "${CMAKE_SOURCE_DIR}/tools/ci/gte_core_player_link_probe/main.cpp")
    target_link_libraries(gte_core_player_link_probe PRIVATE gte_core)
    set_target_properties(gte_core gte_core_player_link_probe PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    return()
endif()

# --- gte_editor ----------------------------------------------------------------
add_library(gte_editor STATIC ${GTE_EDITOR_SOURCES})
target_link_libraries(gte_editor
    PUBLIC  gte_core
    PRIVATE imgui imguizmo
    PUBLIC  SDL3::SDL3)
target_compile_definitions(gte_editor PRIVATE "GTE_SPONZA_DIR=\"${GTE_SPONZA_DIR}\"")

# --- GreatTamanaEditor ------------------------------------------------------------
add_executable(GreatTamanaEditor main.cpp)
target_link_libraries(GreatTamanaEditor PRIVATE gte_editor)
# Whole-archive keeps self-registering translation units. Export-all lets project DLLs link to the exe.
target_link_options(GreatTamanaEditor PRIVATE
    "-Wl,--whole-archive" "$<TARGET_FILE:gte_core>" "$<TARGET_FILE:gte_editor>" "-Wl,--no-whole-archive"
    "-Wl,--export-all-symbols")
set_target_properties(GreatTamanaEditor PROPERTIES
    ENABLE_EXPORTS ON
    WINDOWS_EXPORT_ALL_SYMBOLS ON)
gte_apply_shared_crt_linkage(GreatTamanaEditor)
sdl3_copy_runtime_dll(GreatTamanaEditor)

# Keep the binaries in the top-level build folder, where scripts and tests expect them.
set_target_properties(gte_core gte_editor GreatTamanaEditor PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# --- Shaders -------------------------------------------------------------------
# Generate the GLSL constants first: gte_add_shaders_in_dir() scans #include files at configure time.
gte_generate_glsl_constants(
    "${CMAKE_CURRENT_SOURCE_DIR}/Renderer/Culling/ClusterCullConstants.h"
    "${GTE_SHADER_GENERATED_DIR}/ClusterCullConstants.glsl")
gte_add_shaders_in_dir(GreatTamanaEditor src/Shaders)

file(GLOB _feature_shader_dirs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Features/*/Shaders")
foreach(_dir ${_feature_shader_dirs})
    file(RELATIVE_PATH _dir_rel "${CMAKE_SOURCE_DIR}" "${_dir}")
    gte_add_shaders_in_dir(GreatTamanaEditor "${_dir_rel}")
endforeach()

# --- Cloud noise assets ------------------------------------------------------------
gte_add_cloud_noise_assets(GreatTamanaEditor)

# --- Project assemblies ----------------------------------------------------------
# Projects/<Name>/Libraries/CMakeLists.txt calls gte_add_project(<Name>). Their DLLs go here.
set(GTE_PROJECT_ASSEMBLY_OUTPUT_DIR "$<TARGET_FILE_DIR:GreatTamanaEditor>/project_assemblies")
if(GTE_ENABLE_PROJECT_ASSEMBLIES AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/Projects")
    file(GLOB _project_dirs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Projects/*")
    foreach(_dir ${_project_dirs})
        if(IS_DIRECTORY "${_dir}" AND EXISTS "${_dir}/Libraries/CMakeLists.txt")
            file(RELATIVE_PATH _dir_rel "${CMAKE_SOURCE_DIR}" "${_dir}")
            add_subdirectory("${_dir}/Libraries" "${CMAKE_BINARY_DIR}/${_dir_rel}/Libraries")
        endif()
    endforeach()
endif()
