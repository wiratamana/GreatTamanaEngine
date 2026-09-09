# FetchJson.cmake
#
# Downloads nlohmann/json (https://github.com/nlohmann/json) straight from its
# GitHub repo, the same way FetchHttplib.cmake/FetchSTB.cmake/FetchVMA.cmake
# fetch their own single-header dependencies - no submodule, no package
# manager, nothing pre-installed on the machine.
#
# nlohmann/json is a single-header (json.hpp), header-only, MIT-licensed
# modern-C++ JSON library. It is being brought in specifically for
# task_manager/network-impl-3's new POST /instantiate_primitive and
# POST /delete_entity endpoints (see PHASE0_MASTER_STRATEGY.md, "Locked Design
# Decisions" #1) - real, untrusted, possibly-malformed JSON parsing is
# non-trivial to hand-roll correctly (nesting, escaping, malformed input,
# numeric edge cases), unlike this engine's own hand-rolled *.gtscene text
# format (see README.md, "Status" - that format deliberately avoided a JSON
# dependency; this campaign is the first place that genuinely needs one).
#
# nlohmann/json's GitHub repository commits a ready-to-use, fully
# self-contained single header at single_include/nlohmann/json.hpp - this is
# nlohmann/json's own documented "amalgamated header" distribution mechanism.
#
# Downloading works the same way FetchHttplib.cmake fetches httplib.h: directly
# from GitHub's raw content endpoint
# (https://raw.githubusercontent.com/nlohmann/json/<ref>/single_include/nlohmann/json.hpp)
# - no ZIP download/extract step needed, since this is a single file.
# nlohmann/json publishes proper GitHub Releases/tags, so
# NLOHMANN_JSON_RELEASE_TAG supports "latest" (resolved via the releases API,
# same convention as HTTPLIB_RELEASE_TAG/VMA_RELEASE_TAG) in addition to a
# pinned tag/commit SHA.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/json/nlohmann/json.hpp
#   ${CMAKE_SOURCE_DIR}/third_party/json/.gte_fetched_ref - plain text file
#       recording exactly which resolved ref is currently staged (mirrors
#       FetchHttplib.cmake's own marker file).
#
# Note the extra `nlohmann/` subfolder under third_party/json/ - this is what
# makes `target_include_directories(... "${CMAKE_SOURCE_DIR}/third_party/json")`
# + `#include <nlohmann/json.hpp>` work, mirroring exactly how
# third_party/httplib/httplib.h + `target_include_directories(...
# "${CMAKE_SOURCE_DIR}/third_party/httplib")` + `#include <httplib.h>` works
# today.
#
# Defines one target:
#   nlohmann_json   - INTERFACE library exposing third_party/json as an
#                      include directory. Engine code should
#                      `#include <nlohmann/json.hpp>`. Header-only, pure C++,
#                      with NO platform library dependency of its own (unlike
#                      `httplib`, which needs ws2_32/crypt32) - no
#                      target_link_libraries() call is added for it.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   NLOHMANN_JSON_RELEASE_TAG      - Git tag/commit SHA to fetch json.hpp from
#                                     nlohmann/json at, e.g. "v3.11.3".
#                                     Defaults to "latest" (resolved via the
#                                     GitHub releases API, same as
#                                     HTTPLIB_RELEASE_TAG's default in
#                                     FetchHttplib.cmake).
#   NLOHMANN_JSON_FORCE_REDOWNLOAD  - Set to ON to force re-fetching even if
#                                     already present and already matching
#                                     NLOHMANN_JSON_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchJson.cmake only supports Windows. Not supported on this platform.")
endif()

set(NLOHMANN_JSON_RELEASE_TAG "latest" CACHE STRING
    "nlohmann/json git ref to fetch json.hpp from (e.g. a tag like 'v3.11.3', a commit SHA, or 'latest').")
option(NLOHMANN_JSON_FORCE_REDOWNLOAD
    "Force re-downloading json.hpp even if it already appears to be present and matching NLOHMANN_JSON_RELEASE_TAG."
    OFF)

# _json_github_get_json(<label> <url> <out_json>)
#
# Same helper as FetchHttplib.cmake's _httplib_github_get_json /
# FetchVMA.cmake's _vma_github_get_json, duplicated locally (with a distinct
# `_json_` prefix, since this module and FetchHttplib.cmake are both
# include()-d into the same top-level CMakeLists.txt, and plain CMake
# functions are not scoped) so this module has no include-order dependency on
# any of them having been included first.
function(_json_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_json_fetch")
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

# _json_resolve_tag(<tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name via the GitHub releases API. If
# <tag> isn't "latest", this is a no-op (no network call at all) - same
# convention as FetchHttplib.cmake's _httplib_resolve_tag.
function(_json_resolve_tag tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "nlohmann/json: resolving 'latest' via releases API")
    _json_github_get_json("json_release_latest"
        "https://api.github.com/repos/nlohmann/json/releases/latest" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "nlohmann/json: failed to resolve 'latest' via the releases API.")
    endif()

    string(JSON _tag_name GET "${_json}" "tag_name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _json_download_and_stage(<ref>)
#
# Downloads GitHub's raw single_include/nlohmann/json.hpp content for a
# concrete nlohmann/json ref (tag or commit SHA) directly - no archive to
# extract, since this is a single-file library (mirrors
# FetchHttplib.cmake's own _httplib_download_and_stage).
function(_json_download_and_stage ref)
    set(_url "https://raw.githubusercontent.com/nlohmann/json/${ref}/single_include/nlohmann/json.hpp")
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/json/nlohmann")
    file(MAKE_DIRECTORY "${_dest_dir}")
    set(_dest_file "${_dest_dir}/json.hpp")

    message(STATUS "nlohmann/json: downloading ${_url}")
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
        message(FATAL_ERROR "nlohmann/json: failed to download ${_url}: ${_dl_msg}")
    endif()

    # A bad ref (typo'd tag/commit) yields GitHub's plain-text "404: Not
    # Found" page with an HTTP 200 from file(DOWNLOAD)'s perspective in some
    # environments, so explicitly sanity-check the content itself rather than
    # trusting the status code alone.
    file(READ "${_dest_file}" _content LIMIT 256)
    string(FIND "${_content}" "nlohmann" _found)
    if(_found EQUAL -1)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "nlohmann/json: downloaded content from ${_url} does not look like json.hpp - is the resolved ref ('${ref}') valid?")
    endif()

    file(WRITE "${CMAKE_SOURCE_DIR}/third_party/json/.gte_fetched_ref" "${ref}")

    message(STATUS "nlohmann/json: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_json()
#
# Ensures json.hpp is present in this project (downloading it from GitHub if
# needed), then defines the `nlohmann_json` INTERFACE target described above.
function(fetch_json)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_json() only supports Windows. Not supported on this platform.")
    endif()

    set(_json_marker "${CMAKE_SOURCE_DIR}/third_party/json/nlohmann/json.hpp")
    set(_json_ref_marker "${CMAKE_SOURCE_DIR}/third_party/json/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_json_marker}" AND EXISTS "${_json_ref_marker}")
        if(NOT NLOHMANN_JSON_RELEASE_TAG STREQUAL "latest")
            file(READ "${_json_ref_marker}" _staged_ref)
            string(STRIP "${_staged_ref}" _staged_ref)
            if(_staged_ref STREQUAL NLOHMANN_JSON_RELEASE_TAG)
                set(_already_staged TRUE)
            endif()
        else()
            # "latest" is a moving target we can't cheaply compare without a
            # network call - if something is already staged at all, treat it
            # as good enough (same "don't re-resolve latest on every
            # configure once staged" behavior as FetchHttplib.cmake's
            # fetch_httplib()). Use NLOHMANN_JSON_FORCE_REDOWNLOAD to
            # explicitly move to whatever "latest" resolves to right now.
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT NLOHMANN_JSON_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "nlohmann/json: already present and matching ref '${NLOHMANN_JSON_RELEASE_TAG}' - skipping download.")
    else()
        _json_resolve_tag("${NLOHMANN_JSON_RELEASE_TAG}" _resolved_ref)
        message(STATUS "nlohmann/json: resolved ref '${_resolved_ref}'")
        _json_download_and_stage("${_resolved_ref}")
    endif()

    if(NOT TARGET nlohmann_json)
        add_library(nlohmann_json INTERFACE)
        target_include_directories(nlohmann_json INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/json"
        )
        # Header-only, pure C++, no platform library dependency - unlike
        # `httplib` (ws2_32/crypt32), nothing to link here.
    endif()
endfunction()
