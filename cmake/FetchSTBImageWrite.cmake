# FetchSTBImageWrite.cmake
#
# Downloads stb_image_write.h (https://github.com/nothings/stb) straight from
# its GitHub repo, the same way FetchSTB.cmake fetches stb_image.h - no
# submodule, no package manager, nothing pre-installed on the machine. See
# FetchSTB.cmake's own header comment for the full rationale behind this
# "fetch a single raw file straight from GitHub" approach; this file mirrors
# it almost verbatim, adjusted only for stb_image_write.h's own filename/
# target/cache-variable names.
#
# stb_image_write.h is a single public-domain header providing image
# *encoding* (PNG/BMP/TGA/JPG/HDR) - the write-side counterpart of the
# already-vendored stb_image.h *decoder*. Like stb_image.h/VMA, the
# "implementation" only exists once some translation unit defines
# STB_IMAGE_WRITE_IMPLEMENTATION before including it; this module only stages
# the header and exposes an INTERFACE target, and deliberately does NOT
# compile an implementation .cpp anywhere - that's for whichever engine source
# file first needs real PNG encoding to add (see src/Encoding/PngEncoder.cpp,
# this campaign's own one translation unit that does exactly that).
#
# stb_image_write.h is fetched directly from GitHub's raw content endpoint
# (https://raw.githubusercontent.com/nothings/stb/<ref>/stb_image_write.h) -
# no ZIP download/extract step needed, same as stb_image.h.
#
# Staged into this repo (gitignored, regenerated automatically on configure -
# see .gitignore), into the SAME third_party/stb/ directory stb_image.h
# already lives in - these are companion files from the same upstream repo,
# no reason to split them into two directories:
#
#   ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image_write.h
#   ${CMAKE_SOURCE_DIR}/third_party/stb/.gte_fetched_ref_write - plain text
#       file recording exactly which resolved ref is currently staged - a
#       SEPARATE marker from stb_image.h's own .gte_fetched_ref, since the two
#       files can, in principle, be pinned to different commits if ever
#       needed; don't conflate their staleness tracking.
#
# Defines one target:
#   stb_image_write   - INTERFACE library exposing third_party/stb as an
#                        include directory. Engine code should
#                        `#include <stb_image_write.h>`. Whichever
#                        translation unit adds STB_IMAGE_WRITE_IMPLEMENTATION
#                        owns the one-and-only compiled implementation,
#                        exactly like this project's STB_IMAGE_IMPLEMENTATION/
#                        VMA_IMPLEMENTATION convention.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   STB_IMAGE_WRITE_RELEASE_TAG      - Git ref to fetch stb_image_write.h
#                                       from nothings/stb at: a branch (e.g.
#                                       "master"), or a full/abbreviated
#                                       commit SHA. Defaults to the SAME
#                                       pinned commit SHA already used by
#                                       STB_IMAGE_RELEASE_TAG (see
#                                       FetchSTB.cmake) - both files live at
#                                       that exact commit in the upstream
#                                       repo, and keeping them pinned together
#                                       avoids any risk of a version mismatch
#                                       between the two.
#   STB_IMAGE_WRITE_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if
#                                       already present and already matching
#                                       STB_IMAGE_WRITE_RELEASE_TAG.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSTBImageWrite.cmake only supports Windows. Not supported on this platform.")
endif()

# Pinned to the SAME commit stb_image.h itself is pinned to (see
# FetchSTB.cmake's own STB_IMAGE_RELEASE_TAG) - override via
# -DSTB_IMAGE_WRITE_RELEASE_TAG=... (a branch name or another commit SHA) if a
# deliberate upgrade is ever needed.
set(STB_IMAGE_WRITE_RELEASE_TAG "2c980bb59875b0d32144a71867fbdebb2f77cd20" CACHE STRING
    "stb git ref to fetch stb_image_write.h from (e.g. 'master' or a commit SHA - defaults to the same pinned commit SHA already used for stb_image.h, see this file's header comment).")
option(STB_IMAGE_WRITE_FORCE_REDOWNLOAD
    "Force re-downloading stb_image_write.h even if it already appears to be present and matching STB_IMAGE_WRITE_RELEASE_TAG."
    OFF)

# _stb_image_write_download_and_stage(<ref>)
#
# Downloads GitHub's raw stb_image_write.h content for a concrete nothings/stb
# ref (branch name or commit SHA) directly - no archive to extract, since this
# is a single-file library.
function(_stb_image_write_download_and_stage ref)
    set(_url "https://raw.githubusercontent.com/nothings/stb/${ref}/stb_image_write.h")
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/stb")
    file(MAKE_DIRECTORY "${_dest_dir}")
    set(_dest_file "${_dest_dir}/stb_image_write.h")

    message(STATUS "stb_image_write: downloading ${_url}")
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
        message(FATAL_ERROR "stb_image_write: failed to download ${_url}: ${_dl_msg}")
    endif()

    # A bad ref (typo'd branch/commit) yields GitHub's plain-text "404: Not
    # Found" page with an HTTP 200 from file(DOWNLOAD)'s perspective in some
    # environments, so explicitly sanity-check the content itself rather than
    # trusting the status code alone.
    file(READ "${_dest_file}" _content LIMIT 64)
    string(FIND "${_content}" "stb_image_write" _found)
    if(_found EQUAL -1)
        file(REMOVE "${_dest_file}")
        message(FATAL_ERROR "stb_image_write: downloaded content from ${_url} does not look like stb_image_write.h - is STB_IMAGE_WRITE_RELEASE_TAG ('${ref}') a valid ref?")
    endif()

    file(WRITE "${_dest_dir}/.gte_fetched_ref_write" "${ref}")

    message(STATUS "stb_image_write: staged '${ref}' -> ${_dest_dir}")
endfunction()

# fetch_stb_image_write()
#
# Ensures stb_image_write.h is present in this project (downloading it from
# GitHub if needed), then defines the `stb_image_write` INTERFACE target
# described above.
function(fetch_stb_image_write)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_stb_image_write() only supports Windows. Not supported on this platform.")
    endif()

    set(_stb_write_marker "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image_write.h")
    set(_stb_write_ref_marker "${CMAKE_SOURCE_DIR}/third_party/stb/.gte_fetched_ref_write")

    set(_already_staged FALSE)
    if(EXISTS "${_stb_write_marker}" AND EXISTS "${_stb_write_ref_marker}")
        file(READ "${_stb_write_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL STB_IMAGE_WRITE_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT STB_IMAGE_WRITE_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "stb_image_write: already present and matching ref '${STB_IMAGE_WRITE_RELEASE_TAG}' - skipping download.")
    else()
        _stb_image_write_download_and_stage("${STB_IMAGE_WRITE_RELEASE_TAG}")
    endif()

    if(NOT TARGET stb_image_write)
        add_library(stb_image_write INTERFACE)
        target_include_directories(stb_image_write INTERFACE
            "${CMAKE_SOURCE_DIR}/third_party/stb"
        )
    endif()
endfunction()
