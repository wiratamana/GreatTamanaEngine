# Project assemblies: gte_add_project(<Name>) is called by Projects/<Name>/Libraries/CMakeLists.txt.
#
#   gte_add_project(<Name>)                                  builds <Name>_Game and <Name>_Editor DLLs
#   gte_project_assembly_apply_header_paths(<target>)        engine include paths and GTE_ENABLE_* defines
#   gte_project_assembly_apply_editor_header_paths(<target>) imgui/imguizmo include paths
#   gte_add_project_shaders(<target> <assets dir>)           compiles every shader under the assets dir

# Sources under any "/Editor/" folder go to <Name>_Editor, all other .cpp files to <Name>_Game.
function(gte_add_project NAME)
    # Called from Projects/<Name>/Libraries/, so ".." is Projects/<Name>.
    set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    set(ASSETS "${PROJECT_ROOT}/Assets")

    if(NOT IS_DIRECTORY "${ASSETS}")
        message(WARNING "gte_add_project(${NAME}): no Assets/ folder found under ${PROJECT_ROOT} - nothing to build.")
        return()
    endif()

    file(GLOB_RECURSE ALL_CPP CONFIGURE_DEPENDS "${ASSETS}/*.cpp")

    set(GAME_SOURCES "")
    set(EDITOR_SOURCES "")
    foreach(SRC ${ALL_CPP})
        if(SRC MATCHES "/Editor/")
            list(APPEND EDITOR_SOURCES ${SRC})
        else()
            list(APPEND GAME_SOURCES ${SRC})
        endif()
    endforeach()

    if(GAME_SOURCES)
        add_library(${NAME}_Game SHARED ${GAME_SOURCES})
        target_link_libraries(${NAME}_Game PRIVATE GreatTamanaEditor)
        gte_project_assembly_apply_header_paths(${NAME}_Game)
        gte_apply_project_assembly_shared_crt_linkage(${NAME}_Game)
        set_target_properties(${NAME}_Game PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GTE_PROJECT_ASSEMBLY_OUTPUT_DIR}"
            PREFIX "")
        gte_add_project_shaders(${NAME}_Game "${ASSETS}")
    else()
        message(STATUS "gte_add_project(${NAME}): no non-Editor .cpp files found under ${ASSETS} - ${NAME}_Game.dll not created.")
    endif()

    if(EDITOR_SOURCES)
        add_library(${NAME}_Editor SHARED ${EDITOR_SOURCES})
        target_link_libraries(${NAME}_Editor PRIVATE GreatTamanaEditor)
        gte_project_assembly_apply_header_paths(${NAME}_Editor)
        gte_project_assembly_apply_editor_header_paths(${NAME}_Editor)
        gte_apply_project_assembly_shared_crt_linkage(${NAME}_Editor)
        set_target_properties(${NAME}_Editor PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GTE_PROJECT_ASSEMBLY_OUTPUT_DIR}"
            PREFIX "")
        gte_add_project_shaders(${NAME}_Editor "${ASSETS}")
    endif()
endfunction()

# Gives a project target the include paths of gte_core/gte_editor and their dependencies,
# plus the engine's GTE_ENABLE_* switches. Never links their code (that would duplicate globals).
# Every dependency gte_core links PUBLIC must be listed here; PRIVATE ones (saba_pmx) must not.
function(gte_project_assembly_apply_header_paths TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        $<TARGET_PROPERTY:gte_core,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:gte_editor,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:volk,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:vma,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:stb_image,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:stb_image_write,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:KTX::ktx,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:httplib,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Threads::Threads,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        "$<FILTER:$<TARGET_PROPERTY:gte_core,INTERFACE_COMPILE_DEFINITIONS>,INCLUDE,^GTE_ENABLE_>")
endfunction()

# Globs every .vert/.frag/.comp under <ASSETS_DIR> into gte_add_shader().
# CompileShaders.cmake must be included before this is called, not before it is defined.
function(gte_add_project_shaders TARGET ASSETS_DIR)
    file(GLOB_RECURSE PROJECT_SHADER_SOURCES CONFIGURE_DEPENDS
        "${ASSETS_DIR}/*.vert" "${ASSETS_DIR}/*.frag" "${ASSETS_DIR}/*.comp")

    foreach(SHADER_SOURCE ${PROJECT_SHADER_SOURCES})
        # gte_add_shader() prepends CMAKE_SOURCE_DIR, so it needs a relative path.
        file(RELATIVE_PATH SHADER_SOURCE_RELATIVE "${CMAKE_SOURCE_DIR}" "${SHADER_SOURCE}")
        gte_add_shader(${TARGET} "${SHADER_SOURCE_RELATIVE}")
    endforeach()
endfunction()

# Only for _Editor targets. imgui/imguizmo are kept out of the header-paths function above so a
# _Game target never resolves those target names.
function(gte_project_assembly_apply_editor_header_paths TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:imguizmo,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()
