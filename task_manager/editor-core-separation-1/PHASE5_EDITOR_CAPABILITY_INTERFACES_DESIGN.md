# PHASE5 — Bucket B, Part 1: Design and Declare Small Capability Interfaces

## Parent
`PHASE0_MASTER_STRATEGY.md` — see "Locked Design Decision #2": MANY small,
single-purpose interfaces, never one big interface.

## Step 1: The Goal

Every "is the Editor's capability actually available" compile-time
`#if GTE_ENABLE_EDITOR` branch must become a runtime null-check against a
small, single-purpose, opaque interface — mirroring the already-proven
`FrameDebuggerCaptureContext*` shape exactly. This phase ONLY designs and
declares those interfaces (header-only additions, no call-site conversion
yet) — Phases 6-7 do the actual conversion.

## Step 2: The Situation / The Problem

Design doc Section 2.6 Bucket B names these call sites (re-confirm every one
via `search_in_dir` for `GTE_ENABLE_EDITOR`, since this list is described as
representative, not exhaustive):

- `src/Application/EngineCommandDispatch.cpp` — scene save/load, hardcoded
  `"...requires the Editor module (GTE_ENABLE_EDITOR is OFF in this
  build)"` fallback message.
- `src/Network/NetworkServer.cpp` — HTTP error status/body derived from the
  same string.
- `src/Game/Game.h` — its own "not available in this build" fallback.
- `src/Application/AssetImportCommandBridge.h` — same pattern.
- `src/Application/EditorUiCommandBridge.h` — same pattern.
- `src/Editor/GpuDrivenBatchTestSpawner.h` — same pattern (this file
  physically lives under `src/Editor/` already — confirm during execution
  whether its OWN file needs a Bucket-B-style capability check at all, or
  whether it is actually a Bucket C case since the whole file is
  Editor-only; the design doc listed it under Bucket B specifically because
  something in CORE code conditionally calls into it — find that call site
  first).
**Explicitly NOT part of Bucket B, do not touch here**: `IEditorLayer`
(`src/Editor/EditorLayer.h`) and `FrameDebuggerCaptureContext*` are two
ALREADY-DESIGNED, pre-existing opaque hooks with their own dedicated phases
(Phase 2 and Phases 12-13 respectively, per `PHASE0`'s Locked Design
Decision #8) — they are NOT new capability gaps this phase declares
interfaces for, and must never be redesigned/fragmented into smaller
Bucket-B-style pieces. This phase covers only the genuinely NEW gaps listed
above (scene IO, Editor UI commands, asset import, GPU-driven-batch test
spawning).

## Step 3: The Plan

1. For EACH call site above, read the real, current code in full first.
   Identify: (a) what CORE-side code needs to ask "can I do X", and (b)
   what EDITOR-side capability answers "yes, and here's how".
2. Declare one small interface per capability, in a new, single header
   `src/Core/EditorCapabilities.h` (all interfaces can live in one FILE for
   convenience — the point of "many small interfaces" is that each
   interface itself is single-purpose, not that they need separate files).
   Sketch (confirm/adjust exact method signatures once Step 1's real
   reading is done):
   ```cpp
   namespace gte {

   // Answers "can the current build save/load scenes via the Editor", and
   // performs the operation if so. EngineCommandDispatch.cpp and
   // NetworkServer.cpp both consult this SAME instance.
   class ISceneIOCapability {
   public:
       virtual ~ISceneIOCapability() = default;
       virtual bool SaveScene(/* whatever real params EngineCommandDispatch.cpp's current code passes */) = 0;
       virtual bool LoadScene(/* ... */) = 0;
   };

   // Answers "can the current build import assets via the Editor".
   class IAssetImportCapability {
   public:
       virtual ~IAssetImportCapability() = default;
       virtual bool ImportAsset(/* ... */) = 0;
   };

   // Answers "can the current build dispatch Editor UI commands"
   // (activate tab, list tabs, etc — whatever EditorUiCommandBridge.h
   // actually gates today).
   class IEditorUiCapability {
   public:
       virtual ~IEditorUiCapability() = default;
       virtual bool ActivateTab(/* ... */) = 0;
       // ... one method per real thing EditorUiCommandBridge.h does today
   };

   // Answers "can the current build spawn a GPU-driven batch test entity
   // set" (whatever GpuDrivenBatchTestSpawner.h's real call site needs).
   class IGpuDrivenBatchTestCapability {
   public:
       virtual ~IGpuDrivenBatchTestCapability() = default;
       virtual bool SpawnTestBatch(/* ... */) = 0;
   };

   } // namespace gte
   ```
3. Decide WHERE each nullable pointer to these interfaces lives. Likely
   candidates, per real call site: some belong on `Core` itself (if the
   call site already has access to a `Core&`), others may need to be
   threaded through whatever object `EngineCommandDispatch`/`NetworkServer`
   already hold a reference to (read their real constructors to see what
   they already have access to — do not invent a new global if an existing
   object reference already reaches the right place). This is a genuine,
   file-by-file decision — document each choice in the completion report.
4. Add nullable pointer members (default `nullptr`) for each interface to
   wherever Step 3 decided, plus a setter (e.g.
   `void SetSceneIOCapability(ISceneIOCapability* cap)`).
5. Do NOT touch any real call site's `#if GTE_ENABLE_EDITOR` block yet in
   this phase — only add the new interfaces + wiring points. Confirm this
   compiles alongside the still-existing macro branches (both can coexist
   temporarily; Phase 6-7 remove the macros).
6. Compile-check: incremental build, confirm the new header compiles
   cleanly and introduces no circular includes.

## Files Touched

- NEW `src/Core/EditorCapabilities.h`
- Whatever host object(s) Step 3 identifies (likely `Application.h` for
  now, since `Core`/`EditorHost` don't exist until later phases — add the
  nullable pointer members there, to be moved into `Core`/`EditorHost` by
  Phases 12/15 without needing to redesign the interfaces themselves)
- `CMakeLists.txt` (add the new header — header-only, may need no source
  list change if it has no `.cpp`)

## Definition of Done

- One small interface per real capability exists, matching what the real
  call sites actually need (verified by reading them, not guessed).
- `PHASE5_COMPLETION_REPORT.md` documents, for each interface, exactly
  where its nullable-pointer wiring point lives and why.
- Git commit.

## Out of Scope

Do not convert any real call site yet — that is Phases 6 and 7.
