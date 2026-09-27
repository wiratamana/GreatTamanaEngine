// tools/ci/gte_project_assembly_link_probe/probe_module.cpp
//
// editor-core-separation-11 campaign, PHASE2 - see main.cpp's own header
// comment for the full rationale. Stands in for a Project Assembly's own
// _Game.dll/_Editor.dll: links ONLY against gte_project_assembly_probe_host
// (mirroring LDD2's real "link GreatTamanaEditor.exe only" rule, never
// gte_core/gte_editor directly), never re-linking imgui's own compiled
// library a second time - it gets imgui's headers via a plain, headers-only
// target_include_directories() call against imgui's own
// INTERFACE_INCLUDE_DIRECTORIES (see this project's own CMakeLists.txt for
// the exact working recipe - PHASE7 reuses it verbatim), and calls into the
// REAL ImGui entirely through symbols resolved back into the host .exe at
// OS-loader time.

#include <imgui.h>

extern int g_probeCounter; // unresolved reference - must resolve back into
                            // main.cpp's own exported (explicit dllexport)
                            // global, never a private copy.

extern int HostPlainAdd(int a, int b); // unresolved reference into a
                                        // DELIBERATELY non-annotated host
                                        // symbol (no explicit dllexport) -
                                        // mirrors a real, ordinary gte_core
                                        // function/method. See main.cpp's own
                                        // comment on this symbol for why it
                                        // exists (Step 4's escape hatch).

extern "C" __declspec(dllexport) void ProbeEntry() {
    g_probeCounter += 1;
}

extern "C" __declspec(dllexport) int ProbeAddViaHost(int a, int b) {
    return HostPlainAdd(a, b);
}

extern "C" __declspec(dllexport) ImGuiContext* GetModuleImGuiContext() {
    return ImGui::GetCurrentContext();
}
