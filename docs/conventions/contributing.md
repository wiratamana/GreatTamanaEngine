# Contributing rules
> The rules for engine code, in short form, and where each old rule name lives now. For anyone (human or AI) who changes code.
> Verified against code: 2026-10-30

## TL;DR
- The full rule text is in [AGENTS.md](../../AGENTS.md#coding-guidelines). A human owns it. Do not edit it.
- Only `src/Renderer/` may name Vulkan. Everything else uses `gte::fx` and `gte::rg`.
- All code lives in `namespace gte`. All resources use RAII. Use C++20.
- Keep the guard checks in ctest green. Never add to an allowlist.
- Build debug paths for the HTTP server. An AI agent must be able to check your feature without a screenshot of the whole screen.

## Rules
1. Namespace: every class and function is inside `gte`. See [coding guidelines](../../AGENTS.md#coding-guidelines).
2. RAII: acquire in the constructor, release in the destructor. No raw `new` or `delete`. No manual cleanup calls.
3. Layers: lower layers never depend on higher layers. `gte_core` never includes SDL. Only the host (`EditorHost`, `Window`, `EventTranslator`) touches SDL.
4. Vulkan boundary: no `Vk*`, `vk*`, `VK_*` or `volk.h` outside `src/Renderer/`. Not in code, includes or strings. See the [Vulkan boundary](../../AGENTS.md#vulkan-boundary-strict).
5. No raw GPU wrappers outside `src/Renderer/`: `.Native()`, `ComputePipeline`, `ImmediateSubmit`, hand-written barriers. Use `rg::OneShot`, `rg::Shader`, `rg::GpuBuffer` and the pass kinds.
6. Render features under `src/Features/<Name>/` (not `Editor/`) include only `fx::*`, ECS and Math headers.
7. Need a GPU feature the kit lacks? Add it inside `src/Renderer/` behind a Vulkan-free interface. Do not add a `friend`.
8. Rendering code uses Vulkan 1.3 dynamic rendering, synchronization2 and dynamic state. No `VkRenderPass` or `VkFramebuffer`.
9. Threads: no data races, no hidden shared state. See the [job system rules](job-system.md).
10. Cleanup: delete dead code, unused structs and leftover test code. You may edit or delete a test if you delete the code it tests.
11. Debugging: expose state over the existing HTTP server. For visual checks, add a route that returns the image of that view. See the [HTTP cheat sheet](../debugging/http-cheat-sheet.md).
12. Comments: 1 to 2 lines. Say what or why. Never write project history or name a document.

## Do and Don't
| Do | Don't |
|---|---|
| `PixelFormat`, `DescriptorSetHandle`, `UiTextureHandle` | `VkFormat`, `VkImageView` outside `src/Renderer/` |
| Fix the code when a guard check fails | Add a file to `tools/ci/*/allowlist.txt` |
| Add a deleted symbol to `removed_symbol_check/tokens.txt` | Leave a comment about a deleted class |
| Pass a ready `std::string` to `GTE_LOG_*` | Use `printf` format strings in logs |
| `// Must match layout in 'X.comp' (96 bytes).` | A 6-line comment with history and document names |

Allowed Vulkan exceptions: KTX2 `vkFormat` fields (`Assets/Ktx2*`), the SDL window and surface provider (`Window/*`, `Core/ISurfaceProvider.h`), and the ImGui Vulkan backend setup (`Editor/ImGuiEditorLayer.cpp`).

## Checks
| ctest name | Enforces |
|---|---|
| `VolkLeakCheck` | No Vulkan names outside `src/Renderer/` |
| `NativeLeakCheck` | No raw GPU wrappers outside `src/Renderer/` |
| `FeaturePurityCheck` | Features use only the kit, ECS and Math |
| `HeaderPurity` | Listed headers compile without Vulkan |
| `BlockingCallCheck` | GPU waits only in `GpuWaitService.cpp` |
| `EngineVocabularyCheck` | No feature words in `Renderer`, `Core`, `Profiling` |
| `CommentRulesSrc`, `CommentRulesTests` | In a fixed file list: no history or document names, comment blocks of 3 lines or less |

- Run them: `ctest --test-dir build -R "Check|Purity|CommentRules" --output-on-failure`.
- Full list and details: [Testing](../../TESTING.md#guard-checks).

## Old rule names
Code comments still cite old section names of `AGENTS.md`. Those sections are not in the file now. Use this table.

| Old name in comments | Read this now |
|---|---|
| `Job System` | [Job system](job-system.md) |
| `Logging` | [Logging](../debugging/logging.md) |
| `Networking` | [HTTP server](../architecture/http-server.md), [HTTP routes](../debugging/http-routes.md) |
| `Profiling` | [Profiling](../debugging/profiling.md) |
| `Entity-Component-System` | [ECS](../architecture/ecs.md) |
| `Editor Module Structure` | [Editor](../architecture/editor.md) |
| `CPU Dependency Memory Tracking`, `GPU resource memory tracking` | [Memory tracking](memory-tracking.md) |
| `ImGui Widget ID Uniqueness` | [ImGui ID uniqueness](imgui-id-uniqueness.md) |
| `Time and Playback Pause` | [Time and playback pause](time-and-playback-pause.md) |
| `Skeletal Animation Pose Resolution`, `GPU Vertex Skinning` | [Animation and physics](../architecture/animation-physics.md) |
| `Testability & Regression Safety` | [Testing](../../TESTING.md) |

## Gotchas
- `AGENTS.md` says "Application layer" for SDL. That class is gone. Today the host is `EditorHost`.
- `AGENTS.md` has no Job System, Logging or Networking section. The docs above replace them.
- A stale allowlist line fails its check. Lists only shrink.
- The comment guard covers a fixed file list in `tests/CMakeLists.txt`. Follow the rule in all files anyway.

## Related
- [Testing](../../TESTING.md)
- [Building](../../BUILDING.md)
- [Render honesty rules](render-honesty.md)
- [Writing a render feature](../guides/writing-a-render-feature.md)
