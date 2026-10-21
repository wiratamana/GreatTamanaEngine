# Fetches MikkTSpace (mikktspace.h/.c, zlib license) from github.com/mmikk/MikkTSpace into
# third_party/mikktspace and defines the STATIC C library target `mikktspace`.
#
# MIKKTSPACE_RELEASE_TAG    pinned commit SHA to fetch (never a moving branch).
# MIKKTSPACE_FORCE_REDOWNLOAD  re-fetch even if the staged ref matches.

if(NOT WIN32)
    message(FATAL_ERROR "FetchMikkTSpace.cmake only supports Windows. Not supported on this platform.")
endif()

set(MIKKTSPACE_RELEASE_TAG "3e895b49d05ea07e4c2133156cfa94369e19e409" CACHE STRING
    "MikkTSpace commit SHA to fetch mikktspace.h/.c at (pinned commit, not a branch).")
option(MIKKTSPACE_FORCE_REDOWNLOAD
    "Force re-downloading MikkTSpace even if the staged ref matches MIKKTSPACE_RELEASE_TAG." OFF)

# Downloads one raw file and checks that its start contains <content_marker>.
function(_mikktspace_download_file url dest_file content_marker)
    message(STATUS "MikkTSpace: downloading ${url}")
    file(DOWNLOAD "${url}" "${dest_file}"
        HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
        STATUS _dl_status
        TLS_VERIFY ON
    )
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        list(GET _dl_status 1 _dl_msg)
        file(REMOVE "${dest_file}")
        message(FATAL_ERROR "MikkTSpace: failed to download ${url}: ${_dl_msg}")
    endif()

    file(READ "${dest_file}" _content LIMIT 2048)
    string(FIND "${_content}" "${content_marker}" _found)
    if(_found EQUAL -1)
        file(REMOVE "${dest_file}")
        message(FATAL_ERROR "MikkTSpace: downloaded content from ${url} does not look right (missing '${content_marker}') - is the ref valid?")
    endif()
endfunction()

function(fetch_mikktspace)
    set(_dest_dir "${CMAKE_SOURCE_DIR}/third_party/mikktspace")
    set(_h "${_dest_dir}/mikktspace.h")
    set(_c "${_dest_dir}/mikktspace.c")
    set(_ref_marker "${_dest_dir}/.gte_fetched_ref")

    set(_already_staged FALSE)
    if(EXISTS "${_h}" AND EXISTS "${_c}" AND EXISTS "${_ref_marker}")
        file(READ "${_ref_marker}" _staged_ref)
        string(STRIP "${_staged_ref}" _staged_ref)
        if(_staged_ref STREQUAL MIKKTSPACE_RELEASE_TAG)
            set(_already_staged TRUE)
        endif()
    endif()

    if(NOT MIKKTSPACE_FORCE_REDOWNLOAD AND _already_staged)
        message(STATUS "MikkTSpace: already present and matching ref '${MIKKTSPACE_RELEASE_TAG}' - skipping download.")
    else()
        if(NOT MIKKTSPACE_RELEASE_TAG MATCHES "^[0-9a-f]+$")
            message(FATAL_ERROR "MikkTSpace: staged files are missing/stale and MIKKTSPACE_RELEASE_TAG ('${MIKKTSPACE_RELEASE_TAG}') is not a commit SHA - set -DMIKKTSPACE_RELEASE_TAG=<commit sha> and make sure the machine has internet access.")
        endif()
        file(MAKE_DIRECTORY "${_dest_dir}")
        set(_base "https://raw.githubusercontent.com/mmikk/MikkTSpace/${MIKKTSPACE_RELEASE_TAG}")
        _mikktspace_download_file("${_base}/mikktspace.h" "${_h}" "mikktspace")
        _mikktspace_download_file("${_base}/mikktspace.c" "${_c}" "mikktspace")
        file(WRITE "${_ref_marker}" "${MIKKTSPACE_RELEASE_TAG}")
        message(STATUS "MikkTSpace: staged '${MIKKTSPACE_RELEASE_TAG}' -> ${_dest_dir}")
    endif()

    if(NOT TARGET mikktspace)
        add_library(mikktspace STATIC "${_c}" "${_h}")
        set_target_properties(mikktspace PROPERTIES LINKER_LANGUAGE C)
        target_include_directories(mikktspace PUBLIC "${_dest_dir}")
        # Third-party code: no warnings from this target.
        if(MSVC)
            target_compile_options(mikktspace PRIVATE /w)
        else()
            target_compile_options(mikktspace PRIVATE -w)
        endif()
    endif()
endfunction()
