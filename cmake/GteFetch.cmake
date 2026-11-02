# Shared helpers for the cmake/Fetch*.cmake modules.
#
# Every dependency is pinned to a tag or commit (GTE_<NAME>_REF cache variables).
# Files are staged under the source tree (gitignored) with a ".gte_fetched_ref"
# stamp next to them. A configure skips the download when the stamp matches.
#
#   gte_download(<url> <dest_file> [SHA256 <hash>])
#   gte_github_archive(<owner/repo> <ref> <out_root_var>)
#   gte_fetch_raw(<name> REPO <owner/repo> REF <ref> DIR <dir> FILES <path[=dest]>...)
#   gte_dep_present(<out_var> NAME <n> REF <ref> DIR <dir> REQUIRES <files...>)
#   gte_dep_stamp(DIR <dir> REF <ref> [REF_FILE <name>])
#   gte_replace_in_file(<file> <from> <to>)
#
# Set -DGTE_FORCE_REDOWNLOAD=ON to refetch everything on the next configure.

include_guard(GLOBAL)

if(NOT WIN32)
    message(FATAL_ERROR "The cmake/Fetch*.cmake modules only support Windows.")
endif()

option(GTE_FORCE_REDOWNLOAD "Refetch every third-party dependency on the next configure" OFF)
set(GTE_FETCH_RETRIES 3 CACHE STRING "Download attempts per file before configure fails")
mark_as_advanced(GTE_FETCH_RETRIES)

set(_GTE_FETCH_WORK_DIR "${CMAKE_BINARY_DIR}/_gte_fetch")

# Downloads one file. Retries, then fails with the last error. An empty file counts as a failure.
function(gte_download url dest_file)
    cmake_parse_arguments(A "" "SHA256" "" ${ARGN})
    get_filename_component(_dir "${dest_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")

    set(_hash_args "")
    if(A_SHA256)
        set(_hash_args EXPECTED_HASH SHA256=${A_SHA256})
    endif()

    set(_error "unknown error")
    foreach(_attempt RANGE 1 ${GTE_FETCH_RETRIES})
        message(STATUS "Downloading ${url}")
        file(DOWNLOAD "${url}" "${dest_file}"
            STATUS _status
            TLS_VERIFY ON
            TIMEOUT 900
            INACTIVITY_TIMEOUT 60
            HTTPHEADER "User-Agent: GreatTamanaEngine-CMake"
            ${_hash_args})
        list(GET _status 0 _code)
        list(GET _status 1 _error)
        if(_code EQUAL 0)
            file(SIZE "${dest_file}" _size)
            file(READ "${dest_file}" _head LIMIT 4)
            if(_size LESS 64)
                set(_error "downloaded file is too small (${_size} bytes)")
            elseif(_head STREQUAL "404:")
                set(_error "server returned a '404: Not Found' page")
            else()
                return()
            endif()
        endif()
        file(REMOVE "${dest_file}")
        message(STATUS "Attempt ${_attempt}/${GTE_FETCH_RETRIES} failed: ${_error}")
        if(_attempt LESS GTE_FETCH_RETRIES)
            execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 3)
        endif()
    endforeach()
    message(FATAL_ERROR "Download failed: ${url}\n  ${_error}\n"
        "Check the internet connection and proxy, or set -DGTE_FETCH_RETRIES=<n>.")
endfunction()

# Downloads https://github.com/<owner/repo>/archive/<ref>.zip, extracts it and returns the
# archive's top-level folder in <out_root_var>. <ref> can be a tag, a branch or a commit SHA.
function(gte_github_archive repo ref out_root_var)
    get_filename_component(_name "${repo}" NAME)
    set(_dir "${_GTE_FETCH_WORK_DIR}/${_name}")
    file(REMOVE_RECURSE "${_dir}")
    gte_download("https://github.com/${repo}/archive/${ref}.zip" "${_dir}/archive.zip")
    gte_extract_zip("${_dir}/archive.zip" "${_dir}/extracted" _root)
    set(${out_root_var} "${_root}" PARENT_SCOPE)
endfunction()

# Extracts a zip. Returns the single top-level folder, or the extract dir if there are several entries.
function(gte_extract_zip zip_file extract_dir out_root_var)
    file(REMOVE_RECURSE "${extract_dir}")
    file(MAKE_DIRECTORY "${extract_dir}")
    file(ARCHIVE_EXTRACT INPUT "${zip_file}" DESTINATION "${extract_dir}")
    file(GLOB _entries LIST_DIRECTORIES true "${extract_dir}/*")
    list(LENGTH _entries _count)
    if(_count EQUAL 1 AND IS_DIRECTORY "${_entries}")
        set(${out_root_var} "${_entries}" PARENT_SCOPE)
    else()
        set(${out_root_var} "${extract_dir}" PARENT_SCOPE)
    endif()
endfunction()

# Sets <out_var> to TRUE when the files in REQUIRES exist and the stamp in DIR matches REF.
# A copy without a stamp is adopted as is. LEGACY_REFS lists stamps from older versions of
# these modules that are known to hold the same files as REF.
function(gte_dep_present out_var)
    cmake_parse_arguments(A "" "NAME;REF;DIR;REF_FILE" "REQUIRES;LEGACY_REFS" ${ARGN})
    set(${out_var} FALSE PARENT_SCOPE)
    if(GTE_FORCE_REDOWNLOAD)
        return()
    endif()
    foreach(_file ${A_REQUIRES})
        if(NOT EXISTS "${_file}")
            return()
        endif()
    endforeach()

    if(NOT A_REF_FILE)
        set(A_REF_FILE ".gte_fetched_ref")
    endif()
    set(_stamp "${A_DIR}/${A_REF_FILE}")
    if(EXISTS "${_stamp}")
        file(READ "${_stamp}" _have)
        string(STRIP "${_have}" _have)
        if(NOT _have STREQUAL A_REF)
            if(_have IN_LIST A_LEGACY_REFS)
                file(WRITE "${_stamp}" "${A_REF}")
            else()
                message(STATUS "${A_NAME}: staged ref '${_have}' differs from '${A_REF}'")
                return()
            endif()
        endif()
    else()
        file(WRITE "${_stamp}" "${A_REF}")
    endif()
    message(STATUS "${A_NAME} ${A_REF}: already present")
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

# Records the staged ref and removes the temporary download folder.
function(gte_dep_stamp)
    cmake_parse_arguments(A "" "DIR;REF;REF_FILE" "" ${ARGN})
    if(NOT A_REF_FILE)
        set(A_REF_FILE ".gte_fetched_ref")
    endif()
    file(MAKE_DIRECTORY "${A_DIR}")
    file(WRITE "${A_DIR}/${A_REF_FILE}" "${A_REF}")
    file(REMOVE_RECURSE "${_GTE_FETCH_WORK_DIR}")
endfunction()

# Fails when an expected file is missing. Use it on a freshly extracted archive.
function(gte_require_files root)
    foreach(_rel ${ARGN})
        if(NOT EXISTS "${root}/${_rel}")
            message(FATAL_ERROR "Unexpected archive layout: '${_rel}' not found in ${root}")
        endif()
    endforeach()
endfunction()

# Stages single files from raw.githubusercontent.com. FILES entries are "<repo path>" or
# "<repo path>=<path under DIR>". Optional: REF_FILE <stamp name>, LEGACY_REFS <ref>...
function(gte_fetch_raw name)
    cmake_parse_arguments(A "" "REPO;REF;DIR;REF_FILE" "FILES;LEGACY_REFS" ${ARGN})

    set(_required "")
    foreach(_entry ${A_FILES})
        string(REGEX REPLACE "^[^=]*=" "" _dest "${_entry}")
        list(APPEND _required "${A_DIR}/${_dest}")
    endforeach()

    gte_dep_present(_present NAME "${name}" REF "${A_REF}" DIR "${A_DIR}"
        REF_FILE "${A_REF_FILE}" REQUIRES ${_required} LEGACY_REFS ${A_LEGACY_REFS})
    if(_present)
        return()
    endif()

    foreach(_entry ${A_FILES})
        string(REGEX REPLACE "=.*$" "" _path "${_entry}")
        string(REGEX REPLACE "^[^=]*=" "" _dest "${_entry}")
        gte_download("https://raw.githubusercontent.com/${A_REPO}/${A_REF}/${_path}" "${A_DIR}/${_dest}")
    endforeach()
    gte_dep_stamp(DIR "${A_DIR}" REF "${A_REF}" REF_FILE "${A_REF_FILE}")
endfunction()

# Replaces <from> with <to> in a staged file. Fails if <from> is missing, because the
# pinned ref no longer matches the patch.
function(gte_replace_in_file file from to)
    file(READ "${file}" _text)
    string(FIND "${_text}" "${from}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR "Patch does not match ${file}. The pinned ref changed; update the patch.")
    endif()
    string(REPLACE "${from}" "${to}" _text "${_text}")
    file(WRITE "${file}" "${_text}")
endfunction()
