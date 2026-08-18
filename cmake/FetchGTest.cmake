# FetchGTest.cmake
#
# Downloads GoogleTest (https://github.com/google/googletest) straight from
# its GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake/
# FetchImGui.cmake/FetchVMA.cmake fetch their dependencies - no submodule, no
# package manager, nothing pre-installed on the machine. Only fetched/built
# at all when GTE_BUILD_TESTS is ON (see CMakeLists.txt) - a normal engine
# build never touches this file's network call or compiles a single
# GoogleTest source file.
#
# Unlike VMA (FetchVMA.cmake - a single header staged and never compiled by
# this project), GoogleTest is real C++ that must be BUILT: this module
# stages GoogleTest's full source tree and then add_subdirectory()s it
# in-tree, exactly as GoogleTest's own documentation recommends, so it is
# compiled with this project's exact compiler/flags/CRT (see
# gtest_force_shared_crt below) instead of risking an ABI mismatch against a
# separately-built prebuilt binary.
#
# Downloading works by tag, straight from GitHub's codeload archive URL
# (https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip), same
# approach as FetchVulkan.cmake/FetchImGui.cmake/FetchVMA.cmake. GoogleTest
# publishes proper GitHub Releases, so "latest" resolves via the releases
# API.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/googletest/
#
# Defines (guarded against being called/defined twice):
#   gtest, gtest_main, gmock, gmock_main   - GoogleTest's own targets, built
#                                             as part of this project.
#   GTest::gtest, GTest::gtest_main,
#   GTest::gmock, GTest::gmock_main        - ALIAS targets for the above, so
#                                             consumers (tests/CMakeLists.txt)
#                                             can target_link_libraries()
#                                             against the same namespaced
#                                             names a system find_package(GTest)
#                                             would provide, without actually
#                                             depending on GTest being
#                                             installed anywhere.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   GTEST_RELEASE_TAG      - Git tag to fetch from google/googletest, e.g.
#                             "v1.15.2". Defaults to "latest".
#   GTEST_FORCE_REDOWNLOAD  - Set to ON to force re-fetching even if already
#                             present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchGTest.cmake only supports Windows. Not supported on this platform.")
endif()

set(GTEST_RELEASE_TAG "latest" CACHE STRING
    "GoogleTest git tag to fetch (e.g. v1.15.2), or 'latest'.")
option(GTEST_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting GoogleTest even if it already appears to be present."
    OFF)

# _gtest_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchVMA.cmake's _vma_github_get_json, duplicated locally so
# this module has no include-order dependency on either having been included
# first.
function(_gtest_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_gtest_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_out_file "${_work_dir}/${label}.json")

    file(DOWNLOAD "${url}" "${_out_file}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        HTTPHEADER "Accept: application/vnd.github+json"
        STATUS _dl_status
        TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        set(${out_json} "" PARENT_SCOPE)
        return()
    endif()

    file(READ "${_out_file}" _json)
    set(${out_json} "${_json}" PARENT_SCOPE)
endfunction()

# _gtest_resolve_tag(<tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name via the GitHub releases API. If
# <tag> isn't "latest", this is a no-op (no network call at all).
function(_gtest_resolve_tag tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "GoogleTest: resolving 'latest' via releases API")
    _gtest_github_get_json("gtest_release_latest"
        "https://api.github.com/repos/google/googletest/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "GoogleTest: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _gtest_download_and_extract_tag(<tag_name> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete google/googletest
# tag (no GitHub API call involved) and extracts it, returning the single
# top-level folder GitHub always wraps archive contents in.
function(_gtest_download_and_extract_tag tag_name out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_gtest_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip_path "${_work_dir}/googletest-${tag_name}.zip")
    set(_zip_url "https://github.com/google/googletest/archive/refs/tags/${tag_name}.zip")

    message(STATUS "GoogleTest: downloading ${_zip_url}")
    file(DOWNLOAD "${_zip_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "GoogleTest: failed to download ${_zip_url}: ${_dl_msg}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "GoogleTest: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

function(_gtest_download_and_stage)
    _gtest_resolve_tag("${GTEST_RELEASE_TAG}" _resolved_tag)
    message(STATUS "GoogleTest: resolved tag '${_resolved_tag}'")

    _gtest_download_and_extract_tag("${_resolved_tag}" _root)

    if(NOT EXISTS "${_root}/CMakeLists.txt" OR NOT EXISTS "${_root}/googletest/CMakeLists.txt")
        message(FATAL_ERROR "GoogleTest: extracted package doesn't look like a GoogleTest source tree (missing CMakeLists.txt).")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/third_party/googletest")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/googletest")
    # Trailing slash on the source path is deliberate: it copies _root's
    # CONTENTS into third_party/googletest (flattening away the
    # tag-versioned top-level folder name GitHub's archive wraps everything
    # in, e.g. "googletest-1.15.2"), rather than nesting one level deeper as
    # "third_party/googletest/googletest-1.15.2/...". Do not remove it.
    file(COPY "${_root}/" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/googletest")

    message(STATUS "GoogleTest: staged -> ${CMAKE_SOURCE_DIR}/third_party/googletest")
endfunction()

# fetch_gtest()
#
# Ensures GoogleTest's source is present in this project (downloading it
# from GitHub if needed), then add_subdirectory()s it in-tree and defines
# the GTest::* ALIAS targets described above. Must be called before
# tests/CMakeLists.txt links against GTest::gtest / GTest::gtest_main.
function(fetch_gtest)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_gtest() only supports Windows. Not supported on this platform.")
    endif()

    set(_gtest_marker "${CMAKE_SOURCE_DIR}/third_party/googletest/CMakeLists.txt")
    if(NOT GTEST_FORCE_REDOWNLOAD AND EXISTS "${_gtest_marker}")
        message(STATUS "GoogleTest: already present (third_party/googletest/CMakeLists.txt found) - skipping download.")
    else()
        _gtest_download_and_stage()
    endif()

    if(NOT TARGET gtest)
        # GoogleTest's own CMakeLists reads these three cache variables, so
        # they must be forced BEFORE add_subdirectory() below runs:
        #   - gtest_force_shared_crt: on MSVC, GoogleTest defaults to linking
        #     its own /MT(d) static CRT, which will not link against a
        #     project (like this one) built with the default dynamic /MD(d)
        #     CRT - forcing this ON makes GoogleTest match whatever CRT this
        #     project already uses, instead of a confusing LNK2038
        #     "mismatched _ITERATOR_DEBUG_LEVEL" / CRT-mismatch link error.
        #   - BUILD_GMOCK: ON so GTest::gmock is available too, for any
        #     future test that wants to mock an interface (e.g. IEditorLayer).
        #   - INSTALL_GTEST: OFF - this project never installs anything, and
        #     GoogleTest's own install rules aren't needed here.
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        add_subdirectory(
            "${CMAKE_SOURCE_DIR}/third_party/googletest"
            "${CMAKE_BINARY_DIR}/_gtest_build"
            EXCLUDE_FROM_ALL
        )
    endif()

    if(NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    if(TARGET gmock AND NOT TARGET GTest::gmock)
        add_library(GTest::gmock ALIAS gmock)
        add_library(GTest::gmock_main ALIAS gmock_main)
    endif()
endfunction()
