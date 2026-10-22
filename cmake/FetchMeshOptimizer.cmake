# Fetches meshoptimizer (MIT) from github.com/zeux/meshoptimizer into third_party/meshoptimizer
# and defines the STATIC library target `meshoptimizer`.
#
# MESHOPTIMIZER_RELEASE_TAG       pinned commit SHA (release v1.3). Never a branch or a tag name.
# MESHOPTIMIZER_FORCE_REDOWNLOAD  re-fetch even if the staged ref matches.
#
# Every staged file is checked against cmake/MeshOptimizerHashes.cmake on each configure.
# A mismatch, a missing file or an unlisted file is fatal. Regenerate the list after a SHA bump:
#   cmake -DGTE_MESHOPT_GENERATE_HASHES=ON -P cmake/FetchMeshOptimizer.cmake

if(NOT WIN32)
    message(FATAL_ERROR "FetchMeshOptimizer.cmake only supports Windows. Not supported on this platform.")
endif()

set(MESHOPTIMIZER_RELEASE_TAG "9e1f07b159d3cb777f1c67ed31fc11fd117986f4" CACHE STRING
    "meshoptimizer commit SHA to fetch (pinned commit, not a branch).")
option(MESHOPTIMIZER_FORCE_REDOWNLOAD
    "Force re-downloading meshoptimizer even if the staged ref matches MESHOPTIMIZER_RELEASE_TAG." OFF)

set(_GTE_MESHOPT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
get_filename_component(_GTE_MESHOPT_ROOT "${_GTE_MESHOPT_ROOT}" ABSOLUTE)
set(_GTE_MESHOPT_DIR "${_GTE_MESHOPT_ROOT}/third_party/meshoptimizer")
set(_GTE_MESHOPT_HASH_FILE "${CMAKE_CURRENT_LIST_DIR}/MeshOptimizerHashes.cmake")

# Downloads the commit archive and copies src/* and LICENSE.md into third_party/meshoptimizer.
function(_meshopt_download_and_stage sha)
    if(NOT sha MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "meshoptimizer: staged files are missing/stale and MESHOPTIMIZER_RELEASE_TAG ('${sha}') is not a commit SHA - set -DMESHOPTIMIZER_RELEASE_TAG=<commit sha> and make sure the machine has internet access.")
    endif()

    set(_work_dir "${CMAKE_BINARY_DIR}/_meshopt_fetch")
    file(MAKE_DIRECTORY "${_work_dir}")
    set(_zip "${_work_dir}/meshoptimizer-${sha}.zip")
    set(_url "https://github.com/zeux/meshoptimizer/archive/${sha}.zip")

    message(STATUS "meshoptimizer: downloading ${_url}")
    file(DOWNLOAD "${_url}" "${_zip}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
        SHOW_PROGRESS
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${_zip}")
        message(FATAL_ERROR "meshoptimizer: failed to download ${_url}: ${_dl_msg}")
    endif()

    set(_extract_dir "${_work_dir}/extracted")
    file(REMOVE_RECURSE "${_extract_dir}")
    file(MAKE_DIRECTORY "${_extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${_extract_dir}")

    file(GLOB _roots LIST_DIRECTORIES true "${_extract_dir}/*")
    list(LENGTH _roots _n)
    if(NOT _n EQUAL 1)
        message(FATAL_ERROR "meshoptimizer: unexpected archive layout, expected one top-level folder, found ${_n}.")
    endif()
    list(GET _roots 0 _root)

    foreach(_required "src/meshoptimizer.h" "LICENSE.md")
        if(NOT EXISTS "${_root}/${_required}")
            message(FATAL_ERROR "meshoptimizer: extracted package is missing ${_required}")
        endif()
    endforeach()

    file(REMOVE_RECURSE "${_GTE_MESHOPT_DIR}")
    file(MAKE_DIRECTORY "${_GTE_MESHOPT_DIR}")
    file(GLOB _src_files "${_root}/src/*")
    foreach(_f ${_src_files})
        if(NOT IS_DIRECTORY "${_f}")
            file(COPY "${_f}" DESTINATION "${_GTE_MESHOPT_DIR}")
        endif()
    endforeach()
    file(COPY "${_root}/LICENSE.md" DESTINATION "${_GTE_MESHOPT_DIR}")
    file(WRITE "${_GTE_MESHOPT_DIR}/.gte_fetched_ref" "${sha}")
    file(REMOVE_RECURSE "${_work_dir}")
    message(STATUS "meshoptimizer: staged '${sha}' -> ${_GTE_MESHOPT_DIR}")
endfunction()

# Lists every staged file (relative, sorted), the ref marker excluded.
function(_meshopt_list_staged out_var)
    file(GLOB_RECURSE _all RELATIVE "${_GTE_MESHOPT_DIR}" "${_GTE_MESHOPT_DIR}/*")
    list(FILTER _all EXCLUDE REGEX "(^|/)\\.gte_fetched_ref$")
    list(SORT _all)
    set(${out_var} "${_all}" PARENT_SCOPE)
endfunction()

# Fails when the staged tree differs from the pinned hash list in any way.
function(_meshopt_verify_staged)
    if(NOT EXISTS "${_GTE_MESHOPT_HASH_FILE}")
        message(FATAL_ERROR "meshoptimizer: ${_GTE_MESHOPT_HASH_FILE} is missing. Generate it with: cmake -DGTE_MESHOPT_GENERATE_HASHES=ON -P cmake/FetchMeshOptimizer.cmake")
    endif()
    include("${_GTE_MESHOPT_HASH_FILE}")

    set(_expected_paths "")
    foreach(_entry ${GTE_MESHOPT_SOURCE_HASHES})
        string(FIND "${_entry}" "=" _eq)
        string(SUBSTRING "${_entry}" 0 ${_eq} _rel)
        math(EXPR _hash_at "${_eq} + 1")
        string(SUBSTRING "${_entry}" ${_hash_at} -1 _want)
        list(APPEND _expected_paths "${_rel}")

        if(NOT EXISTS "${_GTE_MESHOPT_DIR}/${_rel}")
            message(FATAL_ERROR "meshoptimizer: listed file is missing: ${_rel}")
        endif()
        file(SHA256 "${_GTE_MESHOPT_DIR}/${_rel}" _have)
        if(NOT _have STREQUAL _want)
            message(FATAL_ERROR "meshoptimizer: SHA-256 mismatch for ${_rel}\n  expected ${_want}\n  actual   ${_have}")
        endif()
    endforeach()

    _meshopt_list_staged(_staged)
    foreach(_rel ${_staged})
        if(NOT _rel IN_LIST _expected_paths)
            message(FATAL_ERROR "meshoptimizer: staged file is not in the hash list: ${_rel}")
        endif()
    endforeach()
endfunction()

# One-shot mode: stage the pinned commit and write the hash list.
function(_meshopt_generate_hashes)
    _meshopt_download_and_stage("${MESHOPTIMIZER_RELEASE_TAG}")
    _meshopt_list_staged(_staged)
    set(_out "# Generated by: cmake -DGTE_MESHOPT_GENERATE_HASHES=ON -P cmake/FetchMeshOptimizer.cmake\n")
    string(APPEND _out "# SHA-256 of every staged meshoptimizer file at ${MESHOPTIMIZER_RELEASE_TAG}.\n")
    string(APPEND _out "set(GTE_MESHOPT_SOURCE_HASHES\n")
    foreach(_rel ${_staged})
        file(SHA256 "${_GTE_MESHOPT_DIR}/${_rel}" _h)
        string(APPEND _out "    \"${_rel}=${_h}\"\n")
    endforeach()
    string(APPEND _out ")\n")
    file(WRITE "${_GTE_MESHOPT_HASH_FILE}" "${_out}")
    message(STATUS "meshoptimizer: wrote ${_GTE_MESHOPT_HASH_FILE}")
endfunction()

if(GTE_MESHOPT_GENERATE_HASHES)
    _meshopt_generate_hashes()
    return()
endif()

function(fetch_meshoptimizer)
    set(_ref_marker "${_GTE_MESHOPT_DIR}/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_GTE_MESHOPT_DIR}/meshoptimizer.h" AND EXISTS "${_ref_marker}")
        file(READ "${_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL MESHOPTIMIZER_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT MESHOPTIMIZER_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "meshoptimizer: already present and matching ref '${MESHOPTIMIZER_RELEASE_TAG}' - skipping download.")
    else()
        _meshopt_download_and_stage("${MESHOPTIMIZER_RELEASE_TAG}")
    endif()

    _meshopt_verify_staged()

    if(NOT TARGET meshoptimizer)
        file(GLOB _sources "${_GTE_MESHOPT_DIR}/*.cpp")
        list(SORT _sources)
        add_library(meshoptimizer STATIC ${_sources})
        target_include_directories(meshoptimizer PUBLIC "${_GTE_MESHOPT_DIR}")
        # Third-party code: no warnings from this target.
        if(MSVC)
            target_compile_options(meshoptimizer PRIVATE /w)
        else()
            target_compile_options(meshoptimizer PRIVATE -w)
        endif()
    endif()
endfunction()
