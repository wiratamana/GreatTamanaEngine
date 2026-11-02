# Sponza test model: a sparse, partial git clone of KhronosGroup/glTF-Sample-Assets that
# downloads only Models/Sponza. A zip is not possible, GitHub archives always hold the whole repo.
#
# fetch_sponza()  -> _reference/glTF-Sample-Assets/Models/Sponza
#                    sets GTE_SPONZA_DIR (CACHE INTERNAL) to that folder, or "" on failure.
#
# Needs git on PATH. A failure is only a warning: Sponza is a test asset and must never break configure.
#
# Cache: GTE_SPONZA_REPO_URL, GTE_SPONZA_REF (branch or tag). GTE_FORCE_REDOWNLOAD clones again.

include(GteFetch)

set(GTE_SPONZA_REPO_URL "https://github.com/KhronosGroup/glTF-Sample-Assets.git" CACHE STRING
    "git URL of glTF-Sample-Assets (only Models/Sponza is downloaded)")
set(GTE_SPONZA_REF "main" CACHE STRING "glTF-Sample-Assets branch or tag")

# Runs one git command. Sets <out_ok> to TRUE or FALSE. Warns with the git output on failure.
function(_sponza_git out_ok working_dir)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c core.longpaths=true ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    if(_rc EQUAL 0)
        set(${out_ok} TRUE PARENT_SCOPE)
    else()
        message(WARNING "Sponza: 'git ${ARGN}' failed (exit code ${_rc}).\n${_out}\n${_err}")
        set(${out_ok} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(fetch_sponza)
    set(_parent "${CMAKE_SOURCE_DIR}/_reference")
    set(_repo "${_parent}/glTF-Sample-Assets")
    set(_sponza "${_repo}/Models/Sponza")
    set(_marker "${_sponza}/glTF/Sponza.gltf")

    if(NOT GTE_FORCE_REDOWNLOAD AND EXISTS "${_marker}")
        message(STATUS "Sponza: already present")
        set(GTE_SPONZA_DIR "${_sponza}" CACHE INTERNAL "Absolute path of the Sponza model folder")
        return()
    endif()
    set(GTE_SPONZA_DIR "" CACHE INTERNAL "Absolute path of the Sponza model folder")

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(WARNING "Sponza: git not found on PATH, skipping. Install Git for Windows to fetch it.")
        return()
    endif()
    set(ENV{GIT_TERMINAL_PROMPT} "0")  # never wait for a password during configure

    # Remove a forced or half-finished clone first.
    file(REMOVE_RECURSE "${_repo}")
    file(MAKE_DIRECTORY "${_parent}")

    message(STATUS "Sponza: sparse clone of ${GTE_SPONZA_REPO_URL} (${GTE_SPONZA_REF}), Models/Sponza only")
    _sponza_git(_ok "${_parent}" clone --filter=blob:none --depth 1 --sparse --no-checkout
        --branch "${GTE_SPONZA_REF}" "${GTE_SPONZA_REPO_URL}" "${_repo}")
    if(_ok)
        _sponza_git(_ok "${_repo}" sparse-checkout set --cone Models/Sponza)
    endif()
    if(_ok)
        message(STATUS "Sponza: downloading files, this can take a while the first time")
        _sponza_git(_ok "${_repo}" checkout)
    endif()
    if(_ok AND NOT EXISTS "${_marker}")
        message(WARNING "Sponza: ${_marker} is missing. The upstream folder layout may have changed.")
        set(_ok FALSE)
    endif()

    if(NOT _ok)
        file(REMOVE_RECURSE "${_repo}")
        return()
    endif()
    message(STATUS "Sponza: ready at ${_sponza}")
    set(GTE_SPONZA_DIR "${_sponza}" CACHE INTERNAL "Absolute path of the Sponza model folder")
endfunction()
