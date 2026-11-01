# Project assembly
> How to make a game project that loads into the editor as DLLs, and how that system works inside. For game developers and engine developers.
> Verified against code: 2026-11-01

## TL;DR
- A project is C++ and GLSL in `Projects/<Name>/`. CMake builds it into `<Name>_Game.dll` and, if needed, `<Name>_Editor.dll`.
- The editor loads the DLLs and calls one export: `GTE_RegisterProject`. Your code uses the real engine types. No wrapper, no engine rebuild.
- It works only on the same machine and toolchain as the engine. There is no portability.
- Hot reload exists and is HTTP only. There is no menu item for it.
- There is no gameplay script API. A project adds render passes, render features, editor windows and ECS components.

## Before you start
- Build the engine with MinGW and Ninja. See [Building](../../BUILDING.md).
- The compiler needs a shared `libstdc++-6.dll` next to it. Without it, CMake warns and project DLLs cannot share the C++ runtime with the exe.
- CMake option `GTE_ENABLE_PROJECT_ASSEMBLIES` must be `ON` (default). `OFF` means no discovery, no load, and "New Project" refuses.
- `glslc` must be on `PATH`. It compiles your shaders.
- Start the editor from the `build` folder.

Three folders look alike. Do not mix them up.

| Folder | What it holds |
|---|---|
| `Projects/<Name>/` (repo root, git-ignored) | Your project source |
| `build/project_assemblies/` | Built DLLs and your compiled shaders |
| `<exe dir>/Project/` | The asset folder of the Project panel (`.gta` files) |

## Steps
1. Create the project. Menu `Project > New Project...`, or call HTTP.
2. Open `Projects/<Name>/Assets/<Name>Game.cpp` in your IDE. Put your code in `RegisterProject`.
3. Compile. Menu `Project > Compile '<Name>'`, or call HTTP. Watch the Log panel (category `ProjectAssemblyBuild`).
4. Load. Menu `Project > Open Project...`, pick `<Name>`. The tier must be `Compiled`.
5. Change code. Reload it with HTTP hot reload. Do not use Compile on a loaded project.
6. Optional: scaffold code from the Project panel: right-click the `[Active Project]` row, then `Create`.
7. Optional: add an editor window. Put the `.cpp` under `Assets/.../Editor/`.

```cmd
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/create_project?name=MyGame"
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/debug/compile_only?name=MyGame"
curl.exe "http://127.0.0.1:8080/project_assembly/list_projects"
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/open_project?name=MyGame"
curl.exe -X POST "http://127.0.0.1:8080/project_assembly/hot_reload?name=MyGame"
```

- `compile_only` returns at once with `{"started": true}`. The build runs in the background. Watch the Log.
- `hot_reload` blocks until the cycle ends. It can take minutes. Read the progress from a second connection.

### Project tiers
| Tier | Meaning | What `Open Project` does |
|---|---|---|
| `NotAProject` | No `Libraries/CMakeLists.txt` | Refuses |
| `NotBuildable` | Has `Libraries/`, but no `.cpp` in `Assets/` yet | Marks it active. Does not load |
| `NotCompiled` | Has source, but no `_Game.dll` | Marks it active. Does not load. Compile first |
| `Compiled` | `_Game.dll` exists | Marks it active and loads it |
| `AlreadyLoaded` | The DLL is loaded | Marks it active. Use hot reload to update |

## Project layout
```text
Projects/<Name>/
  Libraries/CMakeLists.txt            one line: gte_add_project(<Name>)
  Libraries/ProjectAssemblyExports.h  copy of cmake/templates/ProjectAssemblyExports.h
  Assets/<Name>Game.cpp               stub with RegisterProject(core)
  Assets/**/*.cpp                     -> <Name>_Game.dll    (no /Editor/ in the path)
  Assets/**/Editor/**/*.cpp           -> <Name>_Editor.dll  (only if such files exist)
  Assets/**/*.vert|*.frag|*.comp      -> compiled to .spv
```
- The root `CMakeLists.txt` adds every `Projects/*/Libraries/`. A new folder triggers a CMake re-configure on the next build.
- A name must match `^[A-Za-z_][A-Za-z0-9_]*$`. Windows device names such as `CON` or `COM1` are refused.
- Target names `_Game` and `_Editor` are fixed. The host tells the DLL kind by the file name suffix only.
- Each DLL links only `GreatTamanaEditor`. Never link `gte_core`, `gte_editor` or imgui yourself. That makes a second copy of globals such as `GImGui`.

## Write a render effect
`Core` has the registration calls. All names share one global namespace. A duplicate name is refused and logged.

| Call | Use | Per-stage rule |
|---|---|---|
| `AddScreenPostProcessPass(name, cb, blend, priority)` | Effect on top of the Game view. Priority is auto when omitted | Passes use `RenderPassEvent::AfterEverything` |
| `RegisterProjectRenderFeature(name, stage, blend, priority, cb)` | Same, with an explicit stage and priority | Same event rule as above |
| `AddPreOpaquePass(name, cb, priority)` | Draw into your own view before opaque | Event `PreOpaques`. Runs once per view: put `currentView` in blackboard keys |
| `AddPostOpaquePass`, `AddPostTransparentPass` | Read the current view color and depth | Events `AfterOpaques`, `AfterTransparents`. Must `ReadTexture` color or depth |
| `RegisterProjectRenderPassProvider(name, scope, provider)` | Raw render graph pass provider | Create textures before the pass desc. List unread outputs in `frame.finalTextureOutputs` |
| `CreateRenderView`, `FindRenderViewTarget`, `ResizeRenderView` | Your own offscreen views | |

- `RegisterProjectRenderFeature` takes the stage `PostComposite` or `PreUI`. The other stages have their own calls above.
- The two feature calls share a limit: 16 features at once, and a name of at most 63 bytes. A longer name is refused, not cut.
- Blend modes: `Replace`, `AlphaOver`, `Additive`, `Multiply`, `ScreenSpaceMask`.
- Use `rg::` verbs only. Project code must not touch Vulkan. The CI checks `VolkLeakCheck` and `NativeLeakCheck` would fail.
- Shaders: reference the compiled file as `project_assemblies/shaders/<File>.<ext>.spv`, for example `Blur.comp.spv`.
- Keep shader file names unique across the engine and all projects. The compiled file name does not include the folder.

Smallest working effect (the `screen_post_process_pass` scaffold generates the same idea, with a priority argument):

```cpp
#include "../../../src/Core/Core.h"
#include "../../../src/Renderer/RenderGraph/RenderGraphTypes.h"
#include <array>

void RegisterTint(gte::Core& core)
{
    core.AddScreenPostProcessPass("MyGame.Tint",
        [](gte::rg::RenderGraphBuilder& builder, gte::rg::RenderPassBlackboard&, gte::rg::RenderViewId,
           gte::rg::TextureHandle target, gte::Extent2D, const gte::ScenePassReadHandles&,
           const gte::RenderFeatureCameraData&) {
            builder.AddRenderPass("MyGame.Tint.Clear", gte::rg::PassKind::Graphics,
                [target](gte::rg::RenderGraphBuilder::PassBuilder& pass) {
                    pass.WriteColorAttachment(target, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f });
                },
                [](gte::rg::PassContext&) {},
                gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);
        },
        gte::RenderFeatureBlendMode::AlphaOver);
}
```

Call `RegisterTint(core)` inside `RegisterProject`. Compile and open the project. The Game view gets a red tint.

### Scaffolds
Project panel, right-click `[Active Project]`, `Create`. Or `POST /project_assembly/create_asset?kind=<kind>&name=<Name>`.

| Kind | Files in `Assets/` | Wire-up |
|---|---|---|
| `render_pass` | `<Name>RenderPass.cpp` | You add `Register<Name>RenderPass(core);` by hand |
| `compute_shader` | `<Name>.comp`, `<Name>ComputePass.cpp` | You add `Register<Name>ComputePass(core);` by hand |
| `shader_pair` | `<Name>.vert`, `<Name>.frag` | Nothing to wire |
| `screen_post_process_pass` | `<Name>ScreenPass.cpp` | Automatic. Needs the two anchor comments in `<Name>Game.cpp`. Priority is highest existing + 1 |

## Write an editor panel
An `_Editor.dll` registers a window from its own entry point. It gets the host as a second argument.

```cpp
#include "../../Libraries/ProjectAssemblyExports.h"
#include "../../../../src/Editor/EditorWindow.h"
#include <imgui.h>

namespace {
class HelloWindow final : public gte::EditorWindow {
public:
    HelloWindow() : EditorWindow("Hello", gte::WindowDock::Bottom) {}

protected:
    void OnGui(gte::EditorFrame&) override { ImGui::TextUnformatted("Hello"); }
};
HelloWindow g_window;
void RegisterProject(gte::Core&, gte::EditorHost&) { gte::RegisterEditorWindow(g_window); }
} // namespace
GTE_DEFINE_PROJECT_EXPORTS_EDITOR(RegisterProject)
```

- Use `GTE_DEFINE_PROJECT_EXPORTS_GAME(fn)` for `_Game` (`void(gte::Core&)`) and `..._EDITOR(fn)` for `_Editor`. Once per DLL.
- The window gets a title bar, a close button, a tab menu and an entry in the Window menu. Call real `ImGui::*` in `OnGui()`. Your DLL shares the exe's ImGui.
- Hot reload unregisters the window by name before the DLL is unloaded. A window added by a reload floats until you dock it.

## How it works
Load and unload steps, in order:
1. At startup `EditorHost` calls `Core::LoadProjectAssemblies(<exe dir>/project_assemblies)`. It scans files that end with `_Game.dll` or `_Editor.dll`.
2. For each DLL the host resolves `GTE_RegisterProject`. A DLL without it is skipped with a warning.
3. Before the call, `ProjectAssemblyRegistrationLedger` starts recording. Every pass, feature, window and ECS component type that the DLL registers is attributed to the project.
4. On unload the ledger removes all of that, then `FreeLibrary` runs. The `_Editor` DLL goes first.

- Create (`New Project` or `create_project`) checks the name, writes the three scaffold files and runs `cmake` once, synchronously. Then it marks the project active. It does not compile.
- Compile runs `cmake --build <build> --target <Name>_Game` in a background thread, then `<Name>_Editor`. A missing `_Editor` target is fine. One build per project at a time.

## Hot reload
`POST /project_assembly/hot_reload?name=<Name>` freezes the main thread for the whole cycle. Poll `GET /project_assembly/hot_reload/status` from a second connection.

| Phase | What happens |
|---|---|
| `CapturingState` | The ECS world is saved as a scene document |
| `BackingUpBinaries` | DLLs are copied to `build/project_assemblies/.hotreload_backup/*.dll.bak` |
| `Unloading` | GPU goes idle, ledger tears down, `FreeLibrary` |
| `Compiling` | A synchronous `cmake --build` |
| `ReloadingNewCode` | Loads the new DLLs. On a failed build or load: `RollingBack` restores the backup and loads the old DLLs |
| `RestoringState` | The world is rebuilt from the saved document |

- `last_outcome` is `Success`, `RolledBack` or `CriticalFailure`. Status fields: `phase`, `project_name`, `cycle_id`, `phase_elapsed_ms`, `last_outcome`, `last_error_message`.
- `CriticalFailure` means the project is now unloaded. Fix by hand.
- The default wait is 600 s. After a timeout the request answers `503`, but the cycle may still be running.

What survives and what does not:

| Survives | Is lost |
|---|---|
| All ECS entities and their reflected components | Globals and static data in your DLL |
| Built-in and project-defined components | Objects outside the ECS, such as a panel's click counter, and GPU objects a pass owns |

Entity IDs change for all entities.

## Check it worked
```cmd
curl.exe "http://127.0.0.1:8080/project_assembly/debug/loaded_assemblies"
curl.exe "http://127.0.0.1:8080/project_assembly/debug/ledger?name=MyGame"
curl.exe "http://127.0.0.1:8080/render_graph"
```
- `dll_file_names` lists `MyGame_Game.dll`.
- The ledger lists your passes, features, panels and component types. It does not list pre-opaque and post-opaque names.
- `/render_graph` shows your features in `render_features[]` with `is_project_feature`.

## Time your code
Project DLLs get the engine's `GTE_ENABLE_*` switches (profiler, job system, network, panels) from `cmake/GteProject.cmake`. The profiler macros really record in your DLL.

```cpp
#include "../../../src/Profiling/ScopeTimer.h"

namespace my_game {
void BuildTintPass()
{
    GTE_PROFILE_FUNCTION();
    // ... work ...
}
} // namespace my_game
```

- The row is named `my_game::BuildTintPass`. Check it with `curl.exe "http://127.0.0.1:8080/profiler/scope?name=my_game::BuildTintPass"` or in the Profiler panel ("Your project code").
- One macro per line. Not for `constexpr` functions. Use `GTE_PROFILE_JOB_SCOPE("Name")` inside job bodies. Details: [Profiling](../debugging/profiling.md).

## Limits
- Single developer, same machine and toolchain. No script API, no code editor in the engine, no rename or delete of a project.
- No standalone unload. Unload happens only inside hot reload, which is HTTP only and loses non-ECS state.

## Troubleshooting
| Symptom | Cause | Fix |
|---|---|---|
| `New Project` says it cannot create | `GTE_ENABLE_PROJECT_ASSEMBLIES=OFF`, bad name, or folder exists | Check the message. Pick a valid new name |
| `Open Project` does not load | Tier is `NotCompiled` or `NotBuildable` | Compile first, then open again |
| Compile fails with a permission error | The DLL is loaded and Windows locks it | Use hot reload, not Compile |
| `_Editor` DLL not built | No `.cpp` under an `/Editor/` folder | Add one. The path segment `Editor` is case-sensitive |
| Panel or pass missing after load | Not registered in `RegisterProject` | Add the call. Check `debug/ledger` |
| Feature refused | Duplicate name, name over 63 bytes, or 16 features already in use | Rename. Check the Log |
| `hot_reload` answers `503` | A build is running, or the wait timed out | Poll `hot_reload/status` |

## Related
- [Editor](../architecture/editor.md) - panels, registry and the main loop.
- [HTTP routes (debug)](../debugging/http-routes-debug.md) - every `/project_assembly/*` route.
- [HTTP cheat sheet](../debugging/http-cheat-sheet.md) - the hot reload recipe.
- [Writing a render feature](writing-a-render-feature.md) - built-in features use the `fx::` kit instead.
- [Render graph](../architecture/render-graph.md) - the `rg::` verbs your passes use.
- [Scene serialization](../architecture/scene-serialization.md) - hot reload saves and restores the world with it.
- [Building](../../BUILDING.md) - toolchain and CMake options.
