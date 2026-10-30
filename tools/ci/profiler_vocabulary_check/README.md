# ProfilerVocabularyCheck
> The profiler, render graph and view plumbing use neutral view names: `Primary` and `Secondary`. `Game` and `Scene` belong to the editor and the network layer.

## What it enforces

Two ctest tests. Both have an empty allowlist.

| ctest name | Scanned | Forbidden |
|---|---|---|
| `ProfilerVocabularyCheck` | `src/Profiling`, `src/Renderer/RenderGraph`, `src/Renderer/GpuTiming*`, `src/Renderer/FramePresenter*` | `*GameView*`, `*SceneView*`, bare `Game`, bare `Scene` |
| `ProfilerVocabularyCheckPlumbing` | `src/Core`, `src/Renderer`, `src/Profiling`, `Application/RenderPassViewData.h`, `Editor/EditorLayer.h`, `Editor/NullEditorLayer.cpp` | `*GameView*`, `*SceneView*` |

- Bare words match whole words, case sensitive. Comments and strings are scanned.
- `src/Core` owns the Game object, so bare words are not scanned there.
- Tokens: `tokens.txt` and `plumbing_tokens.txt`.

## Run

```cmd
ctest --test-dir build -R ProfilerVocabularyCheck --output-on-failure
```

## If it fails

Name the thing abstractly ("view", "color target"). Do not add an allowlist line.
