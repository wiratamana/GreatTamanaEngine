# FetchSpirvReflect.cmake
#
# Vendors SPIRV-Reflect (https://github.com/KhronosGroup/SPIRV-Reflect) straight
# from its GitHub repo, the same "no submodule, no package manager, nothing
# pre-installed on the machine" philosophy as every other Fetch*.cmake module
# in this project - mirrors cmake/FetchJson.cmake's own two-file-via-raw-
# content-endpoint shape most closely (see that file first).
#
# SPIRV-Reflect is a small, purpose-built library (Decision D4,
# task_manager/better-render-pass-1/PHASE1_SPIRV_REFLECT_DEPENDENCY_AND_SHADER_REFLECTION_CORE.md)
# that extracts reflection metadata (descriptor bindings / push-constant
# ranges / declared compute work-group size) straight out of an
# already-compiled SPIR-V binary - used by the new
# src/Renderer/Vulkan/ShaderReflection.h/.cpp module.
#
# Unlike nlohmann/json (one header), SPIRV-Reflect needs THREE files to build
# cleanly, all fetched straight from GitHub's raw content endpoint (no ZIP
# download/extract step needed - every one of these is a plain single file at
# a known repo-relative path):
#   spirv_reflect.h                         - public header
#   spirv_reflect.c                         - the actual (plain C) implementation,
#                                              needs to be COMPILED, not just
#                                              staged (unlike header-only
#                                              nlohmann_json/httplib/vma)
#   include/spirv/unified1/spirv.h          - the official SPIR-V enum/opcode
#                                              header spirv_reflect.h itself
#                                              #includes via a relative path
#                                              (`#include "./include/spirv/unified1/spirv.h"`)
#                                              - easy to miss (the PHASE1 task
#                                              doc's own "exactly two files"
#                                              description undersells this -
#                                              confirmed by direct inspection
#                                              of the real vendored header),
#                                              but required for spirv_reflect.h
#                                              to even parse.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.h
#   ${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.c
#   ${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/include/spirv/unified1/spirv.h
#   ${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/LICENSE
#   ${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/.gte_fetched_ref - plain text
#       file recording exactly which resolved ref is currently staged (same
#       convention as every other Fetch*.cmake module).
#
# Defines one target:
#   spirv_reflect   - STATIC library compiling spirv_reflect.c, PUBLIC include
#                      directory third_party/spirv_reflect (so consumers
#                      `#include "spirv_reflect.h"`, a plain relative include -
#                      no extra subfolder trick needed, unlike nlohmann/json's
#                      own `nlohmann/` include-path convention). Mirrors
#                      cmake/FetchVulkan.cmake's own `volk` target shape (a
#                      small, two-file, plain-C dependency fetched directly
#                      from GitHub and compiled as a genuine STATIC library) -
#                      the closest existing precedent in this codebase, NOT
#                      vma/httplib (both header-only INTERFACE targets with
#                      nothing compiled at all).
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   SPIRV_REFLECT_RELEASE_TAG      - Git ref to fetch spirv_reflect.h/.c/spirv.h
#                                     from KhronosGroup/SPIRV-Reflect at: a
#                                     branch, tag, or commit SHA. Deliberately
#                                     NOT defaulted to "latest" (SPIRV-Reflect
#                                     does not publish GitHub Releases as
#                                     consistently as nlohmann/json - see the
#                                     PHASE1 task doc). Defaults to
#                                     "local-vendored-from-SPIRV-Reflect-main.zip"
#                                     - a marker (not a real git ref) recording
#                                     that the currently-staged copy was vendored
#                                     by hand from a locally-downloaded
#                                     SPIRV-Reflect-main.zip (offline setup -
#                                     no network access used), rather than
#                                     resolved/fetched through this module's own
#                                     _spirv_reflect_download_and_stage()
#                                     codepath. Override with a real git ref
#                                     (e.g. a pinned commit SHA) + set
#                                     SPIRV_REFLECT_FORCE_REDOWNLOAD ON to
#                                     deliberately move to a real, network-fetched
#                                     upstream copy later.
#   SPIRV_REFLECT_FORCE_REDOWNLOAD  - Set to ON to force re-fetching even if
#                                     already present and already matching
#                                     SPIRV_REFLECT_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSpirvReflect.cmake only supports Windows. Not supported on this platform.")
endif()

set(SPIRV_REFLECT_RELEASE_TAG "local-vendored-from-SPIRV-Reflect-main.zip" CACHE STRING
    "SPIRV-Reflect git ref to fetch spirv_reflect.h/.c/spirv.h from (a branch, tag, or commit SHA), or the special marker 'local-vendored-from-SPIRV-Reflect-main.zip' recording an offline, hand-vendored copy already staged on disk (see this file's header comment).")
option(SPIRV_REFLECT_FORCE_REDOWNLOAD
    "Force re-downloading/re-fetching SPIRV-Reflect even if it already appears to be present and matching SPIRV_REFLECT_RELEASE_TAG."
    OFF)

# _spirv_reflect_download_file(<url> <dest_file> <content_marker>)
#
# Downloads a single raw file from GitHub's raw content endpoint, sanity-
# checking its content against <content_marker> the same defensive way
# FetchJson.cmake's own _json_download_and_stage() guards against a disguised
# 404 page.
function(_spirv_reflect_download_file url dest_file content_marker)
    get_filename_component(_dest_dir "${dest_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dest_dir}")

    message(STATUS "SPIRV-Reflect: downloading ${url}")
    file(DOWNLOAD "${url}" "${dest_file}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${dest_file}")
        message(FATAL_ERROR "SPIRV-Reflect: failed to download ${url}: ${_dl_msg}")
    endif()

    file(READ "${dest_file}" _content LIMIT 256)
    string(FIND "${_content}" "${content_marker}" _found)
    if(_found EQUAL -1)
        file(REMOVE "${dest_file}")
        message(FATAL_ERROR "SPIRV-Reflect: downloaded content from ${url} does not look right (missing '${content_marker}') - is the resolved ref valid?")
    endif()
endfunction()

# _spirv_reflect_download_and_stage(<ref>)
#
# Downloads spirv_reflect.h / spirv_reflect.c / include/spirv/unified1/spirv.h
# for a concrete KhronosGroup/SPIRV-Reflect ref directly from GitHub's raw
# content endpoint - no archive to extract, every file fetched individually
# (mirrors FetchJson.cmake's own single-file raw-content approach, just
# repeated three times).
function(_spirv_reflect_download_and_stage ref)
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect")

    _spirv_reflect_download_file(
        "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Reflect/${ref}/spirv_reflect.h"
        "${_dest_dir}/spirv_reflect.h"
        "SPIRV-Reflect")

    _spirv_reflect_download_file(
        "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Reflect/${ref}/spirv_reflect.c"
        "${_dest_dir}/spirv_reflect.c"
        "spirv_reflect")

    _spirv_reflect_download_file(
        "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Reflect/${ref}/include/spirv/unified1/spirv.h"
        "${_dest_dir}/include/spirv/unified1/spirv.h"
        "SPIR-V")

    _spirv_reflect_download_file(
        "https://raw.githubusercontent.com/KhronosGroup/SPIRV-Reflect/${ref}/LICENSE"
        "${_dest_dir}/LICENSE"
        "Apache License")

    file(WRITE "${_dest_dir}/.gte_fetched_ref" "${ref}")

    message(STATUS "SPIRV-Reflect: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_spirv_reflect()
#
# Ensures SPIRV-Reflect's source (spirv_reflect.h/.c + the SPIR-V header they
# depend on) is present in this project - fetching it from GitHub ONLY if not
# already staged (a plain, offline, hand-vendored copy already satisfies this
# - see SPIRV_REFLECT_RELEASE_TAG's own doc comment above) - then defines the
# `spirv_reflect` STATIC library target described above.
function(fetch_spirv_reflect)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_spirv_reflect() only supports Windows. Not supported on this platform.")
    endif()

    set(_h_marker "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.h")
    set(_c_marker "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.c")
    set(_spv_marker "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/include/spirv/unified1/spirv.h")
    set(_ref_marker "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_h_marker}" AND EXISTS "${_c_marker}" AND EXISTS "${_spv_marker}" AND EXISTS "${_ref_marker}")
        file(READ "${_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL SPIRV_REFLECT_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT SPIRV_REFLECT_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "SPIRV-Reflect: already present and matching ref '${SPIRV_REFLECT_RELEASE_TAG}' - skipping download.")
    else()
        if(SPIRV_REFLECT_RELEASE_TAG STREQUAL "local-vendored-from-SPIRV-Reflect-main.zip")
            message(FATAL_ERROR "SPIRV-Reflect: staged files are missing/stale and SPIRV_REFLECT_RELEASE_TAG is still the offline-vendoring marker - set -DSPIRV_REFLECT_RELEASE_TAG=<a real git ref, e.g. a pinned commit SHA> to fetch a real copy from GitHub (this requires internet access).")
        endif()
        _spirv_reflect_download_and_stage("${SPIRV_REFLECT_RELEASE_TAG}")
    endif()

    if(NOT TARGET spirv_reflect)
        add_library(spirv_reflect STATIC
            "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.c"
            "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect/spirv_reflect.h"
        )
        target_include_directories(spirv_reflect PUBLIC
            "${CMAKE_SOURCE_DIR}/third_party/spirv_reflect"
        )
    endif()
endfunction()
