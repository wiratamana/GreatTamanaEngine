# FetchSDL3.cmake
#
# Downloads the official prebuilt "devel VC" package for SDL3 directly from
# the SDL GitHub repository's releases (https://github.com/libsdl-org/SDL)
# and stages the parts this project needs:
#
#   ${CMAKE_SOURCE_DIR}/include/SDL3/*.h   - SDL3 headers
#   ${CMAKE_SOURCE_DIR}/SDL3.dll           - SDL3 runtime binary (Windows x64)
#   ${CMAKE_SOURCE_DIR}/lib/SDL3.lib       - SDL3 import library (needed to link)
#
# Windows only. Also defines an IMPORTED target `SDL3::SDL3` that consumers
# can target_link_libraries() against, and a helper function
# sdl3_copy_runtime_dll(<target>) that copies SDL3.dll next to a target's
# build output after each build of that target.
#
# Tunable cache variables:
#   SDL3_RELEASE_TAG      - GitHub release tag to fetch, e.g. "release-3.2.16".
#                            Defaults to "latest" which always resolves to the
#                            newest published SDL3 release.
#   SDL3_FORCE_REDOWNLOAD - Set to ON to force re-downloading/re-extracting
#                            SDL3 even if it already looks present.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSDL3.cmake only supports Windows. Not supported on this platform.")
endif()

set(SDL3_RELEASE_TAG "latest" CACHE STRING
    "SDL3 GitHub release tag to fetch (e.g. release-3.2.16), or 'latest'.")
option(SDL3_FORCE_REDOWNLOAD
    "Force re-downloading/re-extracting SDL3 even if it already appears to be present."
    OFF)

function(_sdl3_github_api_url out_var)
    if(SDL3_RELEASE_TAG STREQUAL "latest")
        set(${out_var} "https://api.github.com/repos/libsdl-org/SDL/releases/latest" PARENT_SCOPE)
    else()
        set(${out_var} "https://api.github.com/repos/libsdl-org/SDL/releases/tags/${SDL3_RELEASE_TAG}" PARENT_SCOPE)
    endif()
endfunction()

function(_sdl3_download_and_stage)
    set(_work_dir "${CMAKE_BINARY_DIR}/_sdl3_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")

    _sdl3_github_api_url(_api_url)

    set(_release_json "${_work_dir}/release.json")
    message(STATUS "SDL3: querying release metadata from ${_api_url}")
    file(DOWNLOAD "${_api_url}" "${_release_json}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        HTTPHEADER "Accept: application/vnd.github+json"
        STATUS _dl_status
        TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        message(FATAL_ERROR "SDL3: failed to query GitHub releases API (${_api_url}): ${_dl_msg}")
    endif()

    file(READ "${_release_json}" _json)

    string(JSON _asset_count LENGTH "${_json}" "assets")
    if(_asset_count EQUAL 0)
        message(FATAL_ERROR "SDL3: release metadata contained no assets (release tag '${SDL3_RELEASE_TAG}').")
    endif()

    set(_download_url "")
    set(_found_name "")
    math(EXPR _last_index "${_asset_count} - 1")
    foreach(_i RANGE 0 ${_last_index})
        string(JSON _name GET "${_json}" "assets" ${_i} "name")
        if(_name MATCHES "^SDL3-devel-.*-VC\\.zip$")
            string(JSON _download_url GET "${_json}" "assets" ${_i} "browser_download_url")
            set(_found_name "${_name}")
            break()
        endif()
    endforeach()

    if(_download_url STREQUAL "")
        message(FATAL_ERROR "SDL3: could not find a 'SDL3-devel-*-VC.zip' asset in the resolved GitHub release.")
    endif()

    message(STATUS "SDL3: downloading ${_found_name}")
    set(_zip_path "${_work_dir}/${_found_name}")
    file(DOWNLOAD "${_download_url}" "${_zip_path}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status2
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status2 0 _dl_code2)
    if(NOT _dl_code2 EQUAL 0)
        list(GET _dl_status2 1 _dl_msg2)
        message(FATAL_ERROR "SDL3: failed to download ${_found_name}: ${_dl_msg2}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip_path}" DESTINATION "${_extract_dir}")

    file(GLOB _extracted_root LIST_DIRECTORIES true "${_extract_dir}/SDL3-*")
    list(LENGTH _extracted_root _n)
    if(_n EQUAL 0)
        message(FATAL_ERROR "SDL3: unexpected package layout, could not find an 'SDL3-*' folder after extraction.")
    endif()
    list(GET _extracted_root 0 _sdl3_root)

    if(NOT EXISTS "${_sdl3_root}/include/SDL3/SDL.h")
        message(FATAL_ERROR "SDL3: extracted package is missing include/SDL3/SDL.h")
    endif()
    if(NOT EXISTS "${_sdl3_root}/lib/x64/SDL3.dll")
        message(FATAL_ERROR "SDL3: extracted package is missing lib/x64/SDL3.dll")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/include")
    file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/include/SDL3")
    file(COPY "${_sdl3_root}/include/SDL3" DESTINATION "${CMAKE_SOURCE_DIR}/include")

    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
    file(COPY "${_sdl3_root}/lib/x64/SDL3.lib" DESTINATION "${CMAKE_SOURCE_DIR}/lib")

    file(COPY "${_sdl3_root}/lib/x64/SDL3.dll" DESTINATION "${CMAKE_SOURCE_DIR}")

    message(STATUS "SDL3: staged headers -> ${CMAKE_SOURCE_DIR}/include/SDL3")
    message(STATUS "SDL3: staged runtime -> ${CMAKE_SOURCE_DIR}/SDL3.dll")
    message(STATUS "SDL3: staged import lib -> ${CMAKE_SOURCE_DIR}/lib/SDL3.lib")
endfunction()

# fetch_sdl3()
#
# Ensures SDL3 headers + SDL3.dll (+ SDL3.lib) are present in this project,
# downloading/extracting them from the SDL GitHub release assets if needed.
# Defines the IMPORTED target SDL3::SDL3 and caches SDL3_DLL_PATH for use by
# sdl3_copy_runtime_dll().
function(fetch_sdl3)
    if(NOT WIN32)
        message(FATAL_ERROR "fetch_sdl3() only supports Windows. Not supported on this platform.")
    endif()

    set(_sdl3_include_dir "${CMAKE_SOURCE_DIR}/include/SDL3")
    set(_sdl3_dll_path     "${CMAKE_SOURCE_DIR}/SDL3.dll")
    set(_sdl3_lib_path     "${CMAKE_SOURCE_DIR}/lib/SDL3.lib")

    if(NOT SDL3_FORCE_REDOWNLOAD AND EXISTS "${_sdl3_include_dir}/SDL.h" AND EXISTS "${_sdl3_dll_path}" AND EXISTS "${_sdl3_lib_path}")
        message(STATUS "SDL3: already present (include/SDL3, SDL3.dll, lib/SDL3.lib found) - skipping download.")
    else()
        _sdl3_download_and_stage()
    endif()

    if(NOT TARGET SDL3_prebuilt)
        add_library(SDL3_prebuilt SHARED IMPORTED GLOBAL)
        set_target_properties(SDL3_prebuilt PROPERTIES
            IMPORTED_LOCATION "${_sdl3_dll_path}"
            IMPORTED_IMPLIB "${_sdl3_lib_path}"
            INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include"
        )
        add_library(SDL3::SDL3 ALIAS SDL3_prebuilt)
    endif()

    set(SDL3_DLL_PATH "${_sdl3_dll_path}" CACHE INTERNAL "Path to the staged SDL3.dll")
endfunction()

# sdl3_copy_runtime_dll(<target>)
#
# Adds a post-build step to <target> that copies SDL3.dll next to its build
# output, so the resulting executable can find SDL3.dll at runtime.
function(sdl3_copy_runtime_dll target)
    if(NOT DEFINED SDL3_DLL_PATH)
        message(FATAL_ERROR "sdl3_copy_runtime_dll(): fetch_sdl3() must be called first.")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL3_DLL_PATH}"
                "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copying SDL3.dll next to ${target}"
    )
endfunction()
