# FetchCgltf.cmake
#
# Downloads cgltf.h (https://github.com/jkuhlmann/cgltf) straight from its
# GitHub repo, the same way FetchSTB.cmake fetches stb_image.h - no submodule,
# no package manager, nothing pre-installed on the machine.
#
# cgltf.h is a single-file, zero-dependency (C standard library only), MIT
# licensed glTF 2.0 loader written in C99. It parses .gltf/.glb files into a
# plain C struct tree (cgltf_data) and can load the .bin buffers and decode
# accessors. Like stb_image.h and VMA (see FetchSTB.cmake/FetchVMA.cmake), the
# "implementation" only exists once ONE translation unit defines
# CGLTF_IMPLEMENTATION before including it:
#
#     #define CGLTF_IMPLEMENTATION
#     #include <cgltf.h>
#
# This module only stages the header and exposes an INTERFACE target. It
# deliberately does NOT compile an implementation .cpp anywhere - that is for
# whichever engine source file first needs real glTF loading to add. Every
# OTHER file just does `#include <cgltf.h>` without the define.
#
# Only cgltf.h is fetched. cgltf_write.h (the glTF writer, a separate header in
# the same repo) is NOT fetched - this project only needs to read glTF today.
#
# Single file, so this module fetches it directly from GitHub's raw content
# endpoint (https://raw.githubusercontent.com/jkuhlmann/cgltf/<ref>/cgltf.h) -
# no ZIP download/extract step. That endpoint accepts a branch name, a tag, or
# a commit SHA as <ref>.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/cgltf/cgltf.h
#   ${CMAKE_SOURCE_DIR}/third_party/cgltf/.gte_fetched_ref - plain text file
#       recording exactly which ref is currently staged, so changing
#       CGLTF_RELEASE_TAG on a machine that already has a previous fetch
#       triggers a fresh re-download instead of silently reusing the old one.
#
# Defines one target:
#   cgltf   - INTERFACE library exposing third_party/cgltf as an include
#             directory. Engine code should `#include <cgltf.h>`.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   CGLTF_RELEASE_TAG      - Git ref to fetch cgltf.h at: a tag (e.g. "v1.15"),
#                             a branch (e.g. "master"), or a commit SHA.
#                             Defaults to the PINNED tag "v1.15" rather than
#                             "master" - master is a moving target that could
#                             silently change cgltf's API/behavior on a future
#                             fetch with zero warning (same reasoning as
#                             STB_IMAGE_RELEASE_TAG in FetchSTB.cmake). Pass
#                             -DCGLTF_RELEASE_TAG=master to follow master.
#   CGLTF_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if already
#                             present and already matching CGLTF_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchCgltf.cmake only supports Windows. Not supported on this platform.")
endif()

set(CGLTF_RELEASE_TAG "v1.15" CACHE STRING
    "cgltf git ref to fetch cgltf.h from (a tag like 'v1.15', a branch like 'master', or a commit SHA).")
option(CGLTF_FORCE_REDOWNLOAD
    "Force re-downloading cgltf.h even if it already appears to be present and matching CGLTF_RELEASE_TAG."
    OFF)

# _cgltf_download_and_stage(<ref>)
#
# Downloads GitHub's raw cgltf.h content for a concrete jkuhlmann/cgltf ref
# directly - no archive to extract, since this is a single-file library.
function(_cgltf_download_and_stage ref)
    set(_url "https://raw.githubusercontent.com/jkuhlmann/cgltf/${ref}/cgltf.h")
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/cgltf")
    file(MAKE_DIRECTORY "${_dest_dir}")
    set(_dest_file "${_dest_dir}/cgltf.h")

    message(STATUS "cgltf: downloading ${_url}")
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
        message(FATAL_ERROR "cgltf: failed to download ${_url}: ${_dl_msg}")
    endif()

    # A bad ref (typo'd tag/branch) can yield GitHub's plain-text "404: Not
    # Found" page, so sanity-check the content itself, not just the status
    # code. The real file contains the word "cgltf" within its first lines.
    file(READ "${_dest_file}" _content LIMIT 256)
    string(FIND "${_content}" "cgltf" _found)
    if(_found EQUAL -1)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "cgltf: downloaded content from ${_url} does not look like cgltf.h - is CGLTF_RELEASE_TAG ('${ref}') a valid ref?")
    endif()

    file(WRITE "${_dest_dir}/.gte_fetched_ref" "${ref}")

    message(STATUS "cgltf: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_cgltf()
#
# Ensures cgltf.h is present in this project (downloading it from GitHub if
# needed), then defines the `cgltf` INTERFACE target described above.
function(fetch_cgltf)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_cgltf() only supports Windows. Not supported on this platform.")
    endif()

    set(_cgltf_marker "${CMAKE_SOURCE_DIR}/third_party/cgltf/cgltf.h")
    set(_cgltf_ref_marker "${CMAKE_SOURCE_DIR}/third_party/cgltf/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_cgltf_marker}" AND EXISTS "${_cgltf_ref_marker}")
        file(READ "${_cgltf_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL CGLTF_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT CGLTF_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "cgltf: already present and matching ref '${CGLTF_RELEASE_TAG}' - skipping download.")
    else()
        _cgltf_download_and_stage("${CGLTF_RELEASE_TAG}")
    endif()

    if(NOT TARGET cgltf)
        add_library(cgltf INTERFACE)
        target_include_directories(cgltf INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/cgltf"
        )
    endif()
endfunction()
