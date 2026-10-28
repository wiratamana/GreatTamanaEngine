# Compiles GLSL (src/Shaders, ...) to SPIR-V with glslc at build time. Only GLSL is committed.
# glslc is a build-time tool: it must be on PATH (Vulkan SDK or `scoop install shaderc`).

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc not found on PATH - required to compile GLSL shaders to SPIR-V. "
        "Install it (Vulkan SDK or 'scoop install shaderc') and reconfigure.")
endif()

# gte_add_shader(<target> <source relative to CMAKE_SOURCE_DIR> [EXTRA_DEPENDS <file>...] [GENERATED_DEPENDS <abs file>...])
# Compiles to ${CMAKE_BINARY_DIR}/shaders/<name>.spv and stages a copy in <exe dir>/shaders/.
# EXTRA_DEPENDS lists every #include'd source file (relative paths); GENERATED_DEPENDS lists #include'd files
# generated into GTE_SHADER_GENERATED_DIR (absolute paths). glslc re-runs when one changes.
set(GTE_SHADER_GENERATED_DIR "${CMAKE_BINARY_DIR}/shaders/gen")

function(gte_add_shader TARGET SOURCE)
    cmake_parse_arguments(GTE_ADD_SHADER "" "" "EXTRA_DEPENDS;GENERATED_DEPENDS" ${ARGN})

    set(SOURCE_ABSOLUTE "${CMAKE_SOURCE_DIR}/${SOURCE}")
    get_filename_component(SHADER_NAME "${SOURCE}" NAME)
    set(COMPILED "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    set(EXTRA_DEPENDS_ABSOLUTE "")
    foreach(EXTRA_DEP ${GTE_ADD_SHADER_EXTRA_DEPENDS})
        list(APPEND EXTRA_DEPENDS_ABSOLUTE "${CMAKE_SOURCE_DIR}/${EXTRA_DEP}")
    endforeach()
    list(APPEND EXTRA_DEPENDS_ABSOLUTE ${GTE_ADD_SHADER_GENERATED_DEPENDS})
    # Reflection reads binding names from OpName: never add -O or -g0 (they strip names).
    # A test pins this: reflected names of the fixtures must not be empty.
    add_custom_command(
        OUTPUT "${COMPILED}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLC_EXECUTABLE}" "${SOURCE_ABSOLUTE}" -I "${CMAKE_SOURCE_DIR}/src/Shaders"
                -I "${GTE_SHADER_GENERATED_DIR}" -o "${COMPILED}"
        DEPENDS "${SOURCE_ABSOLUTE}" ${EXTRA_DEPENDS_ABSOLUTE}
        COMMENT "Compiling shader ${SOURCE} -> ${COMPILED}"
        VERBATIM
    )

    set_source_files_properties("${SOURCE_ABSOLUTE}" PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${TARGET} PRIVATE "${SOURCE_ABSOLUTE}" "${COMPILED}")
    set_property(GLOBAL APPEND PROPERTY GTE_COMPILED_SHADERS "${SHADER_NAME}")

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${COMPILED}"
                "$<TARGET_FILE_DIR:${TARGET}>/shaders/${SHADER_NAME}.spv"
        COMMENT "Staging ${SHADER_NAME}.spv next to ${TARGET}"
    )
endfunction()

# gte_stage_shader(<target> <file name without .spv>)
# Stages a shader that another target already compiles (gte_add_shader) next to <target>'s exe.
function(gte_stage_shader TARGET SHADER_NAME)
    set(COMPILED "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    # The output is produced by another directory's custom command; mark it generated here so
    # configure works before it has been built.
    set_source_files_properties("${COMPILED}" PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE "${COMPILED}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${COMPILED}"
                "$<TARGET_FILE_DIR:${TARGET}>/shaders/${SHADER_NAME}.spv"
        COMMENT "Staging ${SHADER_NAME}.spv next to ${TARGET}"
    )
endfunction()

# gte_add_shaders_in_dir(<target> <dir relative to CMAKE_SOURCE_DIR>)
# Registers every *.vert/*.frag/*.comp directly inside <dir> via gte_add_shader().
# #include dependencies are found by scanning (current dir first, then src/Shaders), transitively.
function(gte_add_shaders_in_dir TARGET DIR)
    set(GTE_SHADERS_DIR_ABSOLUTE "${CMAKE_SOURCE_DIR}/${DIR}")

    file(GLOB GTE_SHADER_SOURCES_ABSOLUTE
         CONFIGURE_DEPENDS
         "${GTE_SHADERS_DIR_ABSOLUTE}/*.vert"
         "${GTE_SHADERS_DIR_ABSOLUTE}/*.frag"
         "${GTE_SHADERS_DIR_ABSOLUTE}/*.comp")

    foreach(GTE_SHADER_ABSOLUTE ${GTE_SHADER_SOURCES_ABSOLUTE})
        file(RELATIVE_PATH GTE_SHADER_RELATIVE "${CMAKE_SOURCE_DIR}" "${GTE_SHADER_ABSOLUTE}")

        set(GTE_INCLUDE_WORKLIST "${GTE_SHADER_ABSOLUTE}")
        set(GTE_INCLUDE_VISITED "")
        set(GTE_EXTRA_DEPENDS_ABSOLUTE "")

        while(GTE_INCLUDE_WORKLIST)
            list(GET GTE_INCLUDE_WORKLIST 0 GTE_INCLUDE_CURRENT)
            list(REMOVE_AT GTE_INCLUDE_WORKLIST 0)

            if(NOT GTE_INCLUDE_CURRENT IN_LIST GTE_INCLUDE_VISITED)
                list(APPEND GTE_INCLUDE_VISITED "${GTE_INCLUDE_CURRENT}")

                if(EXISTS "${GTE_INCLUDE_CURRENT}")
                    file(STRINGS "${GTE_INCLUDE_CURRENT}" GTE_INCLUDE_LINES
                         REGEX "^[ \t]*#include[ \t]+\"[^\"]+\"")
                    get_filename_component(GTE_INCLUDE_CURRENT_DIR "${GTE_INCLUDE_CURRENT}" DIRECTORY)

                    foreach(GTE_INCLUDE_LINE ${GTE_INCLUDE_LINES})
                        string(REGEX REPLACE "^[ \t]*#include[ \t]+\"([^\"]+)\".*" "\\1"
                               GTE_INCLUDE_NAME "${GTE_INCLUDE_LINE}")

                        set(GTE_INCLUDE_RESOLVED "")
                        if(EXISTS "${GTE_INCLUDE_CURRENT_DIR}/${GTE_INCLUDE_NAME}")
                            set(GTE_INCLUDE_RESOLVED "${GTE_INCLUDE_CURRENT_DIR}/${GTE_INCLUDE_NAME}")
                        elseif(EXISTS "${CMAKE_SOURCE_DIR}/src/Shaders/${GTE_INCLUDE_NAME}")
                            set(GTE_INCLUDE_RESOLVED "${CMAKE_SOURCE_DIR}/src/Shaders/${GTE_INCLUDE_NAME}")
                        endif()

                        if(GTE_INCLUDE_RESOLVED)
                            if(NOT GTE_INCLUDE_RESOLVED IN_LIST GTE_EXTRA_DEPENDS_ABSOLUTE)
                                list(APPEND GTE_EXTRA_DEPENDS_ABSOLUTE "${GTE_INCLUDE_RESOLVED}")
                            endif()
                            list(APPEND GTE_INCLUDE_WORKLIST "${GTE_INCLUDE_RESOLVED}")
                        endif()
                    endforeach()
                endif()
            endif()
        endwhile()

        set(GTE_EXTRA_DEPENDS_RELATIVE "")
        foreach(GTE_DEP_ABSOLUTE ${GTE_EXTRA_DEPENDS_ABSOLUTE})
            file(RELATIVE_PATH GTE_DEP_RELATIVE "${CMAKE_SOURCE_DIR}" "${GTE_DEP_ABSOLUTE}")
            list(APPEND GTE_EXTRA_DEPENDS_RELATIVE "${GTE_DEP_RELATIVE}")
        endforeach()

        if(GTE_EXTRA_DEPENDS_RELATIVE)
            gte_add_shader(${TARGET} "${GTE_SHADER_RELATIVE}" EXTRA_DEPENDS ${GTE_EXTRA_DEPENDS_RELATIVE})
        else()
            gte_add_shader(${TARGET} "${GTE_SHADER_RELATIVE}")
        endif()
    endforeach()
endfunction()
