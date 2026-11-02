# Compiles GLSL to SPIR-V at build time. Only GLSL is committed.
#
# The compiler is looked up on the first gte_add_shader(), never at include time:
#   1. glslc: GLSLC_EXECUTABLE, PATH, %VULKAN_SDK%/Bin, C:/VulkanSDK/*/Bin
#   2. glslang: PATH, the same SDK folders, build/_tools/glslang
#   3. download of GTE_GLSLANG_URL into build/_tools/glslang (GTE_FETCH_GLSLANG=ON)
#
#   gte_find_shader_compiler()
#   gte_add_shader(<target> <source rel. to CMAKE_SOURCE_DIR> [EXTRA_DEPENDS <rel file>...] [GENERATED_DEPENDS <abs file>...])
#   gte_add_shaders_in_dir(<target> <dir rel. to CMAKE_SOURCE_DIR>)
#   gte_stage_shader(<target> <shader file name without .spv>)
#
# Output: ${CMAKE_BINARY_DIR}/shaders/<name>.spv, copied to <exe dir>/shaders/ after each build.

include(GteFetch)

option(GTE_FETCH_GLSLANG "Download glslang when no shader compiler is found" ON)
set(GTE_GLSLANG_URL
    "https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip"
    CACHE STRING "glslang Windows release zip, used when no glslc is installed")
set(GTE_GLSLANG_SHA256 "82bf434e69b9bb4829de7e2b4bc2c5e7a7861e53d66cf75e5cc70f5f694a8d9b"
    CACHE STRING "SHA-256 of GTE_GLSLANG_URL (empty to skip the check)")
mark_as_advanced(GTE_GLSLANG_URL GTE_GLSLANG_SHA256)

set(GTE_SHADER_GENERATED_DIR "${CMAKE_BINARY_DIR}/shaders/gen")

function(_gte_fetch_glslang)
    set(_tools "${CMAKE_BINARY_DIR}/_tools")
    set(_hash "")
    if(GTE_GLSLANG_SHA256)
        string(TOLOWER "${GTE_GLSLANG_SHA256}" _lower)
        set(_hash SHA256 "${_lower}")
    endif()

    message(STATUS "No glslc found. Downloading glslang (or pass -DGLSLC_EXECUTABLE=<glslc.exe>).")
    gte_download("${GTE_GLSLANG_URL}" "${_tools}/glslang-download/glslang.zip" ${_hash})
    gte_extract_zip("${_tools}/glslang-download/glslang.zip" "${_tools}/glslang" _root)
    file(REMOVE_RECURSE "${_tools}/glslang-download")

    file(GLOB _exe "${_root}/bin/glslang*.exe")
    if(NOT _exe)
        message(FATAL_ERROR "No glslang*.exe in ${_root}/bin. Check GTE_GLSLANG_URL.")
    endif()
    list(GET _exe 0 _exe)
    set(GTE_GLSLANG_EXECUTABLE "${_exe}" CACHE FILEPATH "glslang compiler" FORCE)
endfunction()

# Finds (or downloads) a shader compiler. Sets cache GTE_SHADER_COMPILER and GTE_SHADER_COMPILER_KIND.
function(gte_find_shader_compiler)
    if(GTE_SHADER_COMPILER AND EXISTS "${GTE_SHADER_COMPILER}")
        return()
    endif()

    set(_sdk_dirs "")
    if(DEFINED ENV{VULKAN_SDK})
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" _sdk)
        list(APPEND _sdk_dirs "${_sdk}/Bin")
    endif()
    file(GLOB _installs "C:/VulkanSDK/*/Bin")
    list(SORT _installs ORDER DESCENDING)
    list(APPEND _sdk_dirs ${_installs})

    set(_compiler "")
    set(_kind "")
    find_program(GLSLC_EXECUTABLE NAMES glslc PATHS ${_sdk_dirs} DOC "glslc shader compiler")
    if(GLSLC_EXECUTABLE)
        set(_compiler "${GLSLC_EXECUTABLE}")
        set(_kind glslc)
    else()
        find_program(GTE_GLSLANG_EXECUTABLE NAMES glslang glslangValidator
            PATHS ${_sdk_dirs} "${CMAKE_BINARY_DIR}/_tools/glslang/bin" DOC "glslang compiler")
        if(NOT GTE_GLSLANG_EXECUTABLE AND GTE_FETCH_GLSLANG)
            _gte_fetch_glslang()
        endif()
        if(GTE_GLSLANG_EXECUTABLE)
            set(_compiler "${GTE_GLSLANG_EXECUTABLE}")
            set(_kind glslang)
        endif()
    endif()

    if(NOT _compiler)
        message(FATAL_ERROR
            "No GLSL compiler found. Do one of these, then reconfigure:\n"
            "  - install the Vulkan SDK (https://vulkan.lunarg.com)\n"
            "  - run: scoop install shaderc\n"
            "  - pass -DGLSLC_EXECUTABLE=<path to glslc.exe>\n"
            "  - pass -DGTE_FETCH_GLSLANG=ON to download glslang (needs internet)")
    endif()

    set(GTE_SHADER_COMPILER "${_compiler}" CACHE INTERNAL "Shader compiler executable")
    set(GTE_SHADER_COMPILER_KIND "${_kind}" CACHE INTERNAL "glslc or glslang")
    message(STATUS "Shader compiler (${_kind}): ${_compiler}")
endfunction()

# Never add -O or -g0: reflection reads binding names from OpName, and a test pins that.
function(_gte_shader_command OUT_VAR SOURCE OUTPUT)
    set(_engine_inc "${CMAKE_SOURCE_DIR}/src/Shaders")
    if(GTE_SHADER_COMPILER_KIND STREQUAL "glslc")
        set(_cmd "${GTE_SHADER_COMPILER}" "${SOURCE}" -I "${_engine_inc}" -I "${GTE_SHADER_GENERATED_DIR}"
            -o "${OUTPUT}")
    else()
        # glslang needs #include enabled explicitly; glslc turns it on by itself.
        set(_cmd "${GTE_SHADER_COMPILER}" -V --target-env vulkan1.0
            "-I${_engine_inc}" "-I${GTE_SHADER_GENERATED_DIR}"
            "-P#extension GL_GOOGLE_include_directive : enable" -o "${OUTPUT}" "${SOURCE}")
    endif()
    set(${OUT_VAR} "${_cmd}" PARENT_SCOPE)
endfunction()

function(_gte_stage_spv TARGET SHADER_NAME)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv"
                "$<TARGET_FILE_DIR:${TARGET}>/shaders/${SHADER_NAME}.spv"
        COMMENT "Staging ${SHADER_NAME}.spv next to ${TARGET}")
endfunction()

# EXTRA_DEPENDS: #include'd files, relative to CMAKE_SOURCE_DIR. GENERATED_DEPENDS: absolute paths.
function(gte_add_shader TARGET SOURCE)
    cmake_parse_arguments(A "" "" "EXTRA_DEPENDS;GENERATED_DEPENDS" ${ARGN})
    gte_find_shader_compiler()

    set(_source "${CMAKE_SOURCE_DIR}/${SOURCE}")
    get_filename_component(_name "${SOURCE}" NAME)
    set(_compiled "${CMAKE_BINARY_DIR}/shaders/${_name}.spv")

    set(_deps "")
    foreach(_dep ${A_EXTRA_DEPENDS})
        list(APPEND _deps "${CMAKE_SOURCE_DIR}/${_dep}")
    endforeach()
    list(APPEND _deps ${A_GENERATED_DEPENDS})

    _gte_shader_command(_compile "${_source}" "${_compiled}")
    add_custom_command(
        OUTPUT "${_compiled}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND ${_compile}
        DEPENDS "${_source}" ${_deps}
        COMMENT "Compiling shader ${SOURCE}"
        VERBATIM)

    set_source_files_properties("${_source}" PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${TARGET} PRIVATE "${_source}" "${_compiled}")
    set_property(GLOBAL APPEND PROPERTY GTE_COMPILED_SHADERS "${_name}")
    _gte_stage_spv(${TARGET} "${_name}")
endfunction()

# Stages a shader that another target already compiles with gte_add_shader().
function(gte_stage_shader TARGET SHADER_NAME)
    set(_compiled "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    # Produced by another directory's command, so mark it generated here.
    set_source_files_properties("${_compiled}" PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE "${_compiled}")
    _gte_stage_spv(${TARGET} "${SHADER_NAME}")
endfunction()

# Registers every *.vert/*.frag/*.comp directly in <dir>. #include files are found transitively in
# the including file's folder, src/Shaders and GTE_SHADER_GENERATED_DIR (generate those first).
function(gte_add_shaders_in_dir TARGET DIR)
    file(GLOB _sources CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/${DIR}/*.vert"
        "${CMAKE_SOURCE_DIR}/${DIR}/*.frag"
        "${CMAKE_SOURCE_DIR}/${DIR}/*.comp")

    foreach(_shader ${_sources})
        set(_todo "${_shader}")
        set(_seen "")
        set(_deps "")
        while(_todo)
            list(POP_FRONT _todo _current)
            if(_current IN_LIST _seen OR NOT EXISTS "${_current}")
                continue()
            endif()
            list(APPEND _seen "${_current}")
            get_filename_component(_current_dir "${_current}" DIRECTORY)
            file(STRINGS "${_current}" _lines REGEX "^[ \t]*#include[ \t]+\"[^\"]+\"")
            foreach(_line ${_lines})
                string(REGEX REPLACE "^[ \t]*#include[ \t]+\"([^\"]+)\".*" "\\1" _name "${_line}")
                foreach(_search "${_current_dir}" "${CMAKE_SOURCE_DIR}/src/Shaders" "${GTE_SHADER_GENERATED_DIR}")
                    if(EXISTS "${_search}/${_name}")
                        list(APPEND _deps "${_search}/${_name}")
                        list(APPEND _todo "${_search}/${_name}")
                        break()
                    endif()
                endforeach()
            endforeach()
        endwhile()
        list(REMOVE_DUPLICATES _deps)

        # Includes under the generated dir are outside the source tree: pass them as absolute.
        set(_relative "")
        set(_generated "")
        foreach(_dep ${_deps})
            string(FIND "${_dep}" "${GTE_SHADER_GENERATED_DIR}/" _pos)
            if(_pos EQUAL 0)
                list(APPEND _generated "${_dep}")
            else()
                file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}" "${_dep}")
                list(APPEND _relative "${_rel}")
            endif()
        endforeach()

        file(RELATIVE_PATH _shader_rel "${CMAKE_SOURCE_DIR}" "${_shader}")
        gte_add_shader(${TARGET} "${_shader_rel}" EXTRA_DEPENDS ${_relative} GENERATED_DEPENDS ${_generated})
    endforeach()
endfunction()
