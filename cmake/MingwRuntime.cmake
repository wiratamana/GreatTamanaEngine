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
# CMake target. A no-op when GTE_ENABLE_PLUGINS is OFF, matching that
# configuration's own fingerprint sharedRuntimeLinkage=0 value - AND a
# no-op (with a clear per-target warning) when the ACTIVE toolchain itself
# has no shared libstdc++ variant at all (GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED
# is FALSE - see this file's own top-of-file detection/comment; this is the
# real, current state of this repository's default configure as of PHASE1).
# Call this for the host executable (GreatTamanaEditor) AND for EVERY plugin
# .dll target (PHASE2/3/4's demo_hello_world/demo_render_feature/
# demo_editor_panel) AND for every standalone probe executable that ever
# loads a real plugin .dll (PHASE2's handshake probe, PHASE5's isolation
# probe, PHASE5's extended gte_core_player_link_probe) - never assume
# flipping just the host is sufficient.
# better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md,
# Landmine C) - the neutral, non-"plugin"-named rename of this function
# (`gte_apply_plugin_shared_crt_linkage` below is kept as a one-line
# deprecated forwarder to this one, so its own 3 CI-probe call sites
# - gte_core_player_link_probe, gte_plugin_abi_handshake_probe,
# gte_plugin_isolation_probe - keep compiling unmodified until PHASE4
# deletes them along with the ABI system). `GreatTamanaEditor` itself now
# calls THIS function directly (root CMakeLists.txt) - this flips the HOST
# executable to shared libgcc/libstdc++ linkage, which Project Assembly's own
# docs state is load-bearing for ANY `.dll` that links against
# `GreatTamanaEditor.exe`'s own import library, independent of whether the
# unrelated `gte_plugin_abi` system is even enabled.
function(gte_apply_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PLUGINS)
        return()
    endif()
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

# DEPRECATED - kept only as a one-line forwarder so pre-existing call sites
# (the 3 CI-probe executables - gte_core_player_link_probe,
# gte_plugin_abi_handshake_probe, gte_plugin_isolation_probe - deleted
# wholesale in PHASE4 alongside the rest of the ABI system) keep compiling
# unmodified. New call sites should use gte_apply_shared_crt_linkage()
# directly (above) - GreatTamanaEditor's own call site already does
# (better-render-pass-2 campaign, PHASE1).
function(gte_apply_plugin_shared_crt_linkage target_name)
    gte_apply_shared_crt_linkage(${target_name})
endfunction()

# editor-core-separation-4 campaign, PHASE4
# (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - the variant of
# gte_apply_plugin_shared_crt_linkage() every PLUGIN .dll TARGET must call
# instead of the original function above. Applies the SAME link-time CRT
# flip (-shared-libgcc, needed so THIS .dll's own fingerprint correctly
# reports sharedRuntimeLinkage=1 once a shared-CRT-capable toolchain is
# active) but DELIBERATELY DOES NOT call mingw_copy_runtime_dll() - a plugin
# .dll's RUNTIME_OUTPUT_DIRECTORY is ALWAYS the shared plugins/ folder
# PluginHost::LoadPlugins() scans at startup (GTE_PLUGIN_RUNTIME_OUTPUT_DIR),
# so copying libstdc++-6.dll/libgcc_s_seh-1.dll/libwinpthread-1.dll there
# would make PluginHost try to LoadLibraryW() them as if they were plugins
# (see this phase's own file for the full, confirmed failure mode this
# fixes). This is safe: the HOST executable (or standalone probe .exe) that
# actually loads this plugin .dll already stages its OWN copy of these same
# 3 runtime DLLs next to ITSELF (via the ORIGINAL, unchanged
# gte_apply_plugin_shared_crt_linkage() above, which every host/probe target
# must still call) - the Windows DLL search order includes "the directory
# the loading APPLICATION's own .exe is in" for any DLL resolved by bare
# name (no path) during another DLL's own import resolution, which is
# exactly how a plugin .dll's transitive dependency on libstdc++-6.dll etc.
# gets satisfied here, with zero redundant copy needed inside plugins/
# itself.
function(gte_apply_plugin_dll_shared_crt_linkage target_name)
    if(NOT GTE_ENABLE_PLUGINS)
        return()
    endif()
    if(NOT GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED)
        message(WARNING "gte_apply_plugin_dll_shared_crt_linkage(${target_name}): active toolchain has no shared libstdc++ variant - honest no-op, ${target_name} stays statically linked (see this file's own top-of-file comment).")
        return()
    endif()
    target_link_options(${target_name} PRIVATE -shared-libgcc)
    # Deliberately NOT calling mingw_copy_runtime_dll(${target_name}) here -
    # see this function's own doc comment above for exactly why.
endfunction()

# editor-core-separation-11 campaign (Project Assembly system), PHASE3
# (PHASE0_MASTER_STRATEGY.md, Finding D) - the variant of
# gte_apply_plugin_dll_shared_crt_linkage() every Project Assembly _Game.dll/
# _Editor.dll TARGET must call instead. Applies the exact SAME link-time CRT
# flip (-shared-libgcc) but is gated ONLY by GTE_ENABLE_PROJECT_ASSEMBLIES +
# GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED - NEVER by GTE_ENABLE_PLUGINS,
# which controls the completely separate, unrelated gte_plugin_abi system.
# Reusing gte_apply_plugin_dll_shared_crt_linkage() as-is here would silently
# leave a Project Assembly .dll CRT-mismatched (and therefore genuinely
# unsafe for the real std::string/std::vector cross-boundary traffic this
# whole system exists for) the moment a developer sets GTE_ENABLE_PLUGINS=OFF
# for the unrelated OTHER system, with a log message that never even
# mentions "Project Assembly" - a real, confirmed hazard this function
# exists specifically to close. Deliberately does NOT call
# mingw_copy_runtime_dll() here (mirrors gte_apply_plugin_dll_shared_crt_linkage()'s
# own identical reasoning): a Project Assembly .dll's own
# RUNTIME_OUTPUT_DIRECTORY (GTE_PROJECT_ASSEMBLY_OUTPUT_DIR, root
# CMakeLists.txt) is NEVER scanned by ProjectAssemblyHost for anything other
# than "*_Game.dll"/"*_Editor.dll" (PHASE5) - unlike PluginHost, which scans
# EVERY *.dll in its own folder - so copying the 3 runtime DLLs there would
# not itself break anything, but is still unnecessary: GreatTamanaEditor.exe
# already stages its own copy of these same 3 DLLs (via a direct, always-run
# mingw_copy_runtime_dll(GreatTamanaEditor) call root CMakeLists.txt adds
# unconditionally, PHASE3 Step 3.1 - closing exactly the same
# GTE_ENABLE_PLUGINS=OFF gap this function itself exists to close), and
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
