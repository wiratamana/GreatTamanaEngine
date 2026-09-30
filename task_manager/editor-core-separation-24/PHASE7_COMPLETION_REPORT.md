# PHASE7 — Completion Report: HTTP Route Update (reuse, not a new endpoint)

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file: `PHASE7_HTTP_ROUTE_UPDATE.md`.

**Correction acknowledged and confirmed true, first**: this phase's own file
states an earlier draft of this campaign's strategy incorrectly claimed no
`NetworkServer`-route-level test file existed for
`POST /project_assembly/create_asset`. Before writing any code, `read_file`
confirmed `tests/Network/CreateAssetEndpointEndToEndTests.cpp` genuinely
already exists and already exercises this exact route end to end, including
the exact `InvalidKindReturns400NamingTheThreeValidValues` test named in the
phase file. This phase EXTENDS that real, pre-existing file — no second,
parallel test file was created for this route, per PHASE0's Locked Decision
17.

## What changed

### 1. `src/Network/NetworkServer.cpp` (lines ~1369-1387)

Two small, purely mechanical edits to the `POST /project_assembly/create_asset`
lambda body, exactly as the phase file's own Step 3 prescribes:

1. Added one more `else if` branch, immediately after the existing
   `shader_pair` branch:
   ```cpp
   } else if (kindParam == "screen_post_process_pass") {
       kind = AssetScaffoldKind::ScreenPostProcessPass;
   } else {
   ```
2. Updated the 400 error message text to list all four valid kinds:
   ```cpp
   BuildGenericErrorResponseJson(
       "'kind' must be render_pass, compute_shader, shader_pair, or screen_post_process_pass")
   ```
3. Updated the inline comment on the `kindParam` line itself to include the
   fourth value:
   ```cpp
   const std::string kindParam = req.get_param_value("kind"); // "render_pass" | "compute_shader" | "shader_pair" | "screen_post_process_pass"
   ```

Nothing else in this handler changed. No new JSON field, no new status code,
no new route — `outcome.reminderMessage`/`outcome.createdFiles`/
`outcome.errorMessage` already flow through this exact, unmodified code path
for the new kind with zero further route-level work, exactly as the phase
file's own Step 2 analysis predicted.

### 2. `tests/Network/CreateAssetEndpointEndToEndTests.cpp` (the EXISTING file, extended)

1. **Renamed** `InvalidKindReturns400NamingTheThreeValidValues` →
   `InvalidKindReturns400NamingTheFourValidValues`, and added one more
   `EXPECT_NE(errorMessage.find("screen_post_process_pass"), std::string::npos);`
   assertion alongside the three pre-existing ones. Updated the scenario's
   own header comment from "the 3 valid values" to "the 4 valid values".
2. **Added** `ScreenPostProcessPassScaffoldWritesExpectedFileWithFallbackReminder`
   — mirrors `OneSuccessfulScaffoldPerKindWritesExpectedRealFiles`'s own
   shape, against this file's own existing scratch-project fixture (an
   `Assets/` folder with NO `<Name>Game.cpp` at all). Confirms: `200`,
   `created_files == ["TintScreenPass.cpp"]`, `reminder_message` contains
   `"could not auto-wire it in"` and `"RegisterTintScreenPass(core)"` (and
   explicitly does NOT contain `"automatically wired"` — the fallback
   wording, never the auto-wired one, since `TryAutoWireRegisterCall()` hits
   its own "file cannot be opened" branch for this fixture, exactly like the
   real `Projects/ProjectAssemblyProbe/` case PHASE6's own live check
   exercised), the file exists on disk with `__NAME__`/`@@PRIORITY@@` both
   fully substituted, the correct function name/debug name, and
   `/*priority=*/0,` (the first Screen Post-Process Pass in this scratch
   project).
3. **Added** `ScreenPostProcessPassNameTooLongReturns400AndWritesNoFile` — a
   60-character name (`".ScreenTint"` is 11 chars, so 60 + 11 = 71 > 63)
   posted with `kind=screen_post_process_pass`; confirms `400`, the error
   message contains `"name is too long"`, and no
   `Assets/<name>ScreenPass.cpp` was written.

No `tests/CMakeLists.txt` change was needed — this is purely an extension of
an already-registered file, exactly as the phase file predicted.

## Verification (Step 4)

### 1. Incremental build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — succeeded, zero
errors, zero new warnings. `gte_core`/`GreatTamanaEditor.exe`/
`GreatTamanaEngineTests.exe` all relinked cleanly.

### 2. Targeted `ctest` run

`ctest -R CreateAssetEndpoint --output-on-failure` from `build/` —
**8/8 tests passed** (test IDs 115-122), 1.02s total:

- `CreateAssetEndpointEndToEndTest.NoActiveProjectReturns400WithClearMessage` — Passed
- `CreateAssetEndpointEndToEndTest.InvalidKindReturns400NamingTheFourValidValues` — Passed (renamed)
- `CreateAssetEndpointEndToEndTest.OneSuccessfulScaffoldPerKindWritesExpectedRealFiles` — Passed (pre-existing, unchanged behavior)
- `CreateAssetEndpointEndToEndTest.SameNameSameKindCalledTwiceRejectsSecondCallAndLeavesOriginalFileUntouched` — Passed
- `CreateAssetEndpointEndToEndTest.SameNameDifferentCaseCalledTwiceRejectsSecondCallCaseInsensitively` — Passed
- `CreateAssetEndpointEndToEndTest.ScreenPostProcessPassScaffoldWritesExpectedFileWithFallbackReminder` — Passed (**new**)
- `CreateAssetEndpointEndToEndTest.ScreenPostProcessPassNameTooLongReturns400AndWritesNoFile` — Passed (**new**)
- `CreateAssetEndpointNoCapabilityTests.MissingCapabilityReturns503` — Passed

### 3. Live, HTTP-driven check against a real running `GreatTamanaEditor.exe`

`run_app_background`'d the freshly-built `GreatTamanaEditor.exe` (PID 20388).

1. `GET /get_logs?limit=5` — confirmed the engine was up and logging
   normally.
2. `POST /project_assembly/create_project?name=Phase7HttpSmokeTest` (via
   `gte_send_request` with a non-empty payload to force a POST, since this
   route takes no body) —
   ```
   HTTP 200
   {"created_source_directory":"C:/Users/F5954/Documents/TAMANA/GreatTamanaEngine\\Projects\\Phase7HttpSmokeTest"}
   ```
3. `POST /project_assembly/open_project?name=Phase7HttpSmokeTest` —
   ```
   HTTP 200
   {"load_attempted":false,"load_succeeded":false,"status_message":"opened 'Phase7HttpSmokeTest' - not yet compiled, use Compile to build it"}
   ```
   (expected and harmless — this phase never needs the project to actually
   compile/run, only to be the active project so `CreateAssetScaffold()` has
   somewhere to write. This project was created AFTER PHASE2 shipped, so its
   own `Phase7HttpSmokeTestGame.cpp` has both anchor comments.)
4. `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=HttpTint` —
   ```
   HTTP 200
   {"created_files":["HttpTintScreenPass.cpp"],"reminder_message":"Created Assets/HttpTintScreenPass.cpp and automatically wired RegisterHttpTintScreenPass(core) into your project's RegisterProject() - compile to see the tint live!"}
   ```
   Confirms the same auto-wired success wording PHASE6's own ImGui-driven
   smoke test produced, now reachable via this route.
5. `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=HttpTint`
   (same name again) —
   ```
   HTTP 400
   {"error":"a file named 'HttpTintScreenPass.cpp' already exists","success":false}
   ```
6. `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=AAAA...A`
   (a 60-character name of `A`s) —
   ```
   HTTP 400
   {"error":"name is too long - \"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.ScreenTint\" would exceed the engine's 63-character render feature name limit; choose a shorter name","success":false}
   ```
7. `POST /project_assembly/create_asset?kind=bogus&name=X` —
   ```
   HTTP 400
   {"error":"'kind' must be render_pass, compute_shader, shader_pair, or screen_post_process_pass","success":false}
   ```
   Confirms the 400 error text now lists all FOUR valid kinds, not three.
8. `GET /get_logs?category=ProjectLifecycle&limit=5` — confirmed the exact
   expected log line:
   ```
   CreateAssetScaffold('HttpTint', ScreenPostProcessPass): Created Assets/HttpTintScreenPass.cpp and automatically wired RegisterHttpTintScreenPass(core) into your project's RegisterProject() - compile to see the tint live!
   ```
9. `stop_app_background`'d the Editor (PID 20388).
10. Deleted the throwaway `Projects/Phase7HttpSmokeTest/` folder entirely.
11. Re-ran `cmake -S . -B build` (required — the now-deleted project's own
    `Libraries/CMakeLists.txt` needed to drop out of the build graph at
    configure time) followed by `cmake --build build` — `ninja: no work to
    do.`, confirming a clean, fully-caught-up build tree.

## `git_status` confirmation

Only the two intended files are modified:

```
modified:   src/Network/NetworkServer.cpp
modified:   tests/Network/CreateAssetEndpointEndToEndTests.cpp
```

`Projects/` is `.gitignore`d, so the whole live smoke-test excursion
(`Phase7HttpSmokeTest/`) left zero git-visible trace.

## Step 5: Completion

`git_add` + `git_commit` cover the code change (`NetworkServer.cpp`), the
extended test file (`CreateAssetEndpointEndToEndTests.cpp`), and this report.
No full clean build / full `ctest` regression pass was run (Locked Decision
2, `PHASE0_MASTER_STRATEGY.md` — that is PHASE8's own job).

## Files touched

- `src/Network/NetworkServer.cpp`
- `tests/Network/CreateAssetEndpointEndToEndTests.cpp`
- `task_manager/editor-core-separation-24/PHASE7_COMPLETION_REPORT.md` (this file)

No file under `Projects/` was left modified from before this phase started —
the throwaway `Phase7HttpSmokeTest/` project was deleted, and the build tree
was reconfigured/rebuilt afterward to confirm a clean, fully-caught-up state
with zero dangling references.
