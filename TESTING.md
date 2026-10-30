# Testing
> How to run, find and add tests, and what the guard checks enforce. For anyone changing engine code.
> Verified against code: 2026-10-30

## TL;DR
- One GoogleTest binary: `build\tests\GreatTamanaEngineTests.exe`. Every `TEST` is also its own ctest test.
- The tests link the same `gte_core` library as the editor. They test the real engine code.
- Most tests are Tier 1: pure logic, no GPU, no window.
- A set of guard checks in ctest keeps the architecture rules green. Never weaken them.

## Run tests
Build first. See [Building](BUILDING.md). `GTE_BUILD_TESTS` must be ON (it is by default).

```cmd
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build -j 8 --output-on-failure
ctest --test-dir build -R RenderGraphBuilder
build\tests\GreatTamanaEngineTests.exe --gtest_filter=TimeTest.*
build\tests\GreatTamanaEngineTests.exe --gtest_list_tests
```

| Goal | Command |
|---|---|
| List all tests | `ctest --test-dir build -N` |
| Run by name (regex) | `ctest --test-dir build -R <regex>` |
| Run only guard checks | `ctest --test-dir build -R "^(VolkLeakCheck\|NativeLeakCheck\|FeaturePurityCheck\|HeaderPurity\|BlockingCallCheck\|EngineVocabularyCheck\|ProfilerVocabularyCheck(Plumbing)?\|AtmosphereCoreDecouplingCheck\|RemovedSymbolCheck(Src\|Tests)\|CommentRules(Src\|Tests))$" --output-on-failure` |
| Run one suite, no ctest | `GreatTamanaEngineTests.exe --gtest_filter=Suite.*` |

ctest labels are not used. Group tests by name regex.

## Tiers
| Tier | What | GPU needed |
|---|---|---|
| 1 | Pure logic: math, ECS, assets, profiler data, routes, panel data | No |
| 2 | Headless GPU: real Vulkan device, no window | Yes. Test skips if none |
| Machine-gated | Real PMX, VMD, STL or Sponza files | Skips if the file is missing |

- Tier 2 uses `tests/Fakes/HeadlessSurfaceProvider.h` and `tests/Fakes/HeadlessRenderGraphFixture.h`.
- Skip with `GTEST_SKIP() << fixture.SkipReason();` when the fixture is not usable.
- The test exe still needs `SDL3.dll`. The build copies it next to the exe.

## Where tests live
`tests/<Area>/` mirrors `src/<Area>/`. Find the test for `src/Assets/GtaFile.cpp` in `tests/Assets/`.

| Folder | Holds |
|---|---|
| `tests/Animation`, `Assets`, `ECS`, `Math`, `Physics`, `Scene`, `Jobs`, `Input`, `Memory`, `Encoding` | Tier 1 tests for the matching `src/` folder |
| `tests/Core`, `Game`, `Application`, `Network`, `Profiling` | Engine services, systems, HTTP routes, profiler |
| `tests/Renderer` | Pure renderer logic, render graph, headless GPU tests |
| `tests/Editor` | Panel data and editor logic. No ImGui |
| `tests/Features/<Name>/` | Tests for one render feature. Picked up by a glob |
| `tests/Fakes/` | Headless GPU fixtures, `RecordingLogSink.h` |
| `tests/Support/`, `tests/Tools/`, `tests/Build/` | Std140 helper, scanner tests and header probe, SDL link regression |

## Add a test
1. Create `tests/<Area>/<Name>Tests.cpp`. Use GoogleTest. Put code in `namespace gte`.
2. Add the path to `GTE_TEST_SOURCES` in `tests/CMakeLists.txt`.
3. For a feature test, skip step 2. Drop the file in `tests/Features/<Name>/`. The glob finds it.
4. Re-run CMake, build, then `ctest -R <YourSuite>`.

Rules:
- Tier 1 must not open a window or a GPU device.
- A GPU test must `GTEST_SKIP()` when no device is usable.
- You may edit or delete a test when you delete the code it tests. Keep the suite green.

## Guard checks
Each check is a ctest test. Most run `gte_ci_scan` over `src/` with a `tokens.txt` and an `allowlist.txt` in `tools/ci/<check>/`.

| ctest name | Enforces |
|---|---|
| `VolkLeakCheck` | `volk.h`, `Vk*`, `vk*`, `VK_*` appear only in `src/Renderer/` |
| `NativeLeakCheck` | No raw GPU wrappers (`.Native()`, `DescriptorSet`, `ComputePipeline`, `ImmediateSubmit`) outside `src/Renderer/` |
| `FeaturePurityCheck` | `src/Features/<X>/` (not `Editor/`) uses only the Feature Kit, ECS and Math |
| `HeaderPurity` | Listed headers compile with no Vulkan include path |
| `BlockingCallCheck` | Raw blocking GPU waits only in `Renderer/Vulkan/GpuWaitService.cpp` |
| `EngineVocabularyCheck` | `Renderer`, `Core`, `Profiling` use no feature words (Shadow, Cloud, Sky...) |
| `ProfilerVocabularyCheck`, `ProfilerVocabularyCheckPlumbing` | Profiler and render graph say `Primary` / `Secondary`, never `Game` / `Scene` view |
| `AtmosphereCoreDecouplingCheck` | `Core.h`, `Core.cpp`, `EditorLayer.h`, `EditorHost.cpp` never name Atmosphere |
| `RemovedSymbolCheckSrc`, `RemovedSymbolCheckTests` | Symbols in `removed_symbol_check/tokens.txt` never come back |
| `CommentRulesSrc`, `CommentRulesTests` | In a fixed file list: no history or doc names in comments, no comment block over 3 lines |

Rules:
- Never add a file to an `allowlist.txt`. Lists may only shrink. A stale entry fails the check.
- Fix the code (rename, move behind `src/Renderer/`). Do not weaken a token list.
- When you delete a symbol on purpose, add it to `removed_symbol_check/tokens.txt`.
- Each check has a short README in `tools/ci/<check>/`.

## Manual probes
Not in ctest. Each README has the exact command.

| Probe | Proves |
|---|---|
| [`gte_core_standalone_probe`](tools/ci/gte_core_standalone_probe/README.md) | `gte_core` builds without `gte_editor` |
| [`gte_core_player_link_probe`](tools/ci/gte_core_player_link_probe/README.md) | An exe links `gte_core` alone, with no SDL or ImGui |
| [`gte_project_assembly_link_probe`](tools/ci/gte_project_assembly_link_probe/README.md) | A project DLL links only against the editor exe |

## Related
- [Building](BUILDING.md)
- [Contributing rules](docs/conventions/contributing.md)
- [Documentation index](docs/README.md)
