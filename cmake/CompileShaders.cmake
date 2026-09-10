# Compiles GLSL shader sources (src/Shaders/*.vert, *.frag, ...) to SPIR-V at
# build time via glslc, so only human-editable GLSL is version-controlled -
# never the compiled binary (see .gitignore). This mirrors the project's
# "fetch/derive the binary artifact, commit only the source" convention used
# for SDL3/Vulkan/VMA/ImGui (see cmake/Fetch*.cmake), except glslc itself is
# a build-time-only developer tool (never shipped, never needed at runtime
# on the target machine) so it is NOT fetched automatically - it must
# already be on PATH.
#
# Get glslc from either:
#   - the Vulkan SDK (https://vulkan.lunarg.com/), or
#   - a standalone Shaderc install, e.g. `scoop install shaderc` on Windows.

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc not found on PATH - required to compile GLSL shaders "
        "(src/Shaders/*.vert/*.frag) to SPIR-V at build time. Install it, "
        "e.g. via 'scoop install shaderc' (or the Vulkan SDK), and make "
        "sure it is on PATH, then reconfigure.")
endif()

# gte_add_shader(<target> <source-file-relative-to-CMAKE_SOURCE_DIR> [EXTRA_DEPENDS <dep1> <dep2> ...])
#
# Compiles <source-file> to SPIR-V in two steps:
#
#   1. An OUTPUT-based add_custom_command compiles it into a fixed location
#      under the build tree ("${CMAKE_BINARY_DIR}/shaders/<name>.spv") that
#      does NOT depend on any generator expression tied to <target> itself -
#      this is what gives proper incremental-rebuild dependency tracking
#      (re-runs glslc only when the GLSL source actually changed), the same
#      way any other compiled source is tracked.
#   2. A POST_BUILD add_custom_command on <target> then copies that compiled
#      .spv next to the actual built .exe (into "shaders/" alongside it) -
#      exactly the same pattern FetchSDL3.cmake's sdl3_copy_runtime_dll()
#      already uses to stage SDL3.dll there. This split is required because
#      "$<TARGET_FILE_DIR:target>" cannot be evaluated in an OUTPUT-based
#      custom command for that SAME target (CMake needs to know the
#      target's final link output to resolve it, which isn't settled yet
#      while sources are still being added) - only the POST_BUILD form
#      (attached once the target is otherwise fully defined) can use it.
#
# The GLSL source itself is also added to <target>'s sources (marked
# HEADER_FILE_ONLY so the compiler never tries to build it directly) purely
# so it shows up in an IDE's project tree next to the C++ that uses it.
#
# --- Shared-GLSL #include support (Atmosphere Phase 1 - see
# task_manager/atmosphere-scattering-1/
# ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md,
# Step 3.2) ---------------------------------------------------------------
#
# glslc's built-in file includer resolves a `#include "Foo.glsl"` directive
# relative to the INCLUDING file's own directory with zero extra flags
# needed - verified directly against this project's own glslc via a
# throwaway probe shader (since deleted) before this comment was written, so
# every existing single-file gte_add_shader() call site above needed no
# change at all for #include itself to already work.
#
# The one genuine gap `#include` introduces is INCREMENTAL REBUILD tracking:
# the OUTPUT-based add_custom_command above only ever lists SOURCE in its own
# DEPENDS, so CMake has no way to know a `.comp`/`.frag` needs recompiling
# just because a shared header it #includes changed - it would keep serving
# a stale, already-compiled `.spv`. The optional trailing `EXTRA_DEPENDS`
# keyword-argument list closes exactly that gap: pass every shared header a
# given shader source #includes (e.g. `src/Shaders/AtmosphereCommon.glsl`)
# and this function adds each one to the SAME add_custom_command's own
# DEPENDS list, so glslc re-runs whenever ANY of them change, not just the
# shader's own top-level source file. Every existing call site that doesn't
# pass EXTRA_DEPENDS is completely unaffected - this parameter is purely
# additive.
function(gte_add_shader TARGET SOURCE)
    cmake_parse_arguments(GTE_ADD_SHADER "" "" "EXTRA_DEPENDS" ${ARGN})

    set(SOURCE_ABSOLUTE "${CMAKE_SOURCE_DIR}/${SOURCE}")
    get_filename_component(SHADER_NAME "${SOURCE}" NAME)
    set(COMPILED "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    set(EXTRA_DEPENDS_ABSOLUTE "")
    foreach(EXTRA_DEP ${GTE_ADD_SHADER_EXTRA_DEPENDS})
        list(APPEND EXTRA_DEPENDS_ABSOLUTE "${CMAKE_SOURCE_DIR}/${EXTRA_DEP}")
    endforeach()

    add_custom_command(
        OUTPUT "${COMPILED}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLC_EXECUTABLE}" "${SOURCE_ABSOLUTE}" -o "${COMPILED}"
        DEPENDS "${SOURCE_ABSOLUTE}" ${EXTRA_DEPENDS_ABSOLUTE}
        COMMENT "Compiling shader ${SOURCE} -> ${COMPILED}"
        VERBATIM
    )

    set_source_files_properties("${SOURCE_ABSOLUTE}" PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${TARGET} PRIVATE "${SOURCE_ABSOLUTE}" "${COMPILED}")

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${COMPILED}"
                "$<TARGET_FILE_DIR:${TARGET}>/shaders/${SHADER_NAME}.spv"
        COMMENT "Staging ${SHADER_NAME}.spv next to ${TARGET}"
    )
endfunction()
