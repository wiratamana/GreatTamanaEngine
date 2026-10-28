# Turns the `inline constexpr` constants of a constants-only C++ header into a GLSL include, so shaders
# and C++ read one source of truth. Supported lines (one declaration per line, column 0):
#   inline constexpr std::uint32_t NAME = <decimal> | 1u << <bit>;
#   inline constexpr float NAME = <digits>.<digits>[e<exp>]f;
# Any other `inline constexpr` line is a configure error.

# gte_generate_glsl_constants(<header> <output .glsl>)
# Rewrites <output> only when its content changes; reconfigures when <header> changes.
function(gte_generate_glsl_constants HEADER OUTPUT)
    file(READ "${HEADER}" TEXT)
    # ';' would split the matches below as list separators.
    string(REPLACE ";" "@" TEXT "${TEXT}")

    string(REGEX MATCHALL "\ninline constexpr" STARTS "${TEXT}")
    string(REGEX MATCHALL "\ninline constexpr [^@/\n]*@" DECLS "${TEXT}")
    list(LENGTH STARTS START_COUNT)
    list(LENGTH DECLS DECL_COUNT)
    if(START_COUNT EQUAL 0 OR NOT START_COUNT EQUAL DECL_COUNT)
        message(FATAL_ERROR "${HEADER}: ${START_COUNT} 'inline constexpr' lines, ${DECL_COUNT} parseable. "
            "Use 'std::uint32_t NAME = N | 1u << N;' or 'float NAME = D.Df;' on one line without comments inside.")
    endif()

    get_filename_component(HEADER_NAME "${HEADER}" NAME)
    get_filename_component(GUARD_NAME "${OUTPUT}" NAME_WE)
    string(TOUPPER "${GUARD_NAME}" GUARD_NAME)
    set(CONTENT "// Generated from ${HEADER_NAME}. Do not edit.\n#ifndef GTE_${GUARD_NAME}_GLSL\n#define GTE_${GUARD_NAME}_GLSL\n\n")

    foreach(DECL IN LISTS DECLS)
        string(STRIP "${DECL}" DECL)
        if(DECL MATCHES "^inline constexpr std::uint32_t ([A-Za-z0-9_]+) = ([^@]+)@$")
            set(NAME "${CMAKE_MATCH_1}")
            string(STRIP "${CMAKE_MATCH_2}" EXPR_TEXT)
            if(EXPR_TEXT MATCHES "^1u << ([0-9]+)$")
                math(EXPR VALUE "1 << ${CMAKE_MATCH_1}")
            elseif(EXPR_TEXT MATCHES "^([0-9]+)u?$")
                set(VALUE "${CMAKE_MATCH_1}")
            else()
                message(FATAL_ERROR "${HEADER}: cannot parse the value of ${NAME}: '${EXPR_TEXT}'")
            endif()
            string(APPEND CONTENT "const uint ${NAME} = ${VALUE}u;\n")
        elseif(DECL MATCHES "^inline constexpr float ([A-Za-z0-9_]+) = ([0-9]+\\.[0-9]+([eE][-+]?[0-9]+)?)f@$")
            string(APPEND CONTENT "const float ${CMAKE_MATCH_1} = ${CMAKE_MATCH_2};\n")
        else()
            message(FATAL_ERROR "${HEADER}: cannot parse '${DECL}'")
        endif()
    endforeach()
    string(APPEND CONTENT "\n#endif\n")

    get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")
    # file(WRITE) turns '\n' into '\r\n' on Windows; the tests and shaders expect LF.
    file(CONFIGURE OUTPUT "${OUTPUT}.tmp" CONTENT "${CONTENT}" @ONLY NEWLINE_STYLE UNIX)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}"
        RESULT_VARIABLE COPY_RESULT)
    file(REMOVE "${OUTPUT}.tmp")
    if(NOT COPY_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot write ${OUTPUT}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HEADER}")
endfunction()
