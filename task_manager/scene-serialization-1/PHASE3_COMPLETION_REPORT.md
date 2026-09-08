# PHASE3 — Completion Report: `src/Scene/` Module — `SceneDocument` + the `.gtscene` Text Format

Part of the `scene-serialization-1` campaign — see `PHASE0_MASTER_STRATEGY.md`
for the full plan and `PHASE3_SCENE_DOCUMENT_AND_TEXT_FORMAT.md` (v2) for this
phase's own detailed work order. This phase is now **complete**.

## What was done

Followed the phase document's (v2) plan exactly:

1. **`src/Scene/SceneDocument.h`** (new file) — plain-data `SceneObjectKind`
   (`Primitive`/`Asset`), `SceneObjectRecord` (`kind`, `name`,
   `position`/`rotation`/`scale`, `primitiveType`, `assetGuid`), and
   `SceneDocument` (a flat `std::vector<SceneObjectRecord> objects`). Zero
   ECS/Renderer/filesystem dependency — only `Assets/AssetTypes.h` (`Guid`),
   `Math/Vec3.h`/`Math/Quat.h`, and
   `Renderer/Primitives/PrimitiveMeshGenerator.h` (`PrimitiveType`), matching
   the phase document's exact spec verbatim.
2. **`src/Scene/SceneTextFormat.h`** (new file) — the full `.gtscene` format
   spec as a doc comment, `kSceneTextFormatVersion = 1`, and the two pure
   functions: `SerializeSceneDocument()` / `DeserializeSceneDocument()`.
3. **`src/Scene/SceneTextFormat.cpp`** (new file) — hand-rolled, defensive
   parser/writer with no external parsing library, matching the codebase's
   existing `PmxLoader.cpp`/`VmdLoader.cpp` precedent:
   - `SerializeSceneDocument()` writes the `"GTSCENE 1"` header, then one
     `OBJECT`/`END` block per record — `name=`/`primitiveType=`/`assetGuid=`
     lines only emitted when meaningful (non-empty name; `kind == Primitive`
     for `primitiveType`; `kind == Asset` for `assetGuid`), `position=`/
     `rotation=`/`scale=` always emitted via `%.6f`-formatted CSV.
   - `DeserializeSceneDocument()` splits lines (tolerating `\n`/`\r\n`,
     trimming whitespace, skipping blanks), validates the exact
     `"GTSCENE 1"` header, then runs a small `OBJECT`/`key=value`/`END`
     state machine. Per the v2 revision: the `kind == Asset` ⇒
     `assetGuid.IsValid()` cross-field invariant is checked exactly once, at
     `END` time, and is scoped to `Asset` records only — a `Primitive`
     record carrying a stray, even all-zero-looking, `assetGuid=` line
     parses successfully. A syntactically malformed `assetGuid=` value
     (wrong length / non-hex) is rejected immediately at the point it's
     parsed, regardless of `kind`, via a dedicated
     `IsSyntacticallyValidGuidString()` helper — kept deliberately separate
     from `Guid::Parse()` itself (which never fails outright and instead
     degrades to `Guid::Invalid()`), exactly per the phase document's own
     reasoning for why the syntax check must run BEFORE calling
     `Guid::Parse()`. Every other malformed/unrecognized case follows the
     phase document's rules exactly: an unrecognized key is silently
     ignored (forward compatibility); a recognized key with a malformed
     value fails the whole parse; an absent optional field keeps its
     default; an unclosed `OBJECT` or a stray `END` fails the whole parse.
4. **`CMakeLists.txt`** — added the new `src/Scene/*` group to `gte_core`'s
   unconditional source list, inserted right after the last
   `src/Physics/*` entry (`JointPhysicsOverrideApplication.cpp`) and before
   `src/Assets/AssetTypes.h`, exactly as specified.
5. **Tests** — new `tests/Scene/SceneTextFormatTests.cpp`, registered in
   `tests/CMakeLists.txt`'s unconditional `GTE_TEST_SOURCES` list (right
   after `Assets/RigFileTests.cpp`), plus a matching descriptive paragraph
   added to the file's own "Test taxonomy" comment block. 18 `TEST()`s,
   covering every case the phase document's (v2) test list called for:
   - Round-trip: empty document; one `Primitive` record (non-default shape/
     transform/name); one `Asset` record (real, non-invalid `Guid`);
     multiple mixed records in order.
   - Malformed-input rejection: empty string; wrong magic; right magic,
     wrong version (`"GTSCENE 2"`); unclosed `OBJECT`; stray `END`; an
     unknown `kind=` value; an unknown `primitiveType=` value; wrong
     CSV token counts for `position=`; non-numeric `position=` text; a
     syntactically malformed `assetGuid=`.
   - v2 cross-field validation: a `kind=Asset` block with no `assetGuid=`
     line at all is rejected; a `kind=Asset` block whose `assetGuid=` parses
     to `Guid::Invalid()` (all-zero, syntactically valid 32 hex digits) is
     rejected; a `kind=Primitive` block carrying that same stray
     `assetGuid=` line parses successfully (proves the check is correctly
     scoped to `Asset` only).
   - Forward compatibility: an unrecognized `futureField=` key is silently
     ignored.
   - Optional-field omission: no `name=` line at all deserializes to an
     empty `name` rather than failing.

## Verification

- **Fast compile check** (per this task's workflow rules — no full build/
  regression test yet):
  - `cmake --build build --target gte_core` — **clean build**,
    `libgte_core.a` linked successfully (only the pre-existing, unrelated
    KTX-Software `git describe` version-fallback warning appeared).
  - `cmake --build build --target GreatTamanaEngineTests` — **clean build**,
    `GreatTamanaEngineTests.exe` linked successfully.
- Ran the new test suite directly
  (`GreatTamanaEngineTests.exe --gtest_filter=*SceneTextFormatTest*`): **all
  18 tests passed**, zero failures.
- Per this task's workflow rules, a full build/full regression `ctest` run
  was intentionally NOT performed — reserved for a later phase that
  explicitly calls for it.

## Notes / deviations from the phase document

None — every file, function signature, format-spec detail (including the
v2 kind-aware `assetGuid=` cross-field validation), CMake insertion point,
and test case matches `PHASE3_SCENE_DOCUMENT_AND_TEXT_FORMAT.md` (v2)
exactly.

## Next phase

**PHASE4_SCENE_BUILDER_REGISTRY_ASSETDATABASE_BRIDGE** — build
`src/Scene/SceneBuilder.h/.cpp`, the ECS-facing bridge:
`BuildSceneDocumentFromRegistry()` (Registry + AssetDatabase →
`SceneDocument`) and `ClearSerializableSceneObjects()` (wipes only what this
feature owns before a Load), per `PHASE0_MASTER_STRATEGY.md`'s ordering.
