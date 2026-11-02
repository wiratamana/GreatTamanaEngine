# GoogleTest, built from source in-tree. Call only when GTE_BUILD_TESTS is ON.
#
# fetch_gtest()   third_party/googletest, built into build/_gtest_build
#                 targets: gtest, gtest_main, gmock, gmock_main
#                 aliases: GTest::gtest, GTest::gtest_main, GTest::gmock, GTest::gmock_main
#
# Cache: GTE_GTEST_REF (tag).

include(GteFetch)

set(GTE_GTEST_REF "v1.18.0" CACHE STRING "google/googletest tag")

function(fetch_gtest)
    set(_dir "${CMAKE_SOURCE_DIR}/third_party/googletest")
    gte_dep_present(_present NAME GoogleTest REF "${GTE_GTEST_REF}" DIR "${_dir}"
        REQUIRES "${_dir}/CMakeLists.txt" "${_dir}/googletest/CMakeLists.txt")
    if(NOT _present)
        gte_github_archive(google/googletest "${GTE_GTEST_REF}" _root)
        gte_require_files("${_root}" CMakeLists.txt googletest/CMakeLists.txt googlemock/CMakeLists.txt)
        file(REMOVE_RECURSE "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        # The trailing slash copies the contents of _root, not the folder itself.
        file(COPY "${_root}/" DESTINATION "${_dir}")
        gte_dep_stamp(DIR "${_dir}" REF "${GTE_GTEST_REF}")
    endif()

    if(NOT TARGET gtest)
        # Normal variables, visible only to this function and the GoogleTest subdirectory.
        set(gtest_force_shared_crt ON)  # match the project CRT, avoids MSVC LNK2038 mismatch
        set(BUILD_GMOCK ON)
        set(INSTALL_GTEST OFF)
        add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/_gtest_build" EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET GTest::gtest)
        add_library(GTest::gtest ALIAS gtest)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
    if(TARGET gmock AND NOT TARGET GTest::gmock)
        add_library(GTest::gmock ALIAS gmock)
        add_library(GTest::gmock_main ALIAS gmock_main)
    endif()
endfunction()
