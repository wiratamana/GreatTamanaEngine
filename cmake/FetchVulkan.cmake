# FetchVulkan.cmake
#
# Gets Vulkan onto the build WITHOUT requiring the Vulkan SDK to be installed
# on the machine (unlike the usual find_package(Vulkan) approach, which needs
# the LunarG SDK present and VULKAN_SDK set). Instead, this fetches two
# source-only dependencies straight from their GitHub repos, the same way
# FetchSDL3.cmake fetches SDL3:
#
#   - Vulkan-Headers (KhronosGroup/Vulkan-Headers) - the official C headers
#     (vulkan/*.h, vk_video/*.h). Header-only, nothing to compile/link.
#   - volk (zeux/volk)                              - a tiny meta-loader that
#     dynamically loads vulkan-1.dll (LoadLibrary/GetProcAddress) at runtime
#     and resolves every Vulkan function pointer through it.
#
# Because volk loads the driver's vulkan-1.dll dynamically at runtime, there
# is no import library to link against and nothing to copy next to the .exe:
# any machine with a Vulkan-capable GPU driver installed (i.e. any machine
# that can run Vulkan software at all) already has vulkan-1.dll available via
# the normal DLL search path. That's what makes this portable across
# different machines with zero manual setup.
#
# Downloading works by tag, straight from GitHub's codeload archive URL
# (https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip) - this
# needs no GitHub API call at all when an explicit tag is configured. Note
# that Vulkan-Headers does not publish GitHub "Releases", only tags, so
# resolving "latest" for it falls back to the tags API (releases/latest
# would 404); volk does publish proper Releases, so its "latest" resolves via
# the releases API. Either way, once a concrete tag name is known the actual
# download is a plain codeload archive fetch.
#
# Staged into this repo (all gitignored, regenerated automatically on
# configure - see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/include/vulkan/*.h     - Vulkan-Headers
#   ${CMAKE_SOURCE_DIR}/include/vk_video/*.h   - Vulkan-Headers (video codec headers)
#   ${CMAKE_SOURCE_DIR}/third_party/volk/*     - volk.h / volk.c
#
# Defines two targets:
#   Vulkan::Headers  - INTERFACE target exposing the Vulkan-Headers include dir.
#   volk             - STATIC library compiling volk.c; links Vulkan::Headers
#                       publicly. Engine code should `#include <volk.h>`
#                       (never `<vulkan/vulkan.h>` directly) and call
#                       volkInitialize()/volkLoadInstance()/volkLoadDevice()
#                       to resolve function pointers.
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   VULKAN_HEADERS_RELEASE_TAG      - Git tag to fetch from
#                                      KhronosGroup/Vulkan-Headers, e.g.
#                                      "vulkan-sdk-1.4.357.0" or "v1.4.360".
#                                      Defaults to "latest".
#   VULKAN_HEADERS_FORCE_REDOWNLOAD - Set to ON to force re-fetching even if
#                                      already present.
#   VOLK_RELEASE_TAG                - Git tag to fetch from zeux/volk, e.g.
#                                      "1.4.350". Defaults to "latest".
#   VOLK_FORCE_REDOWNLOAD           - Set to ON to force re-fetching even if
#                                      already present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchVulkan.cmake only supports Windows. Not supported on this platform.")
endif()

set(VULKAN_HEADERS_RELEASE_TAG "latest" CACHE STRING
    "Vulkan-Headers git tag to fetch (e.g. vulkan-sdk-1.4.357.0), or 'latest'.")
option(VULKAN_HEADERS_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting Vulkan-Headers even if it already appears to be present."
    OFF)

set(VOLK_RELEASE_TAG "latest" CACHE STRING
    "volk git tag to fetch (e.g. 1.4.350), or 'latest'.")
option(VOLK_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting volk even if it already appears to be present."
    OFF)

# _github_get_json(<label> <url> <out_json>)
#
# Downloads a small JSON document from the GitHub API and reads it back into
# a variable. Returns the raw JSON via out_json; sets out_json to "" (and
# leaves a status message) if the request failed, so callers can fall back
# to another endpoint instead of hard-failing.
function(_github_get_json label url out_json)
    set(_work_dir "${CMAKE_BINARY_DIR}/_vulkan_fetch")
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

# _github_resolve_tag(<owner> <repo> <tag> <out_tag_name>)
#
# Resolves "latest" to a concrete tag name. If <tag> isn't "latest", this is
# a no-op (no network call at all - the tag is used as-is). For "latest":
# first tries the releases API (works for repos with real GitHub Releases,
# e.g. volk); if that fails (e.g. 404 because the repo only has plain git
# tags and no Releases, like Vulkan-Headers), falls back to the tags API and
# takes its first entry.
function(_github_resolve_tag owner repo tag out_tag_name)
    if(NOT tag STREQUAL "latest")
        set(${out_tag_name} "${tag}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "${repo}: resolving 'latest' via releases API")
    _github_get_json("${repo}_release_latest" "https://api.github.com/repos/${owner}/${repo}/releases/latest" _json)
    if(NOT _json STREQUAL "")
        string(JSON _tag_name GET "${_json}" "tag_name")
        set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "${repo}: no GitHub Releases found, falling back to tags API for 'latest'")
    _github_get_json("${repo}_tags" "https://api.github.com/repos/${owner}/${repo}/tags" _json)
    if(_json STREQUAL "")
        message(FATAL_ERROR "${repo}: failed to resolve 'latest' via both the releases API and the tags API.")
    endif()

    string(JSON _tag_count LENGTH "${_json}")
    if(_tag_count EQUAL 0)
        message(FATAL_ERROR "${repo}: tags API returned no tags at all.")
    endif()
    string(JSON _tag_name GET "${_json}" 0 "name")
    set(${out_tag_name} "${_tag_name}" PARENT_SCOPE)
endfunction()

# _download_and_extract_tag(<owner> <repo> <tag_name> <extract_dir> <out_root_dir>)
#
# Downloads GitHub's plain codeload archive for a concrete tag
# (https://github.com/<owner>/<repo>/archive/refs/tags/<tag>.zip - no GitHub
# API call involved) and extracts it, returning the single top-level folder
# GitHub always wraps archive contents in.
function(_download_and_extract_tag owner repo tag_name extract_dir out_root_dir)
    set(_work_dir "${CMAKE_BINARY_DIR}/_vulkan_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip_path "${_work_dir}/${repo}-${tag_name}.zip")
    set(_zip_url "https://github.com/${owner}/${repo}/archive/refs/tags/${tag_name}.zip")

    message(STATUS "${repo}: downloading ${_zip_url}")
    file(DOWNLOAD "${_zip_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "${repo}: failed to download ${_zip_url}: ${_dl_msg}")
    endif()

    file(REMOVE_RECURSE "${extract_dir}")
    file(MAKE_DIRECTORY "${extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${extract_dir}/*")
    list(LENGTH _extracted_root _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "${repo}: unexpected archive layout, expected exactly one top-level folder after extraction, found ${_n}.")
    endif()
    list(GET _extracted_root 0 _root)
    set(${out_root_dir} "${_root}" PARENT_SCOPE)
endfunction()

function(_vulkan_headers_download_and_stage)
    _github_resolve_tag("KhronosGroup" "Vulkan-Headers" "${VULKAN_HEADERS_RELEASE_TAG}" _resolved_tag)
    message(STATUS "Vulkan-Headers: resolved tag '${_resolved_tag}'")

    _download_and_extract_tag("KhronosGroup" "Vulkan-Headers" "${_resolved_tag}" "${CMAKE_BINARY_DIR}/_vulkan_fetch/headers_extracted" _root)

    if(NOT EXISTS "${_root}/include/vulkan/vulkan.h")
        message(FATAL_ERROR "Vulkan-Headers: extracted package is missing include/vulkan/vulkan.h")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/include")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/include/vulkan")
    file(COPY "${_root}/include/vulkan" DESTINATION "${CMAKE_SOURCE_DIR}/include")

    # vk_video/*.h is transitively included by newer vulkan_core.h for video
    # codec extensions - stage it too if this release ships it.
    if(EXISTS "${_root}/include/vk_video")
        file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/include/vk_video")
        file(COPY "${_root}/include/vk_video" DESTINATION "${CMAKE_SOURCE_DIR}/include")
    endif()

    message(STATUS "Vulkan-Headers: staged -> ${CMAKE_SOURCE_DIR}/include/vulkan")
endfunction()

function(_volk_download_and_stage)
    _github_resolve_tag("zeux" "volk" "${VOLK_RELEASE_TAG}" _resolved_tag)
    message(STATUS "volk: resolved tag '${_resolved_tag}'")

    _download_and_extract_tag("zeux" "volk" "${_resolved_tag}" "${CMAKE_BINARY_DIR}/_vulkan_fetch/volk_extracted" _root)

    if(NOT EXISTS "${_root}/volk.h" OR NOT EXISTS "${_root}/volk.c")
        message(FATAL_ERROR "volk: extracted package is missing volk.h/volk.c")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/volk")
    file(COPY "${_root}/volk.h" "${_root}/volk.c" DESTINATION "${CMAKE_SOURCE_DIR}/third_party/volk")

    message(STATUS "volk: staged -> ${CMAKE_SOURCE_DIR}/third_party/volk")
endfunction()

# fetch_vulkan()
#
# Ensures Vulkan-Headers and volk are present in this project (downloading
# them from GitHub if needed), then defines the Vulkan::Headers and volk
# targets described above.
function(fetch_vulkan)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_vulkan() only supports Windows. Not supported on this platform.")
    endif()

    set(_vk_header_marker "${CMAKE_SOURCE_DIR}/include/vulkan/vulkan.h")
    if(NOT VULKAN_HEADERS_FORCE_REDOWNLOAD AND EXISTS "${_vk_header_marker}")
        message(STATUS "Vulkan-Headers: already present (include/vulkan/vulkan.h found) - skipping download.")
    else()
        _vulkan_headers_download_and_stage()
    endif()

    set(_volk_marker_h "${CMAKE_SOURCE_DIR}/third_party/volk/volk.h")
    set(_volk_marker_c "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c")
    if(NOT VOLK_FORCE_REDOWNLOAD AND EXISTS "${_volk_marker_h}" AND EXISTS "${_volk_marker_c}")
        message(STATUS "volk: already present (third_party/volk/volk.h, volk.c found) - skipping download.")
    else()
        _volk_download_and_stage()
    endif()

    if(NOT TARGET Vulkan::Headers)
        add_library(VulkanHeaders_interface INTERFACE)
        target_include_directories(VulkanHeaders_interface INTERFACE
            "${CMAKE_SOURCE_DIR}/include"
        )
        add_library(Vulkan::Headers ALIAS VulkanHeaders_interface)
    endif()

    if(NOT TARGET volk)
        add_library(volk STATIC
            "${CMAKE_SOURCE_DIR}/third_party/volk/volk.c"
            "${CMAKE_SOURCE_DIR}/third_party/volk/volk.h"
        )
        target_include_directories(volk PUBLIC
            "${CMAKE_SOURCE_DIR}/third_party/volk"
        )
        target_link_libraries(volk PUBLIC Vulkan::Headers)
    endif()
endfunction()
