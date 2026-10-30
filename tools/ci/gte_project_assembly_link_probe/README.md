# Project assembly link probe
> A manual check. A project DLL must link against the running exe only. It must share the exe's globals and its one ImGui context.

This repo has no CI pipeline. Run this by hand. It is a standalone CMake project, not a wrapper around the root `CMakeLists.txt`.

## What it proves

| Claim | Check in `main.cpp` |
|---|---|
| A DLL reaches an exe-side global, not a private copy | `g_probeCounter` is `43` after `ProbeEntry()` |
| A plain, un-annotated exe function resolves from the DLL | `ProbeAddViaHost(2, 3) == 5` |
| The DLL sees the exe's single ImGui context | `GetHostImGuiContext() == GetModuleImGuiContext()` |

The exit code is the result. `0` means every check passed.

## Run

From the repo root:

```cmd
cmake -S tools/ci/gte_project_assembly_link_probe -B build-project-assembly-link-probe -G Ninja
cmake --build build-project-assembly-link-probe
build-project-assembly-link-probe\gte_project_assembly_probe_host.exe
```

The first configure needs internet. It fetches its own copy of SDL3, Vulkan headers, volk and ImGui into this folder. That is expected. The copies are git-ignored.

## How it works

| File | Role |
|---|---|
| `main.cpp` | Stands in for `GreatTamanaEditor.exe`. Exports a global and a function, makes an ImGui context, loads `probe_module.dll`. |
| `probe_module.cpp` | Stands in for a project `_Game.dll` or `_Editor.dll`. Has unresolved references into the exe. |

The recipe it proves, used by the real `GreatTamanaEditor`:

- `ENABLE_EXPORTS ON` on the exe, so the DLL links against the exe import library.
- `-Wl,--export-all-symbols` on the exe. The CMake property `WINDOWS_EXPORT_ALL_SYMBOLS` does nothing on MinGW.
- The DLL gets ImGui headers through `$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>`. It never links `imgui`.

## Reproduce the failure case

Use a separate scratch build folder. Delete it after.

```cmd
cmake -S tools/ci/gte_project_assembly_link_probe -B build-project-assembly-link-probe-scratch -G Ninja -DGTE_PROBE_EXPORT_ALL_SYMBOLS=OFF
cmake --build build-project-assembly-link-probe-scratch
```

Expected: `probe_module.dll` fails to link.

## Related

[Project assembly guide](../../../docs/guides/project-assembly.md)
