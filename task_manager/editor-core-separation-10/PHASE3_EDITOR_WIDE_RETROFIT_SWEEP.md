# PHASE3 — Editor-Wide Retrofit Sweep

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first).
**Previous phase:** `PHASE2_SCOPED_UNIQUE_ID_HELPER_AND_RENDERGRAPHPANEL_FIX.md`
— read its own `PHASE2_COMPLETION_REPORT.md` before starting.

---

## Step 1: The Goal Of This Phase

`gte::ScopedUniqueId` (built in PHASE2) is complete and final — nothing new
needs to be built in this phase. This phase is pure, mechanical (but
careful) retrofitting: migrate every REMAINING `ImGui::PushID(...)` call
site in `src/Editor/` (inventoried in `PHASE0_MASTER_STRATEGY.md` section
2.4) onto `ScopedUniqueId`, so Locked Design Decision #1 ("whole Editor, not
just the reported panel") becomes concretely, verifiably true. End state: a
`search_in_dir` sweep for `ImGui::PushID` under `src/Editor/` returns
matches ONLY inside `ImGuiUniqueId.cpp` itself (PHASE2's own
implementation) — everywhere else goes through the mandated helper.

None of the four files this phase touches
(`ProjectPanel.cpp`/`HierarchyPanel.cpp`/`InspectorPanel.cpp`/
`BoneViewerWindow.cpp`) has ever had a REPORTED bug — every retrofit in this
phase is preventative hardening of an already-safe-today call site (each one
already uses a dense integer index as its `PushID` key), done purely so the
"never happen again, anywhere, even in code not written yet" guarantee
(LDD1) is uniform across the whole Editor, and so every single one of these
call sites also gets the proactive `ImGuiIdConflictGuard` logging safety net
"for free" — e.g. a future hierarchy-cycle bug that made the SAME
`entity.index` render twice in one frame would now be caught and logged
automatically, which it is not today.

## Step 2: The Situation Going Into This Phase

Re-read every file below with `read_file`/`read_line` immediately before
editing it — the line numbers quoted here are a snapshot from when this
strategy was written and may have drifted (though none of these four files
were touched by PHASE1/PHASE2, so drift should be minimal to none).

## Step 3: The Detailed Plan — one subsection per file

### 3.1 — `src/Editor/Panels/ProjectPanel.cpp` (three call sites)

Add the include once, near the top with the other local includes:

```cpp
#include "../ImGuiUniqueId.h"
```

**Call site A — `RenderLeftPaneFolder()` (currently ~line 129-162).** This
function's OWN `PushID` (line 135, keyed off `entry.relativePath`) needs an
index, but the function itself doesn't currently receive one — it is called
from two places: `RenderLeftPane()`'s top-level loop over `m_tree` (line
181-183) and its OWN recursive loop over `entry.children` (line 155-157).
Both call sites need to supply an index. Change the signature (both the
`.h` declaration, `ProjectPanel.h` line 168, and the `.cpp` definition, line
129) to add a leading `int siblingIndex` parameter:

```cpp
// ProjectPanel.h
void RenderLeftPaneFolder(EditorContext& ctx, int siblingIndex, const ProjectEntry& entry);
```

```cpp
// ProjectPanel.cpp
void ProjectPanel::RenderLeftPaneFolder(EditorContext& ctx, int siblingIndex, const ProjectEntry& entry)
{
    if (!entry.isDirectory) {
        return;
    }

    // task_manager/editor-core-separation-10 campaign, PHASE3 - was
    // ImGui::PushID(entry.relativePath.c_str()) alone. A relative
    // filesystem path is very likely unique in practice, but "very likely"
    // is exactly the assumption this whole campaign exists to stop relying
    // on - siblingIndex (this call's own position among its immediate
    // siblings) is what actually guarantees uniqueness now; relativePath is
    // kept only as the debug key.
    ScopedUniqueId idScope(siblingIndex, "ProjectPanel::RenderLeftPaneFolder", entry.relativePath.c_str());

    const bool hasSubfolders = HasSubfolders(entry);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (entry.relativePath == m_currentFolderRelativePath) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!hasSubfolders) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_Bullet;
    }

    const bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
    RecordFolderDropZone(entry.relativePath);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
        m_currentFolderRelativePath = entry.relativePath;
        SetAssetSelection(ctx, entry.relativePath, true);
    }

    if (hasSubfolders && open) {
        for (std::size_t i = 0; i < entry.children.size(); ++i) {
            RenderLeftPaneFolder(ctx, static_cast<int>(i), entry.children[i]);
        }
        ImGui::TreePop();
    }
    // No manual ImGui::PopID() anymore - ScopedUniqueId's destructor
    // handles it.
}
```

Update `RenderLeftPane()`'s own top-level loop (currently line 181-183) to
supply an index too:

```cpp
    if (rootOpen) {
        for (std::size_t i = 0; i < m_tree.size(); ++i) {
            RenderLeftPaneFolder(ctx, static_cast<int>(i), m_tree[i]);
        }
        ImGui::TreePop();
    }
```

**Call site B — `RenderBreadcrumb()`'s `segmentIndex` (currently ~line
188-220).** Already index-based (`ImGui::PushID(segmentIndex++)`, line 209)
— just swap the mechanism:

```cpp
        ImGui::SameLine();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        // task_manager/editor-core-separation-10 campaign, PHASE3.
        {
            ScopedUniqueId idScope(segmentIndex++, "ProjectPanel::RenderBreadcrumb", segment.c_str());
            if (ImGui::SmallButton(segment.c_str())) {
                m_currentFolderRelativePath = accumulated;
            }
        }
```

(Note the explicit braces — `idScope` must go out of scope, and therefore
pop its ID, before the loop's next iteration re-enters this block; a bare
`ScopedUniqueId idScope(...)` declared at the same indentation as the rest
of the `while` body without its own braces would otherwise stay alive for
the REST of the loop body only, which happens to also be correct here since
nothing after it in this same iteration needs the ID popped early — but the
explicit braces make the intent unambiguous and match the pattern's shape
in every other retrofit in this phase. Prefer the explicit-braces form.)

**Call site C — `RenderRightPaneEntry()` (currently ~line 222-268),
called from `RenderRightPane()`'s loop (currently ~line 270-284).** Same
shape as call site A — add a leading index parameter:

```cpp
// ProjectPanel.h
void RenderRightPaneEntry(EditorContext& ctx, int entryIndex, const ProjectEntry& entry);
```

```cpp
// ProjectPanel.cpp
void ProjectPanel::RenderRightPaneEntry(EditorContext& ctx, int entryIndex, const ProjectEntry& entry)
{
    // task_manager/editor-core-separation-10 campaign, PHASE3 - was
    // ImGui::PushID(entry.relativePath.c_str()) alone.
    ScopedUniqueId idScope(entryIndex, "ProjectPanel::RenderRightPaneEntry", entry.relativePath.c_str());

    const bool isSelected = ctx.selection.IsAssetSelected(entry.relativePath);
    const std::string label = entry.isDirectory ? ("[Folder] " + entry.name) : entry.name;
    ImGui::Selectable(label.c_str(), isSelected);

    // ... (everything else in this function is UNCHANGED - keep it exactly
    // as-is, only remove the final manual ImGui::PopID(); at the end of the
    // function, since ScopedUniqueId's destructor now does that.)
}
```

```cpp
void ProjectPanel::RenderRightPane(EditorContext& ctx)
{
    RenderBreadcrumb();
    ImGui::Separator();

    const std::vector<ProjectEntry>* children = CurrentFolderChildren();
    if (children == nullptr || children->empty()) {
        ImGui::TextDisabled("(empty - drag files in from Explorer, or right-click for options)");
        return;
    }

    for (std::size_t i = 0; i < children->size(); ++i) {
        RenderRightPaneEntry(ctx, static_cast<int>(i), (*children)[i]);
    }
}
```

Re-read the FULL body of `RenderRightPaneEntry()` before editing (it is
longer than the excerpt in `PHASE0_MASTER_STRATEGY.md` — it also handles
drag-and-drop, per the file's own comments) — only the opening `PushID`
line, the function signature, and the final `PopID()` removal should
change; every drag-and-drop/selection/tooltip line in between stays exactly
as-is.

### 3.2 — `src/Editor/Panels/HierarchyPanel.cpp` (one call site)

`RenderEntityNode()` (currently ~line 68-...), currently
`ImGui::PushID(static_cast<int>(entity.index));` at line 70. `entity.index`
is already a real, dense, stable ECS slot index — genuinely safe today.
Retrofit purely for LDD1's uniform-coverage guarantee (and as a free safety
net against a hypothetical future hierarchy-cycle bug re-visiting the same
entity twice in one recursive walk):

```cpp
#include "../ImGuiUniqueId.h" // add near the top with other local includes
```

```cpp
void RenderEntityNode(Game& game, Renderer& renderer, EditorContext& ctx, Registry& registry, Entity entity)
{
    // task_manager/editor-core-separation-10 campaign, PHASE3 - was
    // ImGui::PushID(static_cast<int>(entity.index)) alone. entity.index is
    // already genuinely unique per live entity, so this is uniform-coverage
    // hardening (LDD1), not a bug fix - it ALSO now gets
    // ImGuiIdConflictGuard's logging for free, which would catch a future
    // hierarchy-CYCLE bug (the same entity visited twice in one recursive
    // walk) that nothing today detects.
    ScopedUniqueId idScope(static_cast<int>(entity.index), "HierarchyPanel::RenderEntityNode");

    const std::string label = BuildEntityLabel(registry, entity);
    // ... (everything else in this function is UNCHANGED - remove only the
    // final manual ImGui::PopID(); at the end of the function.)
}
```

Note: this call site has no meaningful separate "debug key" string beyond
the label itself (which is already computed a few lines later, after the
`ScopedUniqueId` construction) — passing no third argument (defaults to
`""`) is correct and fine here; do not reorder the function to compute
`label` earlier just to feed it into `debugKey` — it is not worth the
churn.

### 3.3 — `src/Editor/Panels/InspectorPanel.cpp` (two call sites, nested)

Currently (see `PHASE0_MASTER_STRATEGY.md` section 2.4): `chainIndex` at
line 755, nested `jointIndex` at line 762, both already-dense loop indices.

```cpp
#include "../ImGuiUniqueId.h" // add near the top with other local includes
```

```cpp
                for (std::size_t chainIndex = 0; chainIndex < model->chains.size(); ++chainIndex) {
                    DynamicChainDefinition& chain = model->chains[chainIndex];
                    // task_manager/editor-core-separation-10 campaign, PHASE3.
                    ScopedUniqueId chainIdScope(static_cast<int>(chainIndex), "InspectorPanel::VerletChainSection");
                    char chainLabel[64];
                    std::snprintf(chainLabel, sizeof(chainLabel), "Chain %zu (%zu joints)", chainIndex,
                        chain.jointBoneIndices.size());
                    if (ImGui::TreeNode(chainLabel)) {
                        for (std::size_t jointIndex = 0; jointIndex < chain.jointSettings.size(); ++jointIndex) {
                            DynamicJointSettings& settings = chain.jointSettings[jointIndex];
                            // task_manager/editor-core-separation-10 campaign, PHASE3.
                            ScopedUniqueId jointIdScope(static_cast<int>(jointIndex), "InspectorPanel::VerletJointSection");
                            ImGui::Text("Joint %zu", jointIndex);
                            ImGui::DragFloat("Damping", &settings.damping, 0.005f, 0.0f, 1.0f);
                            ImGui::DragFloat("Stiffness", &settings.stiffness, 0.005f, 0.0f, 1.0f);
                            ImGui::DragFloat("Weight (Mass)", &settings.mass, 0.01f, 0.01f, 100.0f);
                            ImGui::BeginDisabled();
                            ImGui::DragFloat("Collision Radius (from PMX rigid body shape)", &settings.collisionRadius);
                            ImGui::EndDisabled();
                            // No manual ImGui::PopID(); anymore.
                        }
                        // ... rest of the TreeNode body unchanged (Separator,
                        // "Enable Collision" checkbox, etc.) ...
                        ImGui::TreePop();
                    }
                    // No manual ImGui::PopID(); anymore.
                }
```

Give the two nested scopes distinct local variable names
(`chainIdScope`/`jointIdScope` above) — never name both `idScope` in the
same enclosing function body, since (unlike raw `PushID`/`PopID` pairs,
which don't need C++ names at all) `ScopedUniqueId` is a real stack object
and a shadowed/duplicate name would either fail to compile or (worse) shadow
the outer one silently depending on brace placement. Re-read the FULL
surrounding function before editing — this is a deeply-nested block inside
a much larger `InspectorPanel.cpp` function; only touch the two `PushID`/
`PopID` lines and their immediately adjacent loop headers, nothing else.

### 3.4 — `src/Editor/BoneViewerWindow.cpp` (three call sites)

```cpp
#include "ImGuiUniqueId.h" // add near the top with other local includes (same directory, no "../" needed - this file lives directly under src/Editor/)
```

**Call site A — `RenderBoneTreeNode()` (currently ~line 560-631),
`ImGui::PushID(boneIndex);` at line 587.** `boneIndex` here is the bone's
own dense array index into `m_bones` — a real, stable, already-unique-by-
construction key (not merely a sibling position):

```cpp
    const std::string label = bone.name.empty() ? ("Bone " + std::to_string(boneIndex)) : bone.name;

    // task_manager/editor-core-separation-10 campaign, PHASE3.
    ScopedUniqueId idScope(boneIndex, "BoneViewerWindow::RenderBoneTreeNode", bone.name.c_str());
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    // ... unchanged body ...

    if (opened && !children.empty()) {
        for (const std::int32_t child : children) {
            RenderBoneTreeNode(child, lowerFilter, depth + 1, ctx);
        }
        ImGui::TreePop();
    }
    // No manual ImGui::PopID(); anymore.
```

**Call site B — `RenderFlatPartRow()` (currently ~line 633-684),
`ImGui::PushID(index);` at line 643.** `index` here is already a dense
part index (bone/rigid-body/joint index depending on `kind`):

```cpp
    const bool isSelected = ctx.selection.IsModelPartSelected(m_targetEntity, kind, index);
    const std::string label = name.empty() ? ("Part " + std::to_string(index)) : name;

    // task_manager/editor-core-separation-10 campaign, PHASE3.
    ScopedUniqueId idScope(index, "BoneViewerWindow::RenderFlatPartRow", name.c_str());
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        // ... unchanged body ...
    }
    // No manual ImGui::PopID(); anymore.
```

**Call site C — `RenderVerletChainNode()` (currently ~line 686-748),
`ImGui::PushID(chainIndex);` at line 712.** `chainIndex` here is already a
dense per-chain index:

```cpp
    char header[160];
    std::snprintf(header, sizeof(header), "Chain %d - Root: %s (%zu joints)", chainIndex, rootName,
        chain.jointBoneIndices.size());

    // task_manager/editor-core-separation-10 campaign, PHASE3.
    ScopedUniqueId idScope(chainIndex, "BoneViewerWindow::RenderVerletChainNode");
    if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        // ... unchanged body ...
        ImGui::TreePop();
    }
    // No manual ImGui::PopID(); anymore.
```

(No meaningful `debugKey` string is cheaply available here beyond
`chainIndex` itself before `header` is built — passing none, i.e. the
default `""`, is correct.)

### 3.5 — The "grep gate" (mandatory, this is this phase's real acceptance
criterion)

After every retrofit above, run:

```
search_in_dir path="C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src\Editor" content="ImGui::PushID" filter="*.cpp"
search_in_dir path="C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\src\Editor" content="ImGui::PopID" filter="*.cpp"
```

Both must now return matches ONLY inside `src/Editor/ImGuiUniqueId.cpp`
(PHASE2's own implementation, which legitimately calls the raw functions
internally so `ScopedUniqueId` can wrap them). If any OTHER file still shows
up, that call site was missed — go back and retrofit it before considering
this phase done. This grep result belongs verbatim in
`PHASE3_COMPLETION_REPORT.md` as the phase's own proof of completion.

Also re-run the `"##Enabled_"` search from PHASE2 step 3.8.2 as a final
sanity check — still zero matches expected.

### 3.6 — Verify (incremental compile check only — no full build/ctest this
phase)

1. Incremental build across all four edited files. The most likely compile
   errors: (a) a header (`.h`) declaration not updated to match a `.cpp`
   signature change (`ProjectPanel.h`'s two declarations), (b) a caller not
   updated after a callee's signature gained a new leading `int` parameter,
   (c) two `ScopedUniqueId` locals accidentally given the same name in one
   nested scope (InspectorPanel.cpp).
2. Do NOT run a full `ctest` regression yet (Locked Design Decision #8) —
   PHASE4 does that. It is fine (encouraged) to do a quick, targeted
   incremental rebuild + a manual live smoke test (open Hierarchy/Inspector/
   Project/Bone Viewer panels via `run_app_background` +
   `gte_send_request GET /activate_tab`, screenshot each, confirm nothing
   visually broke) since these four panels are otherwise completely
   untested by any existing automated Tier-1 test for THIS specific
   behavior (tree/selectable rendering is inherently a live-ImGui, Tier-2
   concern).

### 3.7 — Write `PHASE3_COMPLETION_REPORT.md`

Include the final, literal `search_in_dir` "grep gate" output from step 3.5
verbatim (this is the load-bearing evidence for this phase), plus a short
note on the live smoke-test screenshots taken. Then `git_add` + `git_commit`.

## Step 4: Handoff To PHASE4

After this phase, `ScopedUniqueId` is the ONLY way any ImGui code anywhere
under `src/Editor/` enters a per-iteration ID scope — code, not just intent,
now matches Locked Design Decision #1. PHASE4 makes this binding on all
FUTURE code too (documentation), then proves the whole campaign with a full
build and full regression pass.
