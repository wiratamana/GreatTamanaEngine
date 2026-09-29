# PHASE7 — HTTP Route Update (reuse, not a new endpoint)

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 5 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`.

## Step 1: The Goal

Let an HTTP caller (an AI agent, a script, `gte_send_request`) scaffold a
Screen Post-Process Pass through the EXISTING
`POST /project_assembly/create_asset` route — no new endpoint, no new
response shape, since `CreateAssetScaffold()`'s own outcome (PHASE6) is
already generic enough to carry this new kind's dynamic reminder message
without any route-level change beyond parsing the new `kind` string.

## Step 2: The Situation

`src/Network/NetworkServer.cpp` lines 1367-1401, the ENTIRE current handler:

```cpp
server.Post("/project_assembly/create_asset",
    [assetScaffoldingCapability](const httplib::Request& req, httplib::Response& res) {
    const std::string kindParam = req.get_param_value("kind"); // "render_pass" | "compute_shader" | "shader_pair"
    const std::string name = req.get_param_value("name");
    AssetScaffoldKind kind;
    if (kindParam == "render_pass") {
        kind = AssetScaffoldKind::RenderPass;
    } else if (kindParam == "compute_shader") {
        kind = AssetScaffoldKind::ComputeShader;
    } else if (kindParam == "shader_pair") {
        kind = AssetScaffoldKind::ShaderPair;
    } else {
        res.status = 400;
        res.set_content(
            BuildGenericErrorResponseJson("'kind' must be render_pass, compute_shader, or shader_pair"),
            "application/json");
        return;
    }
    if (assetScaffoldingCapability == nullptr) {
        res.status = 503;
        res.set_content(BuildGenericErrorResponseJson("asset scaffolding capability not available"), "application/json");
        return;
    }
    const IAssetScaffoldingCapability::ScaffoldOutcome outcome =
        assetScaffoldingCapability->CreateAssetScaffold(kind, name);
    if (!outcome.success) {
        res.status = 400;
        res.set_content(BuildGenericErrorResponseJson(outcome.errorMessage), "application/json");
        return;
    }
    nlohmann::json body;
    body["created_files"] = outcome.createdFiles;
    body["reminder_message"] = outcome.reminderMessage;
    res.set_content(body.dump(), "application/json");
});
```

Everything below `AssetScaffoldKind kind;`'s resolution is ALREADY fully
generic — `outcome.reminderMessage`/`outcome.createdFiles`/`outcome.errorMessage`
are read as plain fields regardless of which kind was requested. This means
PHASE6's own dynamic reminder message (auto-wired vs. fallback wording) and
its length-check rejection (surfaced as `outcome.errorMessage`) BOTH already
flow through this exact, unmodified code path with zero further route-level
work — an HTTP-only caller gets the SAME honest signal a human using the
ImGui window gets, automatically, for free.

**A real, existing test file for this exact route already covers it — do not
assume otherwise.** `tests/Network/CreateAssetEndpointEndToEndTests.cpp`
already exists (confirmed by direct read) and already exercises
`POST /project_assembly/create_asset` end to end, including a
`InvalidKindReturns400NamingTheThreeValidValues` test that posts
`kind=not_a_real_kind` and asserts the 400 error text contains all three
CURRENT valid kind substrings (`"render_pass"`, `"compute_shader"`,
`"shader_pair"`). This is genuinely relevant, load-bearing prior work this
phase must extend, not a gap that needs a brand-new test scaffold from
scratch.

## Step 3: The Plan

Two small, purely mechanical edits to the SAME lambda body:

1. Add one more `else if` branch, immediately after the existing
   `shader_pair` branch:
   ```cpp
   } else if (kindParam == "screen_post_process_pass") {
       kind = AssetScaffoldKind::ScreenPostProcessPass;
   } else {
   ```
2. Update the 400 error message text to list all four valid kinds:
   ```cpp
   BuildGenericErrorResponseJson(
       "'kind' must be render_pass, compute_shader, shader_pair, or screen_post_process_pass")
   ```

Also update the inline comment on the `kindParam` line itself (currently
`// "render_pass" | "compute_shader" | "shader_pair"`) to include the fourth
value, so a future reader of this file doesn't need to hunt through the `if`
chain to know every accepted value:
```cpp
const std::string kindParam = req.get_param_value("kind"); // "render_pass" | "compute_shader" | "shader_pair" | "screen_post_process_pass"
```

Nothing else in this handler changes. No new JSON field, no new status code,
no new route.

### 3.1 — Required test-file update (this phase's own real testing work)

Extend the EXISTING `tests/Network/CreateAssetEndpointEndToEndTests.cpp` — do
NOT create a second, parallel test file for this same route:

1. **Rename/extend `InvalidKindReturns400NamingTheThreeValidValues`** to
   reflect four valid values, not three, going forward (e.g.
   `InvalidKindReturns400NamingTheFourValidValues`) — a stale test name that
   still says "three" after a fourth kind exists is a small but real, honest
   documentation debt. Add one more `EXPECT_NE(errorMessage.find("screen_post_process_pass"), ...)`
   assertion alongside the three pre-existing ones. The pre-existing
   assertions all use `EXPECT_NE(..., npos)` substring checks (never an exact
   full-string match), so simply extending the route's own message text (per
   3 above) does NOT break this test even before you touch it — but leaving
   its own NAME/assertion set stale would be a real, if small, regression in
   this test file's own honesty.
2. **Add a new scenario, mirroring `OneSuccessfulScaffoldPerKindWritesExpectedRealFiles`'s**
   own shape, for `kind=screen_post_process_pass` against this file's own
   existing scratch-project fixture (`m_scratchProjectDirectory`/
   `m_scratchAssetsDirectory`, which has an `Assets/` folder but NO
   `<Name>Game.cpp` file at all) — confirm: `200`, `created_files` contains
   exactly `"<Name>ScreenPass.cpp"`, the file exists on disk with the
   substituted name/priority (`0`, since this is the first Screen
   Post-Process Pass scaffolded into that scratch project), and
   `reminder_message` reports the FALLBACK wording (never the auto-wired
   wording) — because this fixture's own scratch directory has no
   `<ScratchProjectName>Game.cpp` at all, `TryAutoWireRegisterCall()` hits its
   own "file cannot be opened" branch, exactly like the real
   `Projects/ProjectAssemblyProbe/` case PHASE6's own live check exercises
   (see that phase's own Step 5 note) — a good, cheap, automated complement to
   that one manual, live check.
3. **Add a name-too-long scenario**: `POST` with a `name` long enough that
   `"<name>.ScreenTint"` exceeds 63 characters — confirm `400` and the "name
   is too long" message, and confirm no `Assets/<name>ScreenPass.cpp` was
   written.

No `tests/CMakeLists.txt` change is needed for either of these — this is an
extension of an ALREADY-registered file, not a new one.

## Step 4: Verification (this phase only)

1. Incremental build (`cmake --build build`).
2. `ctest -R CreateAssetEndpoint` (or whatever the actual registered test
   name is — confirm via `tests/CMakeLists.txt`) — all cases in the extended
   file passing, including the pre-existing scenarios (unchanged behavior)
   and this phase's own three new/renamed ones.
3. Live, HTTP-driven check: `run_app_background` the built Editor with an
   active, anchors-having project already open (create a fresh throwaway one
   if needed), then:
   - `gte_send_request` a `POST /project_assembly/create_asset?kind=screen_post_process_pass&name=<X>`
     — confirm a 200 response with `created_files`/`reminder_message`
     matching PHASE6's own behavior exactly (same wording as the ImGui path
     produces for the equivalent action).
   - `gte_send_request` the SAME request again with the SAME name — confirm a
     400, "already exists".
   - `gte_send_request` with an intentionally-too-long `name` — confirm a
     400, the "name is too long" message, naming the actual limit.
   - `gte_send_request` with an invalid `kind` (e.g. `kind=bogus`) — confirm
     the 400 error text now lists all FOUR valid kinds, not three.
   - `stop_app_background` when done; clean up the throwaway project/files
     created during this check, mirroring PHASE6's own cleanup discipline.

## Step 5: Completion

Write `PHASE7_COMPLETION_REPORT.md` in this same folder, including the exact
`gte_send_request` calls made and their exact responses, and the exact new/
renamed test names added to `CreateAssetEndpointEndToEndTests.cpp`. `git_add`
+ `git_commit` covering the code change, the extended test file, and the
report. Do not run a full build/regression here (Locked Decision 2, PHASE0)
— PHASE8 is where that happens.
