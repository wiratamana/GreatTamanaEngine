# cmake/GteProject.cmake
#
# editor-core-separation-11 campaign (Project Assembly system), PHASE3.
# Defines gte_add_project(<Name>) - called exactly once by each Project
# Assembly's own generated Projects/<Name>/Libraries/CMakeLists.txt. Mirrors
# this repo's OWN cmake/CompileShaders.cmake / cmake/MingwRuntime.cmake "one
# generic function, called by every real consumer" shape. include()-ed via
# the bare module name (include(GteProject), CMAKE_MODULE_PATH already
# includes cmake/ - root CMakeLists.txt line ~131) - NEVER
# include(cmake/GteProject.cmake), which is not this repo's own convention
# (see include(CompileShaders)/include(MingwRuntime) for the precedent this
# matches).
#
# Extended by PHASE4 (gte_add_project_shaders()) and PHASE7
# (gte_project_assembly_apply_editor_header_paths()) - both are implemented
# here directly, alongside PHASE3's own original functions, per PHASE3's own
# file's explicit guidance ("do not leave a permanent stub" for the shader
# function; "both are tiny" for the editor-header-paths one). PHASE7 itself
# (the real interactive Editor panel, the ImGuiEditorLayer.cpp Finding G
# gate widening, live HTTP verification) is NOT otherwise done here - only
# this one small CMake function or PHASE7's own Step 1 was pulled forward.

function(gte_add_project NAME)
    # CMAKE_CURRENT_SOURCE_DIR here is Projects/<Name>/Libraries/ (the
    # folder this function is called FROM, via add_subdirectory() - root
    # CMakeLists.txt's auto-discovery block) - ".." is therefore
    # Projects/<Name>/ itself.
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
        # Any path containing a path-segment literally named "Editor", at any
        # depth, goes to the Editor assembly instead of the Game one.
        # Case-sensitive, matching this engine's own existing case-sensitive
        # folder-name conventions elsewhere.
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
        gte_project_assembly_apply_editor_header_paths(${NAME}_Editor) # PHASE7 Step 1 - imgui/imguizmo headers only, _Editor targets only
        gte_apply_project_assembly_shared_crt_linkage(${NAME}_Editor)
        set_target_properties(${NAME}_Editor PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${GTE_PROJECT_ASSEMBLY_OUTPUT_DIR}"
            PREFIX "")
        gte_add_project_shaders(${NAME}_Editor "${ASSETS}")
    endif()
endfunction()

# editor-core-separation-11 campaign (Project Assembly system), PHASE3
# (PHASE0_MASTER_STRATEGY.md, Finding C) - GreatTamanaEditor's own link to
# gte_editor is PRIVATE, so linking GreatTamanaEditor alone propagates ZERO
# header search paths to a Project Assembly target. This function grabs
# ONLY the include-directory usage requirement of every dependency a
# Project Assembly's own #include chain can reach through gte_core/
# gte_editor's real headers - NEVER re-linking any of these targets' own
# compiled code (that would reintroduce the duplicate-globals hazard for
# that one dependency). FULL, verified dependency list as of PHASE3
# (re-verify against the real, current root CMakeLists.txt before trusting
# unchanged - dependencies can be added later): gte_core links
# volk/vma/stb_image/stb_image_write/KTX::ktx/httplib/nlohmann_json PUBLIC
# (line ~1176), gte_plugin_abi PUBLIC (line ~1186, its own header comment
# there states the exact same "any consumer of gte_core needs this
# propagated too" reasoning this function exists for - PluginHost.h, which
# Core.h transitively #includes, needs it), and Threads::Threads PUBLIC
# (line ~1199, included below purely for completeness/symmetry with that
# same reasoning - it is a link-only imported target with no real headers
# of its own, so this entry is a harmless no-op in practice, not a
# functional requirement). gte_core's ONE PRIVATE dependency, saba_pmx (plus
# its own glm dependency), is correctly NEVER listed here - PRIVATE never
# propagates, and PmxLoader.h's/VmdLoader.h's public APIs never leak a
# saba::/glm:: type, so no consumer of gte_core (including a Project
# Assembly) needs its headers. gte_editor links imgui/imguizmo PRIVATE
# (handled separately, PHASE7 only, _Editor targets only - see
# gte_project_assembly_apply_editor_header_paths() below) and SDL3::SDL3
# PUBLIC (line ~1114).
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
        $<TARGET_PROPERTY:gte_plugin_abi,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:Threads::Threads,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()

# editor-core-separation-11 campaign (Project Assembly system), PHASE4. Thin
# wrapper around the engine's OWN, already-generic gte_add_shader()
# (cmake/CompileShaders.cmake, include()-ed as "CompileShaders" somewhere in
# root CMakeLists.txt BEFORE gte_add_project()/this function is actually
# CALLED - a CMake function() body is only evaluated when CALLED, not when
# defined, so the relative include ORDER of this file vs. CompileShaders.cmake
# does not matter, only that CompileShaders.cmake has been include()-ed
# somewhere before the call happens - do not "fix" the include order
# thinking it matters) - globs every .vert/.frag/.comp under a project's own
# Assets/ folder and compiles each one exactly the way the engine's own
# internal shaders already are. No new shader-compilation mechanism is
# invented here.
function(gte_add_project_shaders TARGET ASSETS_DIR)
    file(GLOB_RECURSE PROJECT_SHADER_SOURCES CONFIGURE_DEPENDS
        "${ASSETS_DIR}/*.vert" "${ASSETS_DIR}/*.frag" "${ASSETS_DIR}/*.comp")

    foreach(SHADER_SOURCE ${PROJECT_SHADER_SOURCES})
        # gte_add_shader()'s own SOURCE parameter is documented/used as
        # relative to CMAKE_SOURCE_DIR (repo root) - PROJECT_SHADER_SOURCES
        # above is a list of ABSOLUTE paths (file(GLOB_RECURSE) always
        # returns absolute paths), so it must be re-expressed as relative to
        # CMAKE_SOURCE_DIR before being handed to gte_add_shader(), exactly
        # like every existing internal call site already is. Confirmed by
        # directly reading cmake/CompileShaders.cmake's own body: it does
        # set(SOURCE_ABSOLUTE "${CMAKE_SOURCE_DIR}/${SOURCE}") internally -
        # i.e. it ALWAYS prepends CMAKE_SOURCE_DIR, so passing an
        # already-absolute path here would silently produce a broken,
        # doubled path (e.g. "C:/repo/C:/repo/Projects/...") that fails
        # loudly (file not found) - do not skip this re-basing step.
        file(RELATIVE_PATH SHADER_SOURCE_RELATIVE "${CMAKE_SOURCE_DIR}" "${SHADER_SOURCE}")
        gte_add_shader(${TARGET} "${SHADER_SOURCE_RELATIVE}")
    endforeach()
endfunction()

# editor-core-separation-11 campaign (Project Assembly system), PHASE7
# (Finding A), Step 1 - pulled forward into PHASE3 per that phase's own file's
# explicit "implement PHASE7's Step A1 alongside this phase (both are tiny)"
# guidance, since gte_add_project()'s _Editor branch (above) already needs to
# call this the moment it exists, and it depends on nothing PHASE5/PHASE7's
# own runtime-loader/panel-registration work hasn't finished yet. ONLY for
# _Editor.dll targets - a _Game.dll never needs ImGui at all. Mirrors this
# file's own gte_project_assembly_apply_header_paths() exactly (same
# headers-only-via-generator-expression technique) - imgui/imguizmo are
# deliberately excluded from THAT function and added here instead, in their
# OWN, separate, _Editor-only function, so a _Game.dll's own configure never
# even resolves the `imgui`/`imguizmo` CMake target names at all. NOTE: this
# is ONLY PHASE7's Step 1 (the CMake header-propagation fix) - PHASE7's own
# Step 1a (widening ImGuiEditorLayer.cpp's plugin-panel-draw gate), Step 2
# (the real interactive panel), and Step 3 (live HTTP verification) are NOT
# done here and remain PHASE7's own responsibility.
function(gte_project_assembly_apply_editor_header_paths TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:imguizmo,INTERFACE_INCLUDE_DIRECTORIES>
    )
endfunction()
