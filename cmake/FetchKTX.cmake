# FetchKTX.cmake
#
# Builds KTX-Software (https://github.com/KhronosGroup/KTX-Software) IN-TREE
# from source, the same "no submodule, no package manager, nothing
# pre-installed on the machine" philosophy as every other Fetch*.cmake module
# in this project - and the same in-tree-source-build shape as
# FetchGTest.cmake (download a tag's plain GitHub codeload zip, extract, then
# add_subdirectory() it straight into this project's own build).
#
# Why a from-source build instead of a prebuilt binary (unlike FetchSDL3.cmake):
# KTX-Software's GitHub Releases page publishes Windows binaries ONLY as an
# NSIS .exe installer (KTX-Software-<ver>-Windows-x64.exe) - no plain .zip SDK
# at all, unlike SDL3 - so there is no non-interactive, no-installer-execution
# way to fetch an official prebuilt Windows binary. Building from source is
# the only option consistent with "must not run an installer".
#
# Why a plain codeload zip is enough (unlike what an earlier iteration of
# this file assumed): KTX-Software's CMake build pulls in Basis Universal
# (external/basisu) and astc-encoder (external/astc-encoder), but - verified
# directly against the upstream repo - both are vendored straight into the
# main tree (most likely via `git subrepo`, per KTX-Software's own release
# notes), NOT real git submodules; the only actual git submodule declared in
# upstream's .gitmodules is tests/cts (KTX-Software's own CTS conformance
# test suite), which this project never enables (KTX_FEATURE_TOOLS_CTS stays
# OFF below). So a plain GitHub codeload zip - the same download mechanism
# every sibling Fetch*.cmake module already uses - already contains
# everything needed for a real Basis Universal + ASTC capable libktx, with no
# need for an actual `git clone`/submodule step.
#
# Only the `ktx` library target itself is built - KTX-Software's own CLI
# tools/unit test suite/loadtest apps/docs/language bindings are all switched
# off (KTX_FEATURE_TOOLS/_TESTS/_DOC/_JNI/_PY/_LOADTEST_APPS), and so is its
# own opinionated Vulkan/OpenGL texture-upload helper code
# (KTX_FEATURE_VK_UPLOAD/_GL_UPLOAD) - this project reads KTX2 files itself
# and feeds the decoded/transcoded pixel bytes into its OWN existing Vulkan
# upload path (GpuResourceFactory/Buffer/RenderTexture), so none of that
# extra surface (or its extra dependencies - fmt, cxxopts, Doxygen, JNI, ...)
# is needed here.
#
# One extra wrinkle unique to this dependency: KTX-Software's own root
# CMakeLists.txt unconditionally calls find_package(Bash REQUIRED) (needed by
# its own build scripting, nothing this project itself needs a shell for).
# Its bundled cmake/modules/FindBash.cmake only looks in
# "C:\Program Files\Git\bin" and then the system PATH, which misses a
# portable/scoop-style Git for Windows install (bash.exe sits right next to
# git.exe on disk but such installs deliberately do not put bash.exe itself
# on PATH, unlike git.exe) - see _ktx_find_bash() below, which pre-seeds
# BASH_EXECUTABLE from a few more install layouts before KTX-Software's own
# find_package(Bash REQUIRED) runs (find_program() is a no-op once the cache
# variable it searches for is already set).
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/ktx/   - KTX-Software's full source tree
#
# Built into (gitignored build tree, never part of this repo):
#
#   ${CMAKE_BINARY_DIR}/_ktx_build/
#
# Defines:
#   ktx         - KTX-Software's own STATIC library target (BUILD_SHARED_LIBS
#                 forced OFF below, so no ktx.dll needs to be staged/copied
#                 next to the built .exe, unlike SDL3).
#   KTX::ktx    - ALIAS for the above, so consumers can
#                 target_link_libraries(... KTX::ktx) using the same
#                 namespaced name a real installed find_package(ktx) would
#                 provide.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   KTX_RELEASE_TAG      - KTX-Software git tag to build from, e.g. "v4.4.2".
#                           Defaults to "v4.4.2" - deliberately a concrete,
#                           known-good tag rather than "latest"/a moving
#                           branch, since this is a real from-source build
#                           with a much larger blast radius than this
#                           project's other, prebuilt-binary fetches if a
#                           future upstream release ever breaks something.
#   KTX_FORCE_REDOWNLOAD  - Set to ON to force re-downloading/re-extracting
#                           KTX-Software's source even if it already appears
#                           to be present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchKTX.cmake only supports Windows. Not supported on this platform.")
endif()

set(KTX_RELEASE_TAG "v4.4.2" CACHE STRING
    "KTX-Software git tag to build from (e.g. v4.4.2).")
option(KTX_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting KTX-Software's source even if it already appears to be present."
    OFF)

# _ktx_download_and_extract_tag(<tag_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete KTX-Software tag
# (no GitHub API call involved) and extracts it, returning the single
# top-level folder GitHub always wraps archive contents in. Same shape as
# FetchGTest.cmake's _gtest_download_and_extract_tag.
function(_ktx_download_and_extract_tag tag_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_ktx_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip_path "${_work_dir}/ktx-software-${tag_name}.zip")
    set(_zip_url "https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/${tag_name}.zip")

    message(STATUS "KTX-Software: downloading ${_zip_url} (full source tree - this can take a little while, it includes the vendored Basis Universal + astc-encoder sources)")
    file(DOWNLOAD "${_zip_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "KTX-Software: failed to download ${_zip_url}: ${_dl_msg}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "KTX-Software: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

function(_ktx_download_and_stage)
    _ktx_download_and_extract_tag("${KTX_RELEASE_TAG}" _root)

    if(NOT EXISTS "${_root}/CMakeLists.txt"
       OR NOT EXISTS "${_root}/include/ktx.h"
       OR NOT EXISTS "${_root}/external/basisu/transcoder/basisu_transcoder.h"
       OR NOT EXISTS "${_root}/external/astc-encoder/CMakeLists.txt")
        message(FATAL_ERROR "KTX-Software: extracted package doesn't look like a complete KTX-Software source tree (missing CMakeLists.txt / include/ktx.h / vendored Basis Universal / vendored astc-encoder).")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/ktx")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/ktx")
    # Trailing slash on the source path is deliberate: it copies _root's
    # CONTENTS into third_party/ktx (flattening away the tag-versioned
    # top-level folder name GitHub's archive wraps everything in, e.g.
    # "KTX-Software-4.4.2"), rather than nesting one level deeper. Do not
    # remove it.
    file(COPY "${_root}/" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/ktx")

    _ktx_apply_gte_patches("${CMAKE_SOURCE_DIR}/third_party/ktx/lib")

    message(STATUS "KTX-Software: staged -> ${CMAKE_SOURCE_DIR}/third_party/ktx")
endfunction()

# _ktx_apply_gte_patches(<staged_lib_dir>)
#
# Patches a freshly-staged lib/writer2.c to fix a genuine upstream bug found
# while integrating the PNG/JPEG -> KTX2 import pipeline: inside
# ktxTexture2_WriteToStream()'s DEBUG-only sanity check, `pos` is declared as
# a 64-bit ktx_size_t but is then passed to dststr->getpos() cast to
# (ktx_off_t*) - and ktx_off_t is only 32 bits wide on Windows. getpos() only
# ever writes the low 4 bytes of `pos`; the high 4 bytes are left as
# whatever garbage happened to be on the stack, so the subsequent
# `assert(pos == ...)` compares a real value against one containing random
# high bits - producing a FALSE assertion failure on an otherwise perfectly
# valid KTX2 file. This has nothing to do with actual data corruption; the
# written file is fine, only this debug-only comparison is broken.
#
# Applied here (rather than hand-editing third_party/ktx/lib/writer2.c
# directly) so the fix survives KTX_FORCE_REDOWNLOAD/a clean checkout/CI
# re-fetching a pristine copy from GitHub - third_party/ is gitignored,
# nothing under it is ever committed (see this file's own header comment).
# Each expected snippet is searched for BEFORE being replaced, and a
# message(WARNING ...) is raised (never a silent no-op) if it's missing -
# e.g. KTX_RELEASE_TAG is bumped to a future version where upstream
# rewrites or already fixed this differently - so a stale/ineffective patch
# never ships unnoticed.
function(_ktx_apply_gte_patches staged_lib_dir)
    set(_writer2_path "${staged_lib_dir}/writer2.c")
    file(READ "${_writer2_path}" _src)

    set(_decl_from "#if defined(DEBUG) || DUMP_IMAGE
        ktx_size_t pos;
#endif")
    set(_decl_to "#if defined(DEBUG) || DUMP_IMAGE
        ktx_off_t pos; // GreatTamanaEngine patch - see cmake/FetchKTX.cmake's _ktx_apply_gte_patches()
#endif")

    set(_check_from "        result = dststr->getpos(dststr, (ktx_off_t*)&pos);
        // Could fail if stdout is a pipe
        if (result == KTX_SUCCESS)
            assert(pos == private->_levelIndex[level].byteOffset + baseOffset);
        else
            assert(result == KTX_FILE_ISPIPE);")
    set(_check_to "        result = dststr->getpos(dststr, &pos); // GreatTamanaEngine patch - see cmake/FetchKTX.cmake's _ktx_apply_gte_patches()
        // Could fail if stdout is a pipe
        if (result == KTX_SUCCESS)
            assert((ktx_size_t)pos == private->_levelIndex[level].byteOffset + baseOffset);
        else
            assert(result == KTX_FILE_ISPIPE);")

    string(FIND "${_src}" "${_decl_from}" _decl_pos)
    if(_decl_pos EQUAL -1)
        message(WARNING "ktx: expected `ktx_size_t pos;` declaration text was not found in writer2.c - the pos/ktx_off_t type-mismatch patch was NOT applied. KTX-Software's source may have changed upstream; re-check cmake/FetchKTX.cmake's _ktx_apply_gte_patches().")
    else()
        string(REPLACE "${_decl_from}" "${_decl_to}" _src "${_src}")
    endif()

    string(FIND "${_src}" "${_check_from}" _check_pos)
    if(_check_pos EQUAL -1)
        message(WARNING "ktx: expected getpos()/assert() text was not found in writer2.c - the pos/ktx_off_t type-mismatch patch was NOT applied. KTX-Software's source may have changed upstream; re-check cmake/FetchKTX.cmake's _ktx_apply_gte_patches().")
    else()
        string(REPLACE "${_check_from}" "${_check_to}" _src "${_src}")
    endif()

    file(WRITE "${_writer2_path}" "${_src}")
endfunction()

# _ktx_find_bash()
#
# See the file-level comment above for why this is needed at all. Does
# nothing if BASH_EXECUTABLE is already a valid, existing path (either
# pre-set by the caller, e.g. -DBASH_EXECUTABLE=..., or left over from a
# previous successful configure).
function(_ktx_find_bash)
    if(BASH_EXECUTABLE AND EXISTS "${BASH_EXECUTABLE}")
        return()
    endif()

    find_program(_gte_bash_exe bash
        PATHS
            "$ENV{ProgramFiles}/Git/bin"
            "$ENV{ProgramFiles\(x86\)}/Git/bin"
            "$ENV{LOCALAPPDATA}/Programs/Git/bin"
            "$ENV{USERPROFILE}/scoop/apps/git/current/bin"
    )
    if(_gte_bash_exe)
        set(BASH_EXECUTABLE "${_gte_bash_exe}" CACHE FILEPATH
            "Bash interpreter - needed only because KTX-Software's own CMakeLists.txt calls find_package(Bash REQUIRED)."
            FORCE)
        message(STATUS "KTX-Software: found bash at ${_gte_bash_exe} (needed by KTX-Software's own CMakeLists.txt, not by this project)")
    else()
        message(FATAL_ERROR
            "fetch_ktx(): could not find a 'bash' executable anywhere (checked common "
            "Git for Windows install locations plus PATH). KTX-Software's own "
            "CMakeLists.txt requires Bash (find_package(Bash REQUIRED)) purely for its "
            "own build scripting, even though this project never invokes a shell "
            "script itself. Installing Git for Windows (https://git-scm.com/download/win"
            ", which bundles its own bash.exe) is the easiest fix, or pass "
            "-DBASH_EXECUTABLE=<path-to-bash.exe> explicitly.")
    endif()
endfunction()

# fetch_ktx()
#
# Ensures KTX-Software's source is present in this project (downloading it
# from GitHub if needed), then add_subdirectory()s it in-tree and defines the
# KTX::ktx ALIAS target described above.
function(fetch_ktx)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_ktx() only supports Windows. Not supported on this platform.")
    endif()

    set(_ktx_marker "${CMAKE_SOURCE_DIR}/third_party/ktx/CMakeLists.txt")
    if(NOT KTX_FORCE_REDOWNLOAD AND EXISTS "${_ktx_marker}")
        message(STATUS "KTX-Software: already present (third_party/ktx/CMakeLists.txt found) - skipping download.")
    else()
        _ktx_download_and_stage()
    endif()

    if(NOT TARGET ktx)
        _ktx_find_bash()

        # Only the library itself is needed - see the file-level comment
        # above for why every one of these is switched off.
        set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_TOOLS_CTS OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_JNI OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_PY OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_LOADTEST_APPS OFF CACHE STRING "" FORCE)
        set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
        set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)

        # Build libktx STATIC - see the target-name comment block above.
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        add_subdirectory(
            "${CMAKE_SOURCE_DIR}/third_party/ktx"
            "${CMAKE_BINARY_DIR}/_ktx_build"
            EXCLUDE_FROM_ALL
        )
    endif()

    if(NOT TARGET KTX::ktx)
        add_library(KTX::ktx ALIAS ktx)
    endif()
endfunction()
