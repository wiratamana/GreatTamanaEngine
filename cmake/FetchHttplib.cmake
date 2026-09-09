# FetchHttplib.cmake
#
# Downloads cpp-httplib (https://github.com/yhirose/cpp-httplib) straight from
# its GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake/
# FetchImGui.cmake/FetchVMA.cmake/FetchSTB.cmake fetch their dependencies - no
# submodule, no package manager, nothing pre-installed on the machine.
#
# cpp-httplib is a single-header (httplib.h), header-only C++ HTTP/HTTPS
# library. It is being brought in purely as a debugging/tooling aid (e.g. a
# lightweight in-process HTTP endpoint for inspecting engine state from a
# browser/external tool) - this module only fetches and stages the header and
# exposes a target; it is deliberately NOT included/used by any engine source
# file yet. Being a plain, self-contained header (no separate *_IMPLEMENTATION
# macro needed, unlike stb_image.h/vk_mem_alloc.h - httplib.h's functions are
# all inline/template), whichever translation unit first needs it can simply
# `#include <httplib.h>` directly.
#
# Downloading works the same way FetchSTB.cmake fetches stb_image.h: directly
# from GitHub's raw content endpoint
# (https://raw.githubusercontent.com/yhirose/cpp-httplib/<ref>/httplib.h) - no
# ZIP download/extract step needed, since this is a single file. Unlike STB,
# cpp-httplib does publish proper GitHub Releases/tags, so HTTPLIB_RELEASE_TAG
# supports "latest" (resolved via the releases API, same convention as
# FetchVMA.cmake's VMA_RELEASE_TAG) in addition to a pinned tag/commit SHA.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/httplib/httplib.h
#   ${CMAKE_SOURCE_DIR}/third_party/httplib/.gte_fetched_ref - plain text file
#       recording exactly which resolved ref is currently staged (mirrors
#       FetchSTB.cmake's own marker file).
#
# Defines one target:
#   httplib   - INTERFACE library exposing third_party/httplib as an include
#               directory. Engine code should `#include <httplib.h>`. On
#               Windows, cpp-httplib's socket layer needs Winsock
#               (ws2_32) and, for its certificate-store lookups even without
#               OpenSSL compiled in, crypt32 - both are standard system
#               import libraries always present with any Windows
#               toolchain/SDK, so they are linked here unconditionally rather
#               than requiring every consumer to remember to add them.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   HTTPLIB_RELEASE_TAG      - Git tag/commit SHA to fetch httplib.h from
#                               yhirose/cpp-httplib at, e.g. "v0.18.5".
#                               Defaults to "latest" (resolved via the GitHub
#                               releases API, same as VMA_RELEASE_TAG's
#                               default in FetchVMA.cmake).
#   HTTPLIB_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if already
#                               present and already matching
#                               HTTPLIB_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchHttplib.cmake only supports Windows. Not supported on this platform.")
endif()

set(HTTPLIB_RELEASE_TAG "latest" CACHE STRING
    "cpp-httplib git ref to fetch httplib.h from (e.g. a tag like 'v0.18.5', a commit SHA, or 'latest').")
option(HTTPLIB_FORCE_REDOWNLOAD
    "Force re-downloading httplib.h even if it already appears to be present and matching HTTPLIB_RELEASE_TAG."
    OFF)

# _httplib_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchVulkan.cmake's _github_get_json / FetchVMA.cmake's
# _vma_github_get_json, duplicated locally so this module has no
# include-order dependency on either having been included first.
function(_httplib_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_httplib_fetch")
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

# _httplib_resolve_tag(<tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name via the GitHub releases API. If
# <tag> isn't "latest", this is a no-op (no network call at all) - same
# convention as FetchVMA.cmake's _vma_resolve_tag.
function(_httplib_resolve_tag tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "cpp-httplib: resolving 'latest' via releases API")
    _httplib_github_get_json("httplib_release_latest"
        "https://api.github.com/repos/yhirose/cpp-httplib/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "cpp-httplib: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _httplib_download_and_stage(<ref>)
#
# Downloads GitHub's raw httplib.h content for a concrete yhirose/cpp-httplib
# ref (tag or commit SHA) directly - no archive to extract, since this is a
# single-file library (mirrors FetchSTB.cmake's _stb_download_and_stage).
function(_httplib_download_and_stage ref)
    set(_url "https://raw.githubusercontent.com/yhirose/cpp-httplib/${ref}/httplib.h")
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/httplib")
    file(MAKE_DIRECTORY "${_dest_dir}")
    set(_dest_file "${_dest_dir}/httplib.h")

    message(STATUS "cpp-httplib: downloading ${_url}")
    file(DOWNLOAD "${_url}" "${_dest_file}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "cpp-httplib: failed to download ${_url}: ${_dl_msg}")
    endif()

    # A bad ref (typo'd tag/commit) yields GitHub's plain-text "404: Not
    # Found" page with an HTTP 200 from file(DOWNLOAD)'s perspective in some
    # environments, so explicitly sanity-check the content itself rather than
    # trusting the status code alone.
    file(READ "${_dest_file}" _content LIMIT 64)
    string(FIND "${_content}" "httplib" _found)
    if(_found EQUAL -1)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "cpp-httplib: downloaded content from ${_url} does not look like httplib.h - is the resolved ref ('${ref}') valid?")
    endif()

    file(WRITE "${_dest_dir}/.gte_fetched_ref" "${ref}")

    message(STATUS "cpp-httplib: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_httplib()
#
# Ensures httplib.h is present in this project (downloading it from GitHub if
# needed), then defines the `httplib` INTERFACE target described above.
function(fetch_httplib)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_httplib() only supports Windows. Not supported on this platform.")
    endif()

    set(_httplib_marker "${CMAKE_SOURCE_DIR}/third_party/httplib/httplib.h")
    set(_httplib_ref_marker "${CMAKE_SOURCE_DIR}/third_party/httplib/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_httplib_marker}" AND EXISTS "${_httplib_ref_marker}")
        if(NOT HTTPLIB_RELEASE_TAG STREQUAL "latest")
            file(READ "${_httplib_ref_marker}" _staged_ref)
            string(STRIP "${_staged_ref}" _staged_ref)
            if(_staged_ref STREQUAL HTTPLIB_RELEASE_TAG)
                set(_already_staged TRUE)
            endif()
        else()
            # "latest" is a moving target we can't cheaply compare without a
            # network call - if something is already staged at all, treat it
            # as good enough (same "don't re-resolve latest on every
            # configure once staged" behavior as FetchVMA.cmake's fetch_vma(),
            # which only checks file existence). Use HTTPLIB_FORCE_REDOWNLOAD
            # to explicitly move to whatever "latest" resolves to right now.
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT HTTPLIB_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "cpp-httplib: already present and matching ref '${HTTPLIB_RELEASE_TAG}' - skipping download.")
    else()
        _httplib_resolve_tag("${HTTPLIB_RELEASE_TAG}" _resolved_ref)
        message(STATUS "cpp-httplib: resolved ref '${_resolved_ref}'")
        _httplib_download_and_stage("${_resolved_ref}")
    endif()

    if(NOT TARGET httplib)
        add_library(httplib INTERFACE)
        target_include_directories(httplib INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/httplib"
        )
        # cpp-httplib's socket layer needs Winsock on Windows; crypt32 backs
        # its certificate-store lookups (used even in some non-OpenSSL code
        # paths). Both are standard Windows SDK import libraries, always
        # available with any Windows toolchain used by this project.
        target_link_libraries(httplib INTERFACE ws2_32 crypt32)
    endif()
endfunction()
