# Getting started
> A 30-minute path from a fresh clone to a running editor, a loaded scene, a game project and a first HTTP call.
> Verified against code: 2026-11-01

## TL;DR
- Build with MinGW-w64 and Ninja. Run `build\GreatTamanaEditor.exe`.
- Load the Sponza test scene from the File menu.
- Create a game project from the Project menu.
- The editor has an HTTP server on `127.0.0.1:8080`. Use it to read logs and capture images.

## Before you start
- A Windows PC with a Vulkan 1.3 GPU driver.
- CMake, MinGW-w64, Ninja, `glslc` and `git` on `PATH`. See [Building](../BUILDING.md).
- Internet for the first configure. It downloads all dependencies.

## Steps
1. Build the editor.
   ```cmd
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```
2. Start it from the repo root.
   ```cmd
   build\GreatTamanaEditor.exe
   ```
3. Look around. Hierarchy is on the left, Inspector on the right. Scene and Game are tabs in the middle. Log, Profiler and Project are at the bottom.
4. Load a scene. Use **File > Open Sponza Scene**. It needs the Sponza model that CMake fetched into `_reference/`. If it is missing, the status line says so.
5. Move in the Scene tab. Middle mouse pans. The wheel zooms smoothly (fast scrolling speeds up a little). Right mouse looks around. Click a head of the gizmo at the top-right to look along an axis. Click `Persp` under it for isometric.
6. Pause and step. Use the toolbar under the menu bar.
7. Save the scene with **Ctrl+S**. Load it again with **Ctrl+O**.
8. Import an asset. Drag a PNG, glTF or PMX file from Windows Explorer into the Project panel. The files live in `build\Project\`.

## Make a game project
1. Use **Project > New Project...**. Type a name, for example `MyGame`. Click Create.
2. Open `Projects\MyGame\Assets\MyGameGame.cpp` in your own IDE. Put your code in `RegisterProject`.
3. Use **Project > Compile 'MyGame'**. Watch the Log panel for progress.
4. Use **Project > Open Project...** and pick `MyGame`. The editor loads the DLL.
5. After a code change, hot reload over HTTP:
   ```cmd
   curl.exe -X POST "http://127.0.0.1:8080/project_assembly/hot_reload?name=MyGame"
   ```

Do not use Compile while the project is loaded. Windows locks the DLL. Use hot reload instead. Details: [Project assembly](guides/project-assembly.md).

## First HTTP calls
The server starts with the editor. It listens on loopback only.

```cmd
curl.exe http://127.0.0.1:8080/http_hello_world
curl.exe "http://127.0.0.1:8080/get_logs?min_level=warning&limit=20"
curl.exe http://127.0.0.1:8080/get_blocking_errors
curl.exe -o game.png http://127.0.0.1:8080/get_game_view
curl.exe http://127.0.0.1:8080/profiler
```

| Call | Result |
|---|---|
| `/http_hello_world` | Plain text. Proves the server is alive |
| `/get_logs` | Log entries as JSON. Filter with `min_level`, `category`, `keyword`, `limit` |
| `/get_blocking_errors` | Only the entries that block. Empty means all is fine |
| `/get_game_view` | A PNG of the Game view. The Game tab must be visible, unless the Frame Debugger is enabled |
| `/profiler` | Frame time, verdict and checks as JSON |

More routes and recipes: [HTTP cheat sheet](debugging/http-cheat-sheet.md).

## Check it worked
- The Game tab shows the scene. `game.png` opens as a picture.
- `/get_blocking_errors` returns no entries.
- The Log panel shows no red lines.

## Troubleshooting
| Symptom | Cause | Fix |
|---|---|---|
| `Connection refused` on 8080 | Editor not running, or port taken | Start one editor only |
| `/get_game_view` returns 409 | Game tab is hidden and the Frame Debugger is off | Click the Game tab |
| Open Sponza fails | Model not fetched | Re-configure with network and `git`. See [Building](../BUILDING.md) |
| Project stays "not compiled" | Compile not run or failed | Run Compile, read the Log panel |
| Status 503 or 504 | Main thread busy or stalled | Wait, retry once. See [HTTP cheat sheet](debugging/http-cheat-sheet.md) |

More fixes: [Troubleshooting](troubleshooting.md).

## Related
- [Documentation index](README.md)
- [Architecture overview](architecture/overview.md)
- [HTTP cheat sheet](debugging/http-cheat-sheet.md)
- [Project assembly](guides/project-assembly.md)
- [Contributing rules](conventions/contributing.md)
