# ProfilerVocabularyCheck

The profiler, the render graph and the view plumbing of the engine layer use neutral view names
(`Primary`, `Secondary`). The labels `Game` and `Scene` belong to `src/Editor` and `src/Network` only.

Two scans, both with an empty allowlist:

- `ProfilerVocabularyCheck`: tokens `*GameView*`, `*SceneView*`, `Game`, `Scene` (bare words are whole-word,
  case sensitive) in `src/Profiling`, `src/Renderer/RenderGraph`, `src/Renderer/GpuTiming*`,
  `src/Renderer/FramePresenter*`. Comments and strings are scanned.
- `ProfilerVocabularyCheckPlumbing`: tokens `*GameView*`, `*SceneView*` in `src/Core`, `src/Renderer`,
  `src/Profiling`, `Application/RenderPassViewData.h`, `Editor/EditorLayer.h`, `Editor/NullEditorLayer.cpp`.
  `Core` owns the Game object, so the bare words are not scanned there.

Fix a hit by naming the thing abstractly ("view", "color target"). Do not add an allowlist line.
List hits: `gte_ci_scan --root src --only Profiling/ --tokens tokens.txt --comments scan --strings scan --report`.
