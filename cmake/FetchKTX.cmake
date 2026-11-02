# KTX-Software, built from source in-tree (the Windows release is only an NSIS installer).
#
# fetch_ktx()   third_party/ktx (full source archive), built into build/_ktx_build
#               targets: ktx (STATIC), KTX::ktx (alias)
#
# Only libktx is built. Tools, tests, docs, bindings and the Vulkan/GL upload helpers are off,
# so the Vulkan SDK is not needed. Basis Universal and astc-encoder are vendored in the archive.
# KTX's own CMakeLists calls find_package(Bash REQUIRED), so a bash.exe is needed (Git for Windows).
#
# Cache: GTE_KTX_REF (tag).

include(GteFetch)

set(GTE_KTX_REF "v4.4.2" CACHE STRING "KhronosGroup/KTX-Software tag")

# Upstream bug in writer2.c (debug builds): `pos` is 64 bit but getpos() writes a 32 bit
# ktx_off_t on Windows, so the assert compares garbage high bits and fails on valid files.
function(_ktx_apply_patches writer_c)
    set(_note "// GreatTamanaEngine patch (cmake/FetchKTX.cmake)")
    gte_replace_in_file("${writer_c}"
"#if defined(DEBUG) || DUMP_IMAGE
        ktx_size_t pos;
#endif"
"#if defined(DEBUG) || DUMP_IMAGE
        ktx_off_t pos; ${_note}
#endif")
    gte_replace_in_file("${writer_c}"
"        result = dststr->getpos(dststr, (ktx_off_t*)&pos);
        // Could fail if stdout is a pipe
        if (result == KTX_SUCCESS)
            assert(pos == private->_levelIndex[level].byteOffset + baseOffset);"
"        result = dststr->getpos(dststr, &pos); ${_note}
        // Could fail if stdout is a pipe
        if (result == KTX_SUCCESS)
            assert((ktx_size_t)pos == private->_levelIndex[level].byteOffset + baseOffset);")
endfunction()

# Seeds BASH_EXECUTABLE for KTX's find_package(Bash). Its FindBash only checks
# "C:/Program Files/Git/bin" and PATH, which misses user and scoop installs of Git for Windows.
function(_ktx_find_bash)
    if(BASH_EXECUTABLE AND EXISTS "${BASH_EXECUTABLE}")
        return()
    endif()
    find_program(_gte_bash_exe bash PATHS
        "$ENV{ProgramFiles}/Git/bin"
        "$ENV{ProgramFiles\(x86\)}/Git/bin"
        "$ENV{LOCALAPPDATA}/Programs/Git/bin"
        "$ENV{USERPROFILE}/scoop/apps/git/current/bin")
    if(NOT _gte_bash_exe)
        message(FATAL_ERROR
            "fetch_ktx(): bash not found. KTX-Software needs bash.exe to configure. "
            "Install Git for Windows (it includes bash.exe) or pass -DBASH_EXECUTABLE=<path>.")
    endif()
    set(BASH_EXECUTABLE "${_gte_bash_exe}" CACHE FILEPATH "Bash, needed by KTX-Software's CMakeLists" FORCE)
    message(STATUS "KTX-Software: using bash at ${_gte_bash_exe}")
endfunction()

function(fetch_ktx)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/ktx")
    gte_dep_present(_present NAME KTX-Software REF "${GTE_KTX_REF}" DIR "${_dir}"
        REQUIRES "${_dir}/CMakeLists.txt" "${_dir}/include/ktx.h")
    if(NOT _present)
        gte_github_archive(KhronosGroup/KTX-Software "${GTE_KTX_REF}" _root)
        gte_require_files("${_root}" CMakeLists.txt include/ktx.h lib/writer2.c
            external/basisu/transcoder/basisu_transcoder.h external/astc-encoder/CMakeLists.txt)
        file(REMOVE_RECURSE "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        # The trailing slash copies the contents of _root, not the folder itself.
        file(COPY "${_root}/" DESTINATION "${_dir}")
        _ktx_apply_patches("${_dir}/lib/writer2.c")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_KTX_REF}")
    endif()

    if(NOT TARGET ktx)
        _ktx_find_bash()

        # Normal variables, visible only to this function and the KTX subdirectory.
        # KTX reads them through option(), which keeps a normal variable (CMP0077).
        foreach(_option TESTS TOOLS TOOLS_CTS DOC JNI PY LOADTEST_APPS VK_UPLOAD GL_UPLOAD)
            set(KTX_FEATURE_${_option} OFF)
        endforeach()
        set(BUILD_SHARED_LIBS OFF)

        add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_ktx_build" EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET KTX::ktx)
        add_library(KTX::ktx ALIAS ktx)
    endif()
endfunction()
