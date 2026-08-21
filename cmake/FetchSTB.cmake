# FetchSTB.cmake
#
# Downloads stb_image.h (https://github.com/nothings/stb) straight from its
# GitHub repo, the same way FetchSDL3.cmake/FetchVulkan.cmake/FetchImGui.cmake/
# FetchVMA.cmake/FetchImGuizmo.cmake fetch their dependencies - no submodule,
# no package manager, nothing pre-installed on the machine.
#
# stb_image.h is a single public-domain header providing image
# loading/decoding (PNG/JPEG/BMP/GIF/TGA/PSD/HDR/...) - this is what backs the
# Editor's image-file-loading support. Like VMA (see FetchVMA.cmake), the
# "implementation" only exists once some translation unit defines
# STB_IMAGE_IMPLEMENTATION before including it; this module only stages the
# header and exposes an INTERFACE target, and deliberately does NOT compile
# an implementation .cpp anywhere - that's for whichever engine source file
# first needs real image decoding to add (with `#define
# STB_IMAGE_IMPLEMENTATION` above a single `#include <stb_image.h>`).
#
# Unlike ImGui/ImGuizmo/VMA (multi-file libraries fetched by downloading and
# extracting a whole GitHub archive), stb_image.h is a single file, so this
# module fetches it directly from GitHub's raw content endpoint
# (https://raw.githubusercontent.com/nothings/stb/<ref>/stb_image.h) - no ZIP
# download/extract step needed. That endpoint transparently accepts a branch
# name, a tag, or a full/abbreviated commit SHA as <ref>, so branch/tag/commit
# all keep working through the exact same fetch function (stb itself does not
# publish GitHub Releases/tags, so in practice <ref> is "master" or a pinned
# commit SHA - see STB_IMAGE_RELEASE_TAG below).
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h
#   ${CMAKE_SOURCE_DIR}/third_party/stb/.gte_fetched_ref - plain text file
#       recording exactly which resolved ref is currently staged, so
#       switching STB_IMAGE_RELEASE_TAG on a machine that already has a
#       previous fetch staged correctly triggers a fresh re-download instead
#       of silently reusing the wrong version.
#
# Defines one target:
#   stb_image   - INTERFACE library exposing third_party/stb as an include
#                 directory. Engine code should `#include "stb_image.h"`.
#                 Whichever translation unit adds STB_IMAGE_IMPLEMENTATION
#                 owns the one-and-only compiled implementation, exactly like
#                 this project's VMA_IMPLEMENTATION convention (see
#                 FetchVMA.cmake's own header comment).
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   STB_IMAGE_RELEASE_TAG      - Git ref to fetch stb_image.h from
#                                 nothings/stb at: a branch (e.g. "master"),
#                                 or a full/abbreviated commit SHA. Defaults
#                                 to a PINNED commit SHA (see the `set()`
#                                 call below) rather than "master" - "master"
#                                 is a moving target that could silently
#                                 change stb_image's public API/behavior
#                                 underneath this engine on a future fetch
#                                 with zero warning; pinning removes that
#                                 risk entirely, matching this project's
#                                 IMGUIZMO_RELEASE_TAG convention (see
#                                 FetchImGuizmo.cmake). Bump this
#                                 deliberately to move to a newer commit.
#   STB_IMAGE_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if
#                                 already present and already matching
#                                 STB_IMAGE_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSTB.cmake only supports Windows. Not supported on this platform.")
endif()

# Pinned to a specific commit (not "master") deliberately - see
# STB_IMAGE_RELEASE_TAG's own doc comment above for why. This is the commit
# at the tip of master (stb_image.h v2.30) as of integrating image-file
# loading; override via -DSTB_IMAGE_RELEASE_TAG=... (a branch name or another
# commit SHA) if a deliberate upgrade is ever needed.
set(STB_IMAGE_RELEASE_TAG "2c980bb59875b0d32144a71867fbdebb2f77cd20" CACHE STRING
    "stb git ref to fetch stb_image.h from (e.g. 'master' or a commit SHA - defaults to a pinned commit SHA, see this file's header comment).")
option(STB_IMAGE_FORCE_REDOWNLOAD
    "Force re-downloading stb_image.h even if it already appears to be present and matching STB_IMAGE_RELEASE_TAG."
    OFF)

# _stb_download_and_stage(<ref>)
#
# Downloads GitHub's raw stb_image.h content for a concrete nothings/stb ref
# (branch name or commit SHA) directly - no archive to extract, since this is
# a single-file library.
function(_stb_download_and_stage ref)
    set(_url "https://raw.githubusercontent.com/nothings/stb/${ref}/stb_image.h")
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/stb")
    file(MAKE_DIRECTORY "${_dest_dir}")
    set(_dest_file "${_dest_dir}/stb_image.h")

    message(STATUS "stb_image: downloading ${_url}")
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
        message(FATAL_ERROR "stb_image: failed to download ${_url}: ${_dl_msg}")
    endif()

    # A bad ref (typo'd branch/commit) yields GitHub's plain-text "404: Not
    # Found" page with an HTTP 200 from file(DOWNLOAD)'s perspective in some
    # environments, so explicitly sanity-check the content itself rather than
    # trusting the status code alone.
    file(READ "${_dest_file}" _content LIMIT 64)
    string(FIND "${_content}" "stb_image" _found)
    if(_found EQUAL -1)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "stb_image: downloaded content from ${_url} does not look like stb_image.h - is STB_IMAGE_RELEASE_TAG ('${ref}') a valid ref?")
    endif()

    file(WRITE "${_dest_dir}/.gte_fetched_ref" "${ref}")

    message(STATUS "stb_image: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_stb()
#
# Ensures stb_image.h is present in this project (downloading it from GitHub
# if needed), then defines the `stb_image` INTERFACE target described above.
function(fetch_stb)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_stb() only supports Windows. Not supported on this platform.")
    endif()

    set(_stb_marker "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
    set(_stb_ref_marker "${CMAKE_SOURCE_DIR}/third_party/stb/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_stb_marker}" AND EXISTS "${_stb_ref_marker}")
        file(READ "${_stb_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL STB_IMAGE_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT STB_IMAGE_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "stb_image: already present and matching ref '${STB_IMAGE_RELEASE_TAG}' - skipping download.")
    else()
        _stb_download_and_stage("${STB_IMAGE_RELEASE_TAG}")
    endif()

    if(NOT TARGET stb_image)
        add_library(stb_image INTERFACE)
        target_include_directories(stb_image INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/stb"
        )
    endif()
endfunction()
