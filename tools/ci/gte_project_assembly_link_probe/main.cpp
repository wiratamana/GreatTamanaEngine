// tools/ci/gte_project_assembly_link_probe/main.cpp
//
// editor-core-separation-11 campaign, PHASE2
// (task_manager/editor-core-separation-11/PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md).
// Stands in for "GreatTamanaEditor.exe": exports (a) a plain global,
// g_probeCounter, mimicking a real gte_core singleton (e.g. Logger/
// EditorPanelRegistry), (b) a plain, DELIBERATELY un-annotated free function,
// HostPlainAdd(), mimicking a typical, never-written-with-DLL-export-in-mind
// gte_core method (this is Step 4's own "re-test with a real, non-trivial
// internal symbol" escape hatch - see CMakeLists.txt's comment on
// GTE_PROBE_WINDOWS_EXPORT_ALL_SYMBOLS for why this second symbol exists),
// and (c) a real ImGui context, mimicking gte_editor's own single GImGui
// instance. probe_module.dll (standing in for a Project Assembly's own
// _Game.dll/_Editor.dll) resolves all three straight back into THIS running
// process's own copies at OS-loader time, instead of duplicating them -
// proving LDD2 (PHASE0_MASTER_STRATEGY.md) concretely, before PHASE3 ever
// touches the real root CMakeLists.txt/GreatTamanaEditor target.

#include <windows.h>
#include <cstdio>
#include <imgui.h>

// A plain global, standing in for a real gte_core singleton (e.g. Logger/
// EditorPanelRegistry) - the ONE thing this probe exists to prove stays a
// SINGLE physical instance across the .exe/.dll boundary. Explicitly marked
// dllexport, exactly as PHASE2_ENABLE_EXPORTS_FEASIBILITY_PROBE.md's own
// Step 2 code shows.
extern "C" __declspec(dllexport) int g_probeCounter = 0;

// A second, DELIBERATELY non-annotated symbol (no explicit
// __declspec(dllexport)) - mirrors a real, ordinary gte_core function/method,
// which was never written with DLL export in mind. Exists specifically to
// test whether WINDOWS_EXPORT_ALL_SYMBOLS is genuinely load-bearing for
// TYPICAL, un-annotated engine code, independent of g_probeCounter's own
// explicit dllexport marker above (Step 4's own escape hatch: "If (b) links
// successfully anyway ... re-test with a real, non-trivial internal symbol").
int HostPlainAdd(int a, int b) {
    return a + b;
}

extern "C" __declspec(dllexport) ImGuiContext* GetHostImGuiContext() {
    return ImGui::GetCurrentContext();
}

int main() {
    ImGui::CreateContext();

    g_probeCounter = 42;

    HMODULE dll = LoadLibraryW(L"probe_module.dll");
    if (!dll) {
        fprintf(stderr, "FAIL: LoadLibraryW(probe_module.dll) failed, GetLastError()=%lu\n",
                GetLastError());
        return 1;
    }

    using EntryFn = void (*)();
    auto entry = reinterpret_cast<EntryFn>(GetProcAddress(dll, "ProbeEntry"));
    if (!entry) {
        fprintf(stderr, "FAIL: GetProcAddress(ProbeEntry) failed, GetLastError()=%lu\n",
                GetLastError());
        return 1;
    }
    entry(); // probe_module.dll mutates g_probeCounter through its OWN
             // unresolved reference, resolved back into THIS exe.

    if (g_probeCounter != 43) {
        fprintf(stderr,
                "FAIL: g_probeCounter == %d after ProbeEntry() (expected 43) - a private, "
                "duplicated copy inside the .dll would leave THIS copy untouched at 42.\n",
                g_probeCounter);
        return 1;
    }
    printf("PASS: g_probeCounter == 43 - single physical global shared across the .exe/.dll boundary.\n");

    using ProbeAddFn = int (*)(int, int);
    auto probeAdd = reinterpret_cast<ProbeAddFn>(GetProcAddress(dll, "ProbeAddViaHost"));
    if (!probeAdd) {
        fprintf(stderr, "FAIL: GetProcAddress(ProbeAddViaHost) failed, GetLastError()=%lu\n",
                GetLastError());
        return 1;
    }
    int sum = probeAdd(2, 3);
    if (sum != 5) {
        fprintf(stderr, "FAIL: ProbeAddViaHost(2, 3) == %d (expected 5) - HostPlainAdd() did not "
                        "resolve back into this exe correctly.\n",
                sum);
        return 1;
    }
    printf("PASS: ProbeAddViaHost(2, 3) == 5 - un-annotated HostPlainAdd() symbol resolved "
           "correctly across the .exe/.dll boundary.\n");

    using GetModuleCtxFn = ImGuiContext* (*)();
    auto getModuleCtx = reinterpret_cast<GetModuleCtxFn>(GetProcAddress(dll, "GetModuleImGuiContext"));
    if (!getModuleCtx) {
        fprintf(stderr, "FAIL: GetProcAddress(GetModuleImGuiContext) failed, GetLastError()=%lu\n",
                GetLastError());
        return 1;
    }

    ImGuiContext* hostCtx = GetHostImGuiContext();
    ImGuiContext* moduleCtx = getModuleCtx();
    if (hostCtx != moduleCtx) {
        fprintf(stderr,
                "FAIL: host ImGui context (%p) != module ImGui context (%p) - two separate "
                "GImGui instances in the process.\n",
                (void*)hostCtx, (void*)moduleCtx);
        return 1;
    }
    printf("PASS: host ImGui context (%p) == module ImGui context (%p) - single physical ImGui "
           "context shared across the .exe/.dll boundary.\n",
           (void*)hostCtx, (void*)moduleCtx);

    ImGui::DestroyContext();
    return 0;
}
