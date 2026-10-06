# Fails (non-zero exit) if src/Core/Core.h, src/Core/Core.cpp, or
# src/Editor/EditorLayer.h contain the literal, case-sensitive substring
# "Atmosphere". Proves TEXTUAL decoupling only - it cannot and does not prove
# the moved feature still functions correctly.
#
# Usage: cmake -D GTE_REPO_ROOT=<repo root> -P CheckAtmosphereCoreDecoupling.cmake

if(NOT DEFINED GTE_REPO_ROOT)
    message(FATAL_ERROR "AtmosphereCoreDecouplingCheck: GTE_REPO_ROOT must be passed, e.g. -D GTE_REPO_ROOT=<repo root>")
endif()

set(GTE_FILES_TO_CHECK
    "${GTE_REPO_ROOT}/src/Core/Core.h"
    "${GTE_REPO_ROOT}/src/Core/Core.cpp"
    "${GTE_REPO_ROOT}/src/Editor/EditorLayer.h"
    "${GTE_REPO_ROOT}/src/Editor/EditorHost.cpp"
)

set(GTE_TOTAL_HITS 0)

foreach(GTE_FILE ${GTE_FILES_TO_CHECK})
    if(NOT EXISTS "${GTE_FILE}")
        message(FATAL_ERROR "AtmosphereCoreDecouplingCheck: expected file not found: ${GTE_FILE}")
    endif()

    file(STRINGS "${GTE_FILE}" GTE_MATCHING_LINES REGEX "Atmosphere")
    list(LENGTH GTE_MATCHING_LINES GTE_HIT_COUNT)

    if(GTE_HIT_COUNT GREATER 0)
        math(EXPR GTE_TOTAL_HITS "${GTE_TOTAL_HITS} + ${GTE_HIT_COUNT}")
        message(STATUS "AtmosphereCoreDecouplingCheck: FAIL - ${GTE_FILE} has ${GTE_HIT_COUNT} mention(s) of 'Atmosphere':")
        foreach(GTE_LINE ${GTE_MATCHING_LINES})
            message(STATUS "    ${GTE_LINE}")
        endforeach()
    endif()
endforeach()

if(GTE_TOTAL_HITS GREATER 0)
    message(FATAL_ERROR "AtmosphereCoreDecouplingCheck: FAIL - ${GTE_TOTAL_HITS} total mention(s) of 'Atmosphere' found above. Core.h/Core.cpp/EditorLayer.h/EditorHost.cpp must never name a concrete built-in feature again.")
else()
    message(STATUS "AtmosphereCoreDecouplingCheck: PASS - zero mentions of 'Atmosphere' in src/Core/Core.h, src/Core/Core.cpp, src/Editor/EditorLayer.h, src/Editor/EditorHost.cpp.")
endif()
