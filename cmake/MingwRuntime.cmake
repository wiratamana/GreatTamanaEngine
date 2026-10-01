# MingwRuntime.cmake
#
# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md, Step 3.4 (PHASE0_MASTER_STRATEGY.md's
# Locked Design Decision #4) - the plugin ABI's shared/DLL CRT requirement.
# A statically-linked host process and a statically-linked plugin .dll each
# get their OWN private copy of libstdc++/libgcc's heap - allocating on one
# side and freeing on the other (even indirectly, e.g. through a std::string
# or std::vector that crosses the boundary) is undefined behavior. Flipping
# to shared (DLL) CRT linkage gives every participating binary ONE process-
# wide heap instead.
#
# Mirrors cmake/FetchSDL3.cmake's own sdl3_copy_runtime_dll() shape exactly -
# see that file for the precedent this one's own mingw_copy_runtime_dll()
# copies.
#
# --- REAL, DISCOVERED DEVIATION FROM THE STRATEGY DOC (documented here, and
# in PHASE1_COMPLETION_REPORT.md) ---
# PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md's own Step 3.4 sketch calls
# `target_link_options(${target_name} PRIVATE -shared-libgcc -shared-libstdc++)`.
# `-shared-libstdc++` IS NOT A REAL GCC/G++ COMMAND-LINE OPTION - confirmed
# mechanically, on the real, installed toolchain: g++ rejects it outright
# ("unrecognized command-line option '-shared-libstdc++'; did you mean
# '-static-libstdc++'?"). The real GCC/MinGW mechanism is: `-shared-libgcc`
# IS a real, valid option, but there is no symmetric "-shared-libstdc++" -
# shared libstdc++ linkage is simply the COMPILER'S OWN DEFAULT whenever its
# own libstdc++ was itself built with `--enable-shared` (confirmed
# mechanically: a trivial iostream/exception/vector-using program compiled
# with NO special flags at all, on a `--enable-shared`-built MinGW toolchain,
# already imports libstdc++-6.dll/libgcc_s_seh-1.dll; adding
# `-static-libgcc -static-libstdc++` is what flips it to static, there is no
# flag that forces the opposite direction beyond simply not passing those two
# static flags). This file's own gte_apply_plugin_shared_crt_linkage() below
# reflects the REAL mechanism, not the strategy doc's own approximate sketch,
# per this campaign's own "real source may have drifted, always re-check"
# rule.
#
# A SECOND real, discovered blocker, requiring a real ask_questions round-trip
# during PHASE1 (see PHASE1_COMPLETION_REPORT.md's own "Deviations" section
# for the full evidence): the ONLY MinGW toolchain actually installed on this
# development machine at the time PHASE1 ran (scoop's "gcc" package, GCC
# 15.2.0) was itself configured `--disable-shared` for libstdc++/libgcc -
# it has NO shared-runtime variant AT ALL, under any flag combination, since
# the shared libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll simply
# were never built for it. A second toolchain WAS installed alongside it via
# scoop (`scoop install mingw` - mingw-builds-binaries, GCC 16.2.0,
# x86_64-posix-seh-ucrt, which DOES ship a real, working shared-runtime
# variant, confirmed mechanically the same way) specifically so this
# mechanism is real and ready - but PHASE1 deliberately does NOT switch
# CMAKE_CXX_COMPILER to it (that would force a de facto full rebuild of the
# entire existing build tree, which conflicts with this campaign's own
# "no full build except PHASE6" rule) - confirmed via `ask_questions`,
# explicitly deferred to a dedicated later decision. Below, this file
# PROBES, AT CONFIGURE TIME, whether the ACTIVE CMAKE_CXX_COMPILER actually
# has a shared libstdc++-6.dll sitting next to it (the same signal
# mingw_copy_runtime_dll() itself needs anyway) - if it does not (true for
# every normal configure of this repository today), every call to
# gte_apply_plugin_shared_crt_linkage() below becomes a clean, single,
# clearly-worded no-op warning instead of emitting flags that would either
# do nothing useful or (worse) silently produce a binary that CLAIMS shared
# linkage in its fingerprint without actually having it. This is exactly
# what Locked Design Decision #4's own "never claim shared linkage a given
# configuration didn't actually request" rule requires.
#
# Two functions:
#   mingw_copy_runtime_dll(<target>)             - the DLL-staging step alone.
#   gte_apply_plugin_shared_crt_linkage(<target>) - the REAL, whole-package
#       call every target on either side of the plugin ABI boundary must use
#       (see PHASE0_MASTER_STRATEGY.md's Locked Design Decision #4 - never
#       apply the raw link-options flip by hand to just one target and
#       assume that is enough).

get_filename_component(GTE_MINGW_TOOLCHAIN_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
if(EXISTS "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/libstdc++-6.dll")
    set(GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED TRUE CACHE INTERNAL
        "TRUE if the active CMAKE_CXX_COMPILER's own libstdc++ was built --enable-shared (a real libstdc++-6.dll sits next to it) - see MingwRuntime.cmake's own top-of-file comment.")
else()
    set(GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED FALSE CACHE INTERNAL
        "TRUE if the active CMAKE_CXX_COMPILER's own libstdc++ was built --enable-shared (a real libstdc++-6.dll sits next to it) - see MingwRuntime.cmake's own top-of-file comment.")
    message(WARNING
        "MingwRuntime.cmake: the active CXX compiler (${CMAKE_CXX_COMPILER}) has "
        "no libstdc++-6.dll next to it - it was almost certainly built with "
        "--disable-shared and CANNOT produce a shared-CRT-linked binary under "
        "ANY flag combination. gte_apply_plugin_shared_crt_linkage() will "
        "therefore be a documented, honest no-op (every target stays "
        "statically linked, and every fingerprint's sharedRuntimeLinkage field "
        "will correctly read 0) until a shared-runtime-capable toolchain is "
        "deliberately switched to build this project with. See "
        "docs/conventions/plugin-architecture.md and "
        "task_manager/editor-core-separation-3/PHASE1_COMPLETION_REPORT.md.")
endif()

# Stages libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll next to a
# built target's own binary - the exact three runtime DLLs shared CRT
# linkage makes that target depend on at runtime. Mirrors
# sdl3_copy_runtime_dll()'s own POST_BUILD copy-if-different shape exactly
# (cmake/FetchSDL3.cmake). Safe to call on ANY target kind this repo ever
# builds (an .exe or a plugin .dll alike) - $<TARGET_FILE_DIR:...> resolves
# correctly for either. Only ever called (by gte_apply_plugin_shared_crt_linkage()
# below) when GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED is TRUE - callable
# standalone too, but a no-op-with-warning is more useful than a hard
# failure if it's ever called directly against an unsupported toolchain.
function(mingw_copy_runtime_dll target_name)
    foreach(dll_name IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
        if(EXISTS "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/${dll_name}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${GTE_MINGW_TOOLCHAIN_BIN_DIR}/${dll_name}"
                    "$<TARGET_FILE_DIR:${target_name}>/${dll_name}"
                COMMENT "Staging ${dll_name} next to ${target_name}"
            )
        else()
            message(WARNING "mingw_copy_runtime_dll: ${dll_name} not found next to CMAKE_CXX_COMPILER (${GTE_MINGW_TOOLCHAIN_BIN_DIR}) - ${target_name} may fail to start on a machine without this exact MinGW toolchain installed.")
        endif()
    endforeach()
endfunction()

# Applies BOTH halves of Locked Design Decision #4 (PHASE0_MASTER_STRATEGY.md)
# to one target in a single call: the shared/DLL CRT link-time flip AND
# staging the resulting runtime DLL dependency next to that target's own
# built binary. `target_name` must already exist (add_library/
# add_executable already called for it) at the point this is invoked -
# unlike target_link_libraries()'s own forward-reference tolerance for
# LIST ITEMS, the target this command is called ON must already be a real
# CMake target. A no-op (with a clear per-target warning) when the ACTIVE
# toolchain itself has no shared libstdc++ variant at all
# (GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED is FALSE - see this file's own
# top-of-file detection/comment).
# better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md,
# Landmine C) - the neutral, non-"plugin"-named rename of this function
# (`gte_apply_plugin_shared_crt_linkage` below was kept as a one-line
# deprecated forwarder to this one during PHASE1-3, deleted outright by
# PHASE4 - PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md - once its own 3
# CI-probe call sites - gte_core_player_link_probe, gte_plugin_abi_handshake_probe,
# gte_plugin_isolation_probe - no longer existed to need it). `GreatTamanaEditor`
# itself calls THIS function directly (root CMakeLists.txt) - this flips the
# HOST executable to shared libgcc/libstdc++ linkage, which Project
# Assembly's own docs state is load-bearing for ANY `.dll` that links against
# `GreatTamanaEditor.exe`'s own import library.
#
# better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
# the `if(NOT GTE_ENABLE_PLUGINS) return()` early-out this function used to
# have (PHASE1-3) is REMOVED outright - GTE_ENABLE_PLUGINS itself no longer
# exists anywhere in this repository as of this phase. This function is no
# longer gated by ANY option - it unconditionally applies shared-CRT linkage
# to GreatTamanaEditor (its one real call site) whenever the active toolchain
# genuinely supports it (GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED). A real,
# confirmed hazard this fix closes: leaving the stale GTE_ENABLE_PLUGINS check
# in place after deleting the option() block that defined it would have made
# CMake treat the now-permanently-undefined variable as empty/falsy, so
# `NOT GTE_ENABLE_PLUGINS` would ALWAYS evaluate true - silently, permanently
# disabling shared-CRT linkage for GreatTamanaEditor itself, exactly the
# regression Landmine C (PHASE0_MASTER_STRATEGY.md Section 2.2) exists to
# prevent.
function(gte_apply_shared_crt_linkage target_name)
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_shared_crt_linkage(${target_name}): active toolchain has no shared libstdc++ variant - honest no-op, ${target_name} stays statically linked (see this file's own top-of-file comment).")
        return()
    endif()
    # -shared-libgcc IS a real, valid GCC/G++ driver option (unlike
    # "-shared-libstdc++", which does not exist - see this file's own
    # top-of-file comment). Shared libstdc++ linkage itself is simply this
    # toolchain's own default when no -static-libgcc/-static-libstdc++ flag
    # is present - explicitly NOT passing those two static flags is what
    # actually produces the shared link here.
    target_link_options(${target_name} PRIVATE -shared-libgcc)
    mingw_copy_runtime_dll(${target_name})
endfunction()

# better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
# gte_apply_plugin_shared_crt_linkage() (the deprecated one-line forwarder to
# gte_apply_shared_crt_linkage() above, kept during PHASE1-3 purely so the 3
# CI-probe call sites - gte_core_player_link_probe, gte_plugin_abi_handshake_probe,
# gte_plugin_isolation_probe - kept compiling unmodified) is DELETED outright
# this phase - all 3 call sites are gone (the latter two probes' whole folders
# deleted; the first's own call site removed, see root CMakeLists.txt).

# better-render-pass-2 campaign, PHASE4 (PHASE4_DELETE_PLUGINS_FOLDER_AND_CMAKE.md) -
# gte_apply_plugin_dll_shared_crt_linkage() (the variant every PLUGIN .dll
# TARGET used to have to call instead of gte_apply_shared_crt_linkage() above)
# is DELETED outright this phase - its only callers were the 9 demo plugin
# CMakeLists.txt files (deleted this phase, plugins/demo_*/) and
# tests/Fixtures/FakePlugins/CMakeLists.txt's 5 fixture targets (already
# deleted by PHASE3's own Step 3.5) - zero callers remain.
#
# editor-core-separation-11 campaign (Project Assembly system), PHASE3
# (PHASE0_MASTER_STRATEGY.md, Finding D) - the variant of
# gte_apply_shared_crt_linkage() every Project Assembly _Game.dll/
# _Editor.dll TARGET must call instead. Applies the exact SAME link-time CRT
# flip (-shared-libgcc) but is gated ONLY by GTE_ENABLE_PROJECT_ASSEMBLIES +
# GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED - a real, confirmed hazard this
# function exists specifically to close: reusing gte_apply_shared_crt_linkage()
# as-is here would behave identically today (both are unconditional now that
# GTE_ENABLE_PLUGINS is gone), but this stays its OWN, independent function -
# a Project Assembly .dll's own gating (GTE_ENABLE_PROJECT_ASSEMBLIES) is a
# genuinely separate concern from the host executable's own call, and a
# future reintroduction of a plugin-system-specific gate on
# gte_apply_shared_crt_linkage() must never silently also affect Project
# Assembly .dll's without a deliberate, separate decision. Deliberately does
# NOT call mingw_copy_runtime_dll() here: a Project Assembly .dll's own
# RUNTIME_OUTPUT_DIRECTORY (GTE_PROJECT_ASSEMBLY_OUTPUT_DIR, root
# CMakeLists.txt) is NEVER scanned by ProjectAssemblyHost for anything other
# than "*_Game.dll"/"*_Editor.dll" - so copying the 3 runtime DLLs there would
# not itself break anything, but is still unnecessary: GreatTamanaEditor.exe
# already stages its own copy of these same 3 DLLs (via a direct, always-run
# mingw_copy_runtime_dll(GreatTamanaEditor) call root CMakeLists.txt adds
# unconditionally, via gte_apply_shared_crt_linkage(GreatTamanaEditor)), and
# Windows' own DLL search order already includes "the directory the loading
# APPLICATION's own .exe is in" for any DLL a loaded .dll's own transitive
# dependency resolves by bare name.
function(gte_apply_project_assembly_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PROJECT_ASSEMBLIES)
        return()
    endif()
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_project_assembly_shared_crt_linkage(${target_name}): active toolchain has no shared libstdc++ variant - honest no-op, ${target_name} stays statically linked (see this file's own top-of-file comment).")
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
endfunction()
