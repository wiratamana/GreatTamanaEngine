# FetchSponza.cmake
#
# Clones ONLY the "Sponza" model from the Khronos glTF-Sample-Assets repo
# (https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)
# into _reference/, next to the other local reference clones (e.g.
# _reference/pl-sky) - if it is not already there.
#
# Why a git sparse + partial clone and not a zip download like every other
# Fetch*.cmake module here: the full glTF-Sample-Assets repo (the whole
# Models/ folder) is very big, and a GitHub codeload zip always contains the
# ENTIRE branch - there is no way to ask GitHub for one sub-folder as a zip.
# A partial clone does not have this problem:
#
#   git clone --filter=blob:none --depth 1 --sparse --no-checkout <url> <dir>
#       -> downloads commit + tree objects only. NO file contents (blobs).
#   git sparse-checkout set --cone Models/Sponza
#       -> tells git "I only want this one folder in the working tree".
#   git checkout
#       -> now git downloads ONLY the blobs of Models/Sponza (on demand,
#          from the "promisor" remote). Every other model is never downloaded.
#
# Result (gitignored through /_reference/ - see .gitignore):
#
#   ${CMAKE_SOURCE_DIR}/_reference/glTF-Sample-Assets/Models/Sponza/...
#       glTF/Sponza.gltf (+ .bin + textures), glTF-Binary/, glTF-Draco/, ...
#
# Defines (CACHE INTERNAL):
#   GTE_SPONZA_DIR   - absolute path of the Sponza folder, or "" if the fetch
#                      was skipped or failed.
#
# Needs `git` on PATH (Git for Windows). A failed fetch is only a WARNING,
# never a FATAL_ERROR - Sponza is a test asset, it must never break a
# configure of the engine itself (no network, no git, corporate proxy, ...).
#
# Windows only, matching the rest of this project's CMake right now.
#
# Tunable cache variables:
#   SPONZA_REPO_URL        - git URL to clone from.
#   SPONZA_REPO_REF        - branch or tag to clone. Default "main".
#   SPONZA_FORCE_REDOWNLOAD - Set to ON to delete the existing clone and
#                             clone again.

if(NOT WIN32)
    message(FATAL_ERROR "FetchSponza.cmake only supports Windows. Not supported on this platform.")
endif()

set(SPONZA_REPO_URL "https://github.com/KhronosGroup/glTF-Sample-Assets.git" CACHE STRING
    "git URL of the glTF-Sample-Assets repo (only Models/Sponza is ever downloaded from it).")
set(SPONZA_REPO_REF "main" CACHE STRING
    "Branch or tag of glTF-Sample-Assets to clone (e.g. main).")
option(SPONZA_FORCE_REDOWNLOAD
    "Delete the existing Sponza clone under _reference/ and clone it again."
    OFF)

# _sponza_git(<out_ok_var> <working_dir> <args...>)
# Runs one git command, shows its output on failure. Sets <out_ok_var> to
# TRUE/FALSE in the caller's scope.
function(_sponza_git out_ok working_dir)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c core.longpaths=true ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(_rc EQUAL 0)
        set(${out_ok} TRUE PARENT_SCOPE)
    else()
        message(WARNING "Sponza: 'git ${ARGN}' failed (exit code ${_rc}).\n${_out}\n${_err}")
        set(${out_ok} FALSE PARENT_SCOPE)
    endif()
endfunction()

# fetch_sponza()
#
# Ensures _reference/glTF-Sample-Assets/Models/Sponza exists (sparse clone,
# Sponza only). Does nothing if it is already there.
function(fetch_sponza)
    set(_repo_dir    "${CMAKE_SOURCE_DIR}/_reference/glTF-Sample-Assets")
    set(_sponza_dir  "${_repo_dir}/Models/Sponza")
    set(_marker      "${_sponza_dir}/glTF/Sponza.gltf")

    if(NOT SPONZA_FORCE_REDOWNLOAD AND EXISTS "${_marker}")
        message(STATUS "Sponza: already present (${_marker} found) - skipping clone.")
        set(GTE_SPONZA_DIR "${_sponza_dir}" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(WARNING "Sponza: 'git' was not found on PATH - cannot clone Sponza. "
                        "Install Git for Windows, or clone by hand (see cmake/FetchSponza.cmake). Skipping.")
        set(GTE_SPONZA_DIR "" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()

    # Never let git stop and wait for a password prompt inside a configure.
    set(ENV{GIT_TERMINAL_PROMPT} "0")

    # Start clean: this handles SPONZA_FORCE_REDOWNLOAD and also a half-done
    # clone left behind by an earlier interrupted configure (.git exists but
    # the marker file does not).
    if(EXISTS "${_repo_dir}")
        message(STATUS "Sponza: removing old/incomplete clone ${_repo_dir}")
        file(REMOVE_RECURSE "${_repo_dir}")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/_reference")

    message(STATUS "Sponza: sparse clone of ${SPONZA_REPO_URL} (${SPONZA_REPO_REF}) - Models/Sponza ONLY")

    _sponza_git(_ok "${CMAKE_SOURCE_DIR}/_reference"
        clone --filter=blob:none --depth 1 --sparse --no-checkout
        --branch "${SPONZA_REPO_REF}"
        "${SPONZA_REPO_URL}" "${_repo_dir}")
    if(NOT _ok)
        file(REMOVE_RECURSE "${_repo_dir}")
        set(GTE_SPONZA_DIR "" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()

    _sponza_git(_ok "${_repo_dir}" sparse-checkout set --cone Models/Sponza)
    if(NOT _ok)
        file(REMOVE_RECURSE "${_repo_dir}")
        set(GTE_SPONZA_DIR "" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()

    message(STATUS "Sponza: downloading Sponza files (this can take a while the first time)...")
    _sponza_git(_ok "${_repo_dir}" checkout)
    if(NOT _ok OR NOT EXISTS "${_marker}")
        message(WARNING "Sponza: checkout finished but ${_marker} is missing. "
                        "The upstream folder layout may have changed. Removing the incomplete clone.")
        file(REMOVE_RECURSE "${_repo_dir}")
        set(GTE_SPONZA_DIR "" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()

    message(STATUS "Sponza: ready -> ${_sponza_dir}")
    set(GTE_SPONZA_DIR "${_sponza_dir}" CACHE INTERNAL "Absolute path of the Sponza model folder")
endfunction()
