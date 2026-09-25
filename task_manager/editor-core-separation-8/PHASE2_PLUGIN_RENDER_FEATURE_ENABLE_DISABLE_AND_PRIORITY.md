# PHASE2 — Plugin Render Feature Enable/Disable + Live Priority Reorder

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Product Decisions #2/#4/#8 and Step 2.2's evidence). Also read
`PHASE1_COMPLETION_REPORT.md` for continuity (this phase does not depend on
PHASE1's code, but both phases touch sibling parts of the same overall
feature — confirm PHASE1 really did land cleanly first). Use `ask_questions`
for any genuine ambiguity.

## Step 1: The Goal

Give `RenderFeatureCompositor` two new, host-side-only mutator methods —
`SetFeatureEnabled(name, enabled)` and `SetFeaturePriority(name, priority)` —
that let a caller turn a loaded `_v2` plugin render feature on/off and
re-order it live within its own stage, WITHOUT touching the plugin ABI at
all. Make `ContributeRenderGraphPasses()` skip disabled entries. Make
`DebugSnapshot()` (and therefore `RenderGraphMetadata::renderFeatures`, and
therefore the ALREADY-SHIPPING `GET /render_graph` endpoint) report each
entry's current enabled state. By the end of this phase, `GET /render_graph`'s
`render_features[]` array has a new `"enabled"` key on every entry — with
ZERO new endpoint needed for that. Nothing in production code calls
`SetFeatureEnabled()`/`SetFeaturePriority()` yet (PHASE4/PHASE5's job) —
this phase is additive plumbing — the plain-data serialization half
(`RenderFeatureDebugEntry`'s new field, `to_json()`) is Tier-1 tested; the
new mutator methods themselves are Tier 2 and are exercised only by PHASE5's
live HTTP smoke test (see Step 3.6's LOCKED resolution — this is not left
ambiguous).

## Step 2: The Situation

Read these exact files in full before writing any code:

- `src/Core/Plugins/RenderFeatureCompositor.h` — the WHOLE file. Note:
  `struct Entry { IRenderFeatureModule_v2* module; GtePluginRenderFeatureDescriptor descriptor; };`,
  `std::vector<Entry> m_postComposite;`/`m_preUi;` (both sorted by priority
  ascending, per-stage), `DebugSnapshot() const` (its own doc comment: "Safe
  to call at most once per Editor frame... never mutates any of this class's
  own state" — THIS PHASE'S new methods are the first ones that DO mutate
  state, which is fine — that doc comment is specifically about
  `DebugSnapshot()` itself, not about the class in general).
- `src/Core/Plugins/RenderFeatureCompositor.cpp` — read `OnPluginsLoaded()`'s
  FULL body (the `sortAndDetectCollisions` lambda in particular — this
  phase's own `SetFeaturePriority()` REUSES this exact sort+tie-break logic,
  extracted into a shared private method rather than duplicated) and
  `ContributeRenderGraphPasses()`'s FULL body (the `combinedList` build —
  `combinedList.insert(...)` twice, then `if (combinedList.empty()) return;`
  — this phase's new filter goes IMMEDIATELY after both inserts, BEFORE that
  empty check, so a session where every plugin is disabled correctly takes
  the existing, already-safe "nothing to do" early-return path with zero new
  code needed there).
- `src/Core/Plugins/RenderFeatureDebugEntry.h` — the whole (tiny) file. This
  phase adds exactly one new field.
- `src/Renderer/RenderGraph/RenderGraphMetadata.cpp` — find the EXISTING
  `to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)`
  function (search `to_json` + `RenderFeatureDebugEntry` — it was added by
  the `editor-core-separation-7` campaign's PHASE2/PHASE4) and read its FULL
  current body — confirm the exact `snake_case` key-naming convention already
  used for `name`/`stage`/`priority`/`blend_mode` before adding the new
  `"enabled"` key in the same style.
- `src/Core/Core.h` — find `GetRenderFeatureCompositor() const noexcept`'s
  EXACT current declaration (its return type is
  `const RenderFeatureCompositor*` today) and `Core.cpp`'s matching
  definition (likely `return m_renderFeatureCompositor.get();` or similar,
  through whatever smart-pointer/optional member actually owns it — read the
  real member declaration in `Core.h` first).
- Confirm (via `search_in_dir` for `GetRenderFeatureCompositor()` across
  `src/`) EVERY existing call site, to confirm each one only ever assigns the
  result into a `const RenderFeatureCompositor*`-typed local or otherwise
  only calls existing `const`-qualified methods on it (e.g.
  `DebugSnapshot()`) — this is what makes the return-type widening in Step
  3.4 below provably safe. If any surprising call site is found that would
  break, STOP and `ask_questions` before proceeding.
- Is there an existing `tests/Core/Plugins/` test file for
  `RenderFeatureCompositor`? Confirmed during planning: **no** (a
  `search_in_dir` for `RenderFeatureCompositor` under `tests/` returned zero
  hits) — and this phase does NOT create one either; see Step 3.6's LOCKED
  resolution below for why.

## Step 3: The Plan

### Step 3.1 — `RenderFeatureCompositor.h` changes

1. `Entry` gains one new field:

```cpp
struct Entry {
    IRenderFeatureModule_v2* module = nullptr;
    GtePluginRenderFeatureDescriptor descriptor{};
    // editor-core-separation-8 campaign, PHASE2 - host-side-only override,
    // NEVER part of the plugin's own descriptor/ABI (the plugin ABI stays
    // byte-for-byte unchanged - see PHASE0_MASTER_STRATEGY.md's Step 2.2/
    // Locked Product Decision, mirroring GtePluginRenderFeatureDescriptor's
    // own "the HOST NEVER trusts a plugin to self-order at runtime" rule,
    // extended here to "and the host may now ALSO fully hide a plugin from
    // the compositing chain, without the plugin itself ever knowing").
    // Defaults true - every existing loaded plugin behaves EXACTLY as before
    // this phase until something explicitly calls SetFeatureEnabled(false).
    bool enabledOverride = true;
};
```

2. Two new public methods, declared immediately after `DebugSnapshot()`:

```cpp
// editor-core-separation-8 campaign, PHASE2 - host-side enable/disable
// override. Finds `name` in EITHER m_postComposite OR m_preUi (a plugin's
// own descriptor.name is unique by construction - two plugins sharing a
// name is not a case this engine defends against anywhere else either) and
// sets its enabledOverride. Returns false (no-op) if `name` matches no
// loaded _v2 plugin - defense-in-depth; a caller (the panel, PHASE4, or the
// HTTP bridge, PHASE5) is expected to only ever pass a name it already saw
// via DebugSnapshot()/GET /render_graph's own render_features[] array.
bool SetFeatureEnabled(const std::string& name, bool enabled);

// editor-core-separation-8 campaign, PHASE2 - host-side LIVE priority
// override (PHASE0_MASTER_STRATEGY.md's Locked Product Decision #2 - safe,
// because ContributeRenderGraphPasses() rebuilds its entire compositing
// chain fresh from m_postComposite/m_preUi every single frame, and every
// interned target name is keyed by plugin name + view name, never by list
// position). Mutates the SAME host-side Entry::descriptor.priority copy
// already made once at OnPluginsLoaded() time (never writes back into the
// plugin's own memory), then immediately re-sorts + re-runs the SAME
// collision-detection/tie-break logic OnPluginsLoaded() already uses, for
// ONLY the one stage `name` belongs to. Returns false if `name` matches no
// loaded _v2 plugin.
bool SetFeaturePriority(const std::string& name, std::int32_t priority);
```

3. New private helper (extracted from `OnPluginsLoaded()`'s existing inline
   lambda), declared alongside `EnsureTextureSized()`/etc.:

```cpp
// editor-core-separation-8 campaign, PHASE2 - extracted VERBATIM from
// OnPluginsLoaded()'s own former inline `sortAndDetectCollisions` lambda, so
// SetFeaturePriority() can reuse the EXACT same sort+collision-tie-break
// behavior for a single re-sort after a live priority change, without
// duplicating the logic.
static void SortAndDetectCollisionsInStage(std::vector<Entry>& entries, const char* stageName);

// editor-core-separation-8 campaign, PHASE2 - shared lookup used by both
// SetFeatureEnabled() and SetFeaturePriority(): searches m_postComposite
// then m_preUi for an Entry whose descriptor.name matches `name` exactly
// (std::strcmp against the fixed char[64] buffer). Returns nullptr if not
// found. Non-const overload only (both callers mutate through it).
Entry* FindEntryByName(const std::string& name);
```

### Step 3.2 — `RenderFeatureCompositor.cpp` changes

1. Move `OnPluginsLoaded()`'s existing `sortAndDetectCollisions` lambda body
   OUT into the new `static void SortAndDetectCollisionsInStage(...)`
   method, VERBATIM (same log lines, same tie-break) — then replace
   `OnPluginsLoaded()`'s own two call sites
   (`sortAndDetectCollisions(m_postComposite, "PostComposite");`/
   `sortAndDetectCollisions(m_preUi, "PreUI");`) with
   `SortAndDetectCollisionsInStage(m_postComposite, "PostComposite");`/
   `SortAndDetectCollisionsInStage(m_preUi, "PreUI");`. **Zero behavior
   change** — confirm by reading the extracted body against the original
   before moving on.

2. `FindEntryByName()`:

```cpp
RenderFeatureCompositor::Entry* RenderFeatureCompositor::FindEntryByName(const std::string& name)
{
    for (Entry& entry : m_postComposite) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    for (Entry& entry : m_preUi) {
        if (name == entry.descriptor.name) {
            return &entry;
        }
    }
    return nullptr;
}
```

(`entry.descriptor.name` is a fixed `char[64]` — comparing a `std::string`
against a `const char*` via `operator==` already works via the standard
library's own `char*`-to-`std::string` implicit conversion, exactly like
`SortAndDetectCollisionsInStage`'s own existing
`std::strcmp(a.descriptor.name, b.descriptor.name)` usage elsewhere in this
same file — either spelling is acceptable; prefer `name == entry.descriptor.name`
for readability since `std::string::operator==` already does the right
thing here.)

3. `SetFeatureEnabled()`:

```cpp
bool RenderFeatureCompositor::SetFeatureEnabled(const std::string& name, bool enabled)
{
    Entry* entry = FindEntryByName(name);
    if (entry == nullptr) {
        return false;
    }
    entry->enabledOverride = enabled;
    return true;
}
```

4. `SetFeaturePriority()`:

```cpp
bool RenderFeatureCompositor::SetFeaturePriority(const std::string& name, std::int32_t priority)
{
    Entry* entry = FindEntryByName(name);
    if (entry == nullptr) {
        return false;
    }
    entry->descriptor.priority = priority;

    // Re-sort ONLY the stage this entry actually belongs to - determined by
    // which vector FindEntryByName() actually found it in, not by
    // entry->descriptor.stage alone (defensive: always re-derive from the
    // real container to stay correct even if this class's own stage-routing
    // rules ever change).
    const bool isPostComposite =
        std::find_if(m_postComposite.begin(), m_postComposite.end(),
            [&name](const Entry& e) { return name == e.descriptor.name; })
        != m_postComposite.end();
    if (isPostComposite) {
        SortAndDetectCollisionsInStage(m_postComposite, "PostComposite");
    } else {
        SortAndDetectCollisionsInStage(m_preUi, "PreUI");
    }
    return true;
}
```

(Note: `entry` itself is a raw pointer into one of the two vectors — the
re-sort above INVALIDATES it, since `std::stable_sort` may reorder/relocate
elements. This is fine because `entry` is not read again after this point,
but double-check the final method body does not accidentally dereference
`entry` after the sort call — read your own diff back before moving on.)

5. `ContributeRenderGraphPasses()` — immediately AFTER the existing two
   `combinedList.insert(...)` calls, BEFORE the existing
   `if (combinedList.empty()) { return; }` check, add:

```cpp
combinedList.erase(std::remove_if(combinedList.begin(), combinedList.end(),
    [](const Entry& entry) { return !entry.enabledOverride; }), combinedList.end());
```

(Needs `#include <algorithm>` — confirm it is already included in this file;
if not, add it.)

6. `DebugSnapshot()`'s existing `appendStage` lambda gains one new line:

```cpp
debugEntry.enabled = entry.enabledOverride;
```

(placed alongside the existing `debugEntry.name = ...`/`debugEntry.stage =
...`/etc. assignments, in the same order style.)

### Step 3.3 — `RenderFeatureDebugEntry.h` changes

Add one new field, at the end of the struct (never inserted in the middle —
matches this whole codebase's own "append at the end" convention):

```cpp
struct RenderFeatureDebugEntry {
    std::string name;
    std::string stage;
    std::int32_t priority = 0;
    std::string blendMode;
    // editor-core-separation-8 campaign, PHASE2 - the host-side enable/
    // disable override (RenderFeatureCompositor::Entry::enabledOverride).
    // Defaults true - matches every existing DebugSnapshot() call site's own
    // prior output for a struct that never went through this campaign's own
    // new SetFeatureEnabled() at all.
    bool enabled = true;
};
```

### Step 3.4 — `RenderGraphMetadata.cpp`'s `to_json` update

Find the exact existing
`void to_json(nlohmann::json& j, const RenderFeatureDebugEntry& entry)`
function body and add one new line, in the SAME `snake_case` style as its
existing `j["blend_mode"] = entry.blendMode;` (or whatever the real existing
line reads — `read_file` it first):

```cpp
j["enabled"] = entry.enabled;
```

### Step 3.5 — `Core.h`'s `GetRenderFeatureCompositor()` return-type widening

Change the declared return type from `const RenderFeatureCompositor*` to
`RenderFeatureCompositor*` — the method itself STAYS `const`-qualified (it
does not mutate `Core`):

```cpp
RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept;
```

Update `Core.cpp`'s matching definition's own return-type in its signature
the same way (the function BODY itself — e.g. `return m_renderFeatureCompositor.get();`
— needs no change at all, since `.get()` already yields a non-`const`
pointer regardless of the accessor method's own `const`-ness). **Before
committing this change, re-run the `search_in_dir` from Step 2 one more
time** to reconfirm every real call site still compiles unmodified (they
all assign into a `const RenderFeatureCompositor*` local or call only
`const` methods — both remain perfectly legal against a non-`const`
pointer).

### Step 3.6 — Build system registration + test coverage (LOCKED — no `ask_questions` needed, resolved by direct investigation)

**This decision was already resolved by re-checking the real repository, not
left open for you to re-litigate.** `RenderFeatureCompositor`'s constructor
needs a real `Core&`/`Renderer&` — confirmed genuinely heavy (`Renderer`
cannot be constructed at all without a live or headless `VkDevice`/
`VkSurfaceKHR`). A real precedent for constructing a full `Core` (and
therefore, transitively, a real `RenderFeatureCompositor` —
`Core::RegisterBuiltinCapabilityOrchestrators()` always constructs one in
`Core`'s own constructor) headlessly DOES exist —
`tests/Core/CoreHeadlessConstructionTests.cpp`, via
`tests/Fakes/HeadlessSurfaceProvider.h` (`VK_EXT_headless_surface`) — but
that ONE test itself already `GTEST_SKIP()`s on this development machine
(confirmed: AGENTS.md's own "Testability & Regression Safety" section
states outright "the current development machine doesn't support headless
mode anyway"), and `Entry` (the struct `SetFeatureEnabled()`/
`SetFeaturePriority()`/`FindEntryByName()` all operate on) is `private` —
there is no existing hand-populated-`Entry`-list backdoor, and adding one
would mean widening this class's own encapsulation purely to serve a test
that would then self-skip on this exact machine anyway.

**Locked resolution: option (b).** `SetFeatureEnabled()`/
`SetFeaturePriority()`/`ContributeRenderGraphPasses()`'s new filter/
`FindEntryByName()`/`SortAndDetectCollisionsInStage()` are Tier 2 (need a
live/headless `Renderer&` to construct anything at all) and get NO new
automated test file this phase — mirroring AGENTS.md's own explicit,
standing policy ("the absence of automated Tier 2 coverage should never
itself slow down or stop feature work") and this exact same
`RenderFeatureCompositor` class's own PRE-EXISTING, already-accepted zero-
Tier-1-coverage state (confirmed: `search_in_dir` for
`"RenderFeatureCompositor"` under `tests/` returns zero hits today, before
this campaign too). These 5 methods are exercised end-to-end ONLY by
PHASE5's own live HTTP smoke test (which drives a REAL, running
`GreatTamanaEditor.exe` — a genuine live device, not headless) — record this
explicitly, and honestly, in `PHASE2_COMPLETION_REPORT.md` rather than
treating it as a silent gap.

**Do NOT create `tests/Core/Plugins/RenderFeatureCompositorTests.cpp` this
phase.** Instead:

- `tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` — this file
  ALREADY has real, passing Tier-1 coverage for `RenderFeatureDebugEntry` and
  its `to_json()` shape (`GpuDrivenBatchesAndRenderFeaturesAreCopiedThroughUnchanged`
  and `ToJsonProducesExpectedTopLevelShapeAndNullHandling` — `read_file` both
  in full first). Extend BOTH: add `feature.enabled = false;` (or `true`, per
  test) to each hand-built `RenderFeatureDebugEntry`, and add a matching
  `EXPECT_EQ(metadata.renderFeatures[0].enabled, ...)` /
  `EXPECT_EQ(j["render_features"][0]["enabled"].get<bool>(), ...)` assertion
  to each — zero Vulkan/Core/Renderer involved, genuinely Tier 1, and this is
  the REAL regression proof that `GET /render_graph`'s new `"enabled"` field
  actually serializes correctly.
- No `tests/CMakeLists.txt` change is needed for this phase (no new test
  file is created) — confirm this explicitly in `PHASE2_COMPLETION_REPORT.md`
  rather than silently skipping the check.

### Verification

1. Incremental build: `cmake --build build`.
2. Run the extended `RenderGraphMetadataTests.cpp` cases (Step 3.6).
3. `git_status` — confirm the diff touches EXACTLY:
   `RenderFeatureCompositor.h/.cpp`, `RenderFeatureDebugEntry.h`,
   `RenderGraphMetadata.cpp`, `Core.h`, `Core.cpp`, and the extended
   `RenderGraphMetadataTests.cpp`. Nothing else — in particular, do NOT
   touch `RenderGraphMetadata.h` itself (its `RenderFeatureDebugEntry`
   `#include`/field list needs no change — only the `.cpp`'s `to_json` body
   does), do NOT touch `plugins/gte_plugin_abi/RenderFeatureDescriptor.h` at
   all, and do NOT touch `tests/CMakeLists.txt` (no new test file is created
   this phase — Step 3.6).

### What this phase does NOT do

- Does not add any UI or HTTP surface — `SetFeatureEnabled()`/
  `SetFeaturePriority()` are only ever called from PHASE5's own live HTTP
  smoke test until PHASE4/PHASE5 land (Step 3.6 — no live-device Tier-1 test
  exercises them this phase either).
- Does not change `GtePluginRenderFeatureDescriptor` (the plugin ABI) at
  all.
- Does not add a new discovery endpoint for plugin features (Locked Product
  Decision #8 — `GET /render_graph`'s existing `render_features[]` already
  covers this once this phase lands).
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 6
  only).

### Completion

Write `PHASE2_COMPLETION_REPORT.md` (the exact final method bodies, an
explicit confirmation that Step 3.6's locked Tier-2/no-new-test-file decision
was followed exactly as written, and the real, current
`to_json(RenderFeatureDebugEntry)` body before/after diff), then `git_add` +
`git_commit`.
