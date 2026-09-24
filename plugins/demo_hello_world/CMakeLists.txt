# plugins/demo_hello_world/CMakeLists.txt
#
# PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md, Step 3.5 - a real,
# trivial demo plugin .dll implementing IPluginModule::GetModuleInfo() and
# NOTHING else (zero capabilities) - proving the ABI boundary works in
# complete isolation, before any real capability exists (Milestone 0).
#
# Links ONLY gte_plugin_abi (PHASE0_MASTER_STRATEGY.md's Locked Design
# Decision #2 - a plugin .dll NEVER links gte_core/gte_editor directly).

add_library(demo_hello_world SHARED
    HelloWorldPlugin.cpp
)
target_link_libraries(demo_hello_world PRIVATE gte_plugin_abi)
set_target_properties(demo_hello_world PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${GTE_PLUGIN_RUNTIME_OUTPUT_DIR}"
    # Real, discovered deviation from this phase file's own literal sketch
    # (which assumes the produced file is literally named
    # "demo_hello_world.dll") - MinGW/GCC's default SHARED-library naming
    # convention prepends "lib" to every target name (producing
    # "libdemo_hello_world.dll"), confirmed mechanically the first time this
    # phase's own tools/ci/gte_plugin_abi_handshake_probe/main.cpp tried (and
    # failed, GetLastError=126/ERROR_MOD_NOT_FOUND - actually the file simply
    # wasn't found under the name the probe hardcoded) to LoadLibraryW() a
    # hardcoded "demo_hello_world.dll" path. PluginHost itself does not care
    # about the exact filename (it scans the whole directory for any *.dll),
    # but a stable, predictable, prefix-free name is needed for the
    # standalone probe (and any future documentation/tooling) to hardcode a
    # path against - PREFIX "" strips the "lib" MinGW would otherwise add.
    PREFIX ""
)
# editor-core-separation-4 campaign, PHASE4
# (PHASE4_PLUGIN_RUNTIME_DLL_LANDMINE_DEFENSE_IN_DEPTH.md) - a plugin .dll
# target must call the DLL-specific variant (no runtime DLL staging into
# the shared plugins/ scan folder), not the original host/probe function.
gte_apply_plugin_dll_shared_crt_linkage(demo_hello_world)
