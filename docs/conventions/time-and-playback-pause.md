# Time and playback pause
> Rules for the frame clock (`gte::Time`) and the Pause / Resume / Step toolbar. For anyone reading time or touching `Game::Update`.
> Verified against code: 2026-10-31

## TL;DR
- One `Time` object lives in `EngineContext`, owned by `Core`. It is not a singleton.
- `Core::Update` calls `Time::Advance` once per frame, paused or not.
- Pause freezes the simulation, not the frame. Rendering and the editor keep running.
- Step is a fixed 1/60 s tick. Resume after a long pause is also one 1/60 s step.
- The toggles live in the editor. No HTTP route pauses or steps directly. `/frame_debugger/enable` pauses as a side effect and `value=false` resumes.

## Time API
| Call | Value |
|---|---|
| `DeltaTime()` | Real delta while running. `1/60 s` on a Step frame and on the first frame after a resume. `0` while frozen. |
| `UnscaledDeltaTime()` | Real seconds since last frame. Ignores pause. |
| `SimulatedTimeSeconds()` | Sum of all past `DeltaTime()`. Stops while frozen. |
| `TimeSinceStartupSeconds()` | Real seconds since first frame. Never frozen. |
| `FrameCount()` | +1 on every frame, frozen frames too. |
| `IsPaused()` | Raw pause state this frame. |
| `IsFrozenThisFrame()` | Paused and not stepping. The flag `Game::Update` branches on. |
| `IsSteppedThisFrame()` | A Step was honoured this frame. Implies paused. |

## Rules
1. `Advance()` is the only writer. Everything else reads. It is called only from `Core::Update`.
2. Pass `Time` by `const&` through `EngineContext`. Do not copy it around or cache deltas across frames.
3. `Game::Update` runs every frame. When `IsFrozenThisFrame()` is true it skips `EvaluatePoses`, `PhysicsSystem::Update` and `SkinAndUpload`. It calls `ClearGpuSkinningDispatchThisFrame()` instead.
4. Never skip `Game::Render`, the render graph or `Present` on pause. A frozen frame still needs drawing.
5. A Step is ignored unless paused. `Advance` enforces this.
6. `kFixedStepSeconds` is `1.0 / 60.0` in `src/Core/Core.cpp`. It never comes from wall-clock time.
7. The first `Advance` after a pause uses `kFixedStepSeconds`, not the real gap. No catch-up burst.
8. `EngineContext` has one field today: `Time time`. Add a field only for a real need.
9. Pause intent (`playbackPaused`, step request) lives in `EditorContext`. Only `EditorHost` reads it and passes plain booleans in `InputFrame`.
10. Read the step request every frame (`TryConsumeStepRequest`). A stale step must never linger.
11. The Scene camera uses raw mouse deltas, not `Time`. Do not add a `Time` dependency to `EditorCamera`.

## Flow
```cpp
// EditorHost::Run()
const bool paused  = m_editorLayer->IsPlaybackPaused();
const bool stepRaw = m_editorLayer->TryConsumeStepRequest();   // every frame
m_core.Update(inputFrame, deltaSeconds);                        // inputFrame carries both flags

// Core::Update()
const bool stepped = input.playbackPaused && input.stepRequested;
m_engineContext.time.Advance(deltaTime, input.playbackPaused, stepped, kFixedStepSeconds);
m_game.Update(m_engineContext, *input.inputState);
```

## Do and Don't
| Do | Don't |
|---|---|
| Use `DeltaTime()` for simulation | Use `UnscaledDeltaTime()` for simulation |
| Use `UnscaledDeltaTime()` for UI and smoothing that must run while paused | Gate rendering on `IsPaused()` |
| Branch on `IsFrozenThisFrame()` | Branch on `IsPaused()` to decide whether to simulate (a Step frame is paused too) |
| Test `Time` with hand-built `Advance` calls | Read the system clock inside simulation code |

## Toolbar
- `BuildPlaybackToolbar` (`src/Editor/PlaybackControls.cpp`) draws a strip under the menu bar.
- One button toggles `Pause` and `Resume`. The `Step` button is disabled unless paused.
- Enabling the Frame Debugger also sets `playbackPaused`. Disabling it resumes, unless the user had paused first. Clicking Resume turns the Frame Debugger off. See [Frame debugger](../debugging/frame-debugger.md).

## Checks
| Test | Proves |
|---|---|
| `tests/Core/TimeTests.cpp` | `Advance` maths for paused, stepped and resumed frames |
| `tests/Game/GameUpdateFreezeGatingTests.cpp` | `Game::Update` skips animation, physics and skinning when frozen |

Both are Tier 1. They need no GPU. Do not loosen them without knowing why they failed.

## Gotchas
- No `timeScale`. Pause is binary.
- Pause does not snapshot or revert scene state. It only stops simulation time.
- The feature kit reads `UnscaledDeltaTime()` through `fx::Frame::DeltaSeconds()`, clamped to 0.25 s. Smoothing in render features keeps running while paused.
- `NullEditorLayer` always reports not paused and no step.

## Related
- [Architecture overview](../architecture/overview.md) · [Frame debugger](../debugging/frame-debugger.md)
- [Animation and physics](../architecture/animation-physics.md) · [Job system](job-system.md)
