# Fails if any Vk*/vk*/VK_*/volk.h symbol appears in compiled text (comments
# stripped first) outside src/Renderer - that folder is the only module
# allowed to know this engine's graphics backend is Vulkan. ALLOWLIST_FILE
# is a pre-existing-debt ratchet: every entry is a file that still has a real
# or string-literal Vulkan reference today. Never ADD a new file to the
# allowlist to silence this check - only shrink it as files are migrated.
# Runs via `cmake -P` (ctest script mode) - CONFIGURE_DEPENDS is invalid here
# (project-configure-only), so a plain recursive GLOB is used instead.
file(GLOB_RECURSE ALL_SRC "${SRC_ROOT}/*.h" "${SRC_ROOT}/*.cpp")
list(FILTER ALL_SRC EXCLUDE REGEX "[/\\\\]Renderer[/\\\\]")
file(STRINGS "${ALLOWLIST_FILE}" ALLOWLIST)

set(FORBIDDEN "(volk\\.h|\\bVk[A-Z][A-Za-z0-9_]*\\b|\\bvk[A-Z][A-Za-z0-9_]*\\b|VK_[A-Z_]+)")
set(VIOLATIONS "")
foreach(FILE ${ALL_SRC})
    file(RELATIVE_PATH REL "${SRC_ROOT}" "${FILE}")
    file(TO_NATIVE_PATH "${REL}" REL_NATIVE)
    if(REL IN_LIST ALLOWLIST OR REL_NATIVE IN_LIST ALLOWLIST)
        continue()
    endif()
    file(READ "${FILE}" CONTENTS)
    string(REGEX REPLACE "//[^\n]*" "" CONTENTS "${CONTENTS}")
    string(REGEX REPLACE "/\\*[^*]*\\*+([^/*][^*]*\\*+)*/" "" CONTENTS "${CONTENTS}")
    if(CONTENTS MATCHES "${FORBIDDEN}")
        list(APPEND VIOLATIONS "${REL}")
    endif()
endforeach()

if(VIOLATIONS)
    message(FATAL_ERROR "Vulkan symbol(s) leaked outside src/Renderer: ${VIOLATIONS}")
endif()
