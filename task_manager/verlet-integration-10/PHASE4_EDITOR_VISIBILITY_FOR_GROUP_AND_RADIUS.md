# PHASE4 (v2) — Editor Visibility for Group/Mask Filtering and Joint Radius

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1, PHASE2, PHASE3 (this phase only makes existing Editor UI
text/overlays accurately reflect behavior those phases already implemented —
it changes zero simulation behavior itself)

**v2 change summary (see `PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes
(v2)" for the full campaign-wide list) — THIS PHASE HAD THE CAMPAIGN'S ONE
CRITICAL DEFECT, NOW FIXED:**

1. **(Finding #1, critical) v1's Step 3.3 instructed inserting the new
   `collisionGroupMask` field BETWEEN `RigidBodyEntry`'s existing `group` and
   `motionType` fields, and assumed a field-by-field
   `entry.group = body.group;`-style copy existed somewhere to anchor the
   edit near.** Neither is true: the real struct is populated at exactly ONE
   call site, via 8-argument POSITIONAL aggregate-init
   (`BoneViewerWindow.cpp`, confirmed lines 411-413). Following v1's plan
   literally would have produced a guaranteed, unrecoverable **compile
   error** the moment this file was next built (the previously-last
   positional argument, `body.motionType`, an `enum class
   RigidBodyMotionType` value, would be forced into the newly-inserted
   `std::uint16_t collisionGroupMask` slot — no implicit conversion exists
   between an unrelated `enum class` and an integer type in C++). **Fixed
   below (Step 3.3): the new field is appended strictly AFTER `motionType`
   (the struct's true last field today), and the one real construction call
   site gets a 9th trailing positional argument in the same edit.**
2. **(Finding #2) the independent Editor-only re-derivations of the group/
   mask AND-test (`CountCollidersReachableByChain()` in
   `InspectorPanel.cpp`, and the overlay-dimming loop in
   `BoneViewerWindow.cpp`) both shifted `1u << group` with no range check,**
   the same undefined-behavior risk PHASE1 v2 fixed in the production solver
   — fixed below by inlining the identical `& 0x0Fu` mask in both places
   (kept as independent re-derivations, per this campaign's own "never call
   a `static`/anonymous-namespace production function from another
   translation unit" rule — see PHASE1's own test-writing guidance for why
   this duplication is deliberate, not an oversight).
3. **(Finding #5) Step 3.4's own pseudo-code used a placeholder variable
   name, `m_verletModel`, and explicitly flagged it as unverified.** This has
   now been verified against the live file: the real local variable is named
   `verletModel` (confirmed, `BoneViewerWindow.cpp`, a local
   `const DynamicChainRigCache::ModelEntry*`, not a member — no `m_` prefix).
   Step 3.4 below uses the confirmed real name directly.
4. **(Finding #6) Step 3.4's reachable-collider matching (for the 3D overlay
   dimming) matched a `RigidBodyEntry` to its `ModelColliderDefinition` by
   `boneIndex` alone,** which is technically ambiguous if a model has more
   than one `Static` rigid body on the same bone (rare, but valid PMX
   authoring). Tightened below to match on `(boneIndex, shape, shapeSize)` as
   a combined key — resolves the ambiguity in every realistic case with zero
   new data-model fields, for a feature that is purely a cosmetic Editor aid
   to begin with.

---

## Step 1 — The Goal

`src/Editor/Panels/InspectorPanel.cpp` and `src/Editor/BoneViewerWindow.cpp`
already contain user-facing text/overlays that describe the dynamic-chain
collision feature (inherited from `task_manager/verlet-integration-9`,
PHASE5) — but that text was written when "collision" meant "every joint
tests against every Static collider, unconditionally, off by default." After
PHASE1-3, that description is now **incomplete and, in one specific case,
misleading**: it never mentions that PMX collision-group/layer rules can
silently exclude some colliders from actually affecting a given chain, it
never mentions a joint's own physical radius, and its "off by default"
framing is now stale. **Goal:** update these existing surfaces (never
introduce a whole new UI feature) so a user tuning a model in the Editor can
still trust what these panels tell them.

## Step 2 — The Situation

Exact current text, confirmed by direct inspection:

1. `src/Editor/Panels/InspectorPanel.cpp`, TWO call sites (single-part
   inspector, confirmed lines 465-474; per-chain "Dynamic Chain Physics"
   section, confirmed lines 648-657) — both textually IDENTICAL:
   ```cpp
        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
        if (chain.collisionEnabled) {
            ImGui::TextDisabled(
                "Collides against every auto-detected Static rigid body for this model (%zu total).",
                model->colliders.size());
            if (model->colliders.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
            }
        }
   ```
   The phrase "collides against **every** ... rigid body" is now factually
   inaccurate once PHASE1's group/mask filter can exclude some of them for a
   given chain. (This is exactly the text visible in the screenshot the user
   originally attached showing "Collides against every auto-detected Static
   rigid body for this model (43 total)." — confirmed, this is the
   single-part inspector call site, line 465-474.)
2. `src/Editor/BoneViewerWindow.cpp`, confirmed line 742 (Verlet tree pane,
   one row per chain):
   ```cpp
        if (chain.collisionEnabled) {
            ImGui::TextDisabled("Collision: enabled (collides against this model's auto-detected colliders)");
        }
   ```
   Same staleness.
3. `src/Editor/BoneViewerWindow.cpp`, confirmed lines 1500-1532 (3D viewport
   overlay, `m_viewMode == ModelPartKind::Verlet` branch) — draws EVERY
   detected Static collider's wireframe unconditionally whenever the Verlet
   view mode is active, with no visual distinction for a collider that a
   SPECIFIC selected/expanded chain's own group/mask would actually exclude.
   This loop lives inside `if (verletModel != nullptr) { ... }`, immediately
   AFTER the closing brace of the per-chain `for (const
   DynamicChainDefinition& chain : verletModel->chains) { ... }` loop that
   draws tree edges/extra constraints (i.e. it already runs once PER MODEL,
   not once per chain — confirmed by direct re-reading of the real file, not
   inferred).
4. `src/Editor/BoneViewerWindow.h`'s `RigidBodyEntry` struct (confirmed lines
   146-169) already carries a `group` field (added by an unrelated, earlier
   `verlet-integration-4` phase purely for the "Select All (Group)" toolbar
   button — see `Editor/RigidBodyGroupSelection.h`) but has **no**
   `collisionGroupMask` field at all — needed for this phase's own overlay
   improvement. **Confirmed today's exact, complete field list and order:**
   `name`, `translate`, `rotateRadians`, `shape`, `shapeSize`, `boneIndex`,
   `group`, `motionType` — in that exact order, with `motionType` as the true
   LAST field. **Confirmed today's ONE construction call site**
   (`BoneViewerWindow.cpp`, lines 411-413):
   ```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group, body.motionType });
        }
   ```
   This is plain 8-argument POSITIONAL aggregate-init, matching the 8 fields
   above 1:1, in order — there is no separate field-by-field assignment
   anywhere in this file for this struct. Any new field MUST be appended
   strictly after `motionType`, and this exact call site is where a 9th
   trailing argument must be added to actually populate it with real data
   (per `PHASE0_MASTER_STRATEGY.md`'s Step 2 point 12 and this phase's own
   v1→v2 fix, Revision Notes finding #1).

## Step 3 — The Plan

### 3.1 — `src/Editor/Panels/InspectorPanel.cpp`: accurate collider-count text

At BOTH call sites (single-part inspector AND per-chain section), replace
the existing `if (chain.collisionEnabled) { ... }` block with one that also
reports how many of the model's colliders are actually REACHABLE by this
specific chain under PMX group/mask rules. Add a small local helper function
near the top of this `.cpp` file (private to this translation unit, matching
this file's own existing convention of small local formatting helpers):

```cpp
// task_manager/verlet-integration-10, PHASE4 - counts how many of
// `colliders` this SPECIFIC chain's joints could ever actually reach under
// PMX collision-group/mask rules (mirrors DynamicChainSolver.cpp's own
// GroupsMayCollide()/GroupBit() truth table exactly, but is deliberately NOT
// a call into either of those functions - they are `static`/anonymous-
// namespace, not exported; this is a small, independent, Editor-only
// re-derivation purely for an informational readout, never itself part of
// the simulation). A chain with multiple joints having DIFFERENT group/mask
// values (uncommon but not forbidden - PHASE1 seeds this per-joint, not
// per-chain) counts a collider as "reachable" if ANY of the chain's own
// joints could hit it. `group` is masked to its documented 4-bit range
// (`& 0x0Fu`) before use as a shift amount - see PHASE1's own GroupBit()
// doc comment for why this is required for safety, not merely style (a raw,
// unvalidated .pmx file byte, confirmed never range-checked anywhere in this
// engine's load pipeline).
std::size_t CountCollidersReachableByChain(const DynamicChainDefinition& chain, const std::vector<ModelColliderDefinition>& colliders)
{
    std::size_t reachable = 0;
    for (const ModelColliderDefinition& collider : colliders) {
        bool anyJointReaches = false;
        for (const DynamicJointSettings& joint : chain.jointSettings) {
            const std::uint16_t jointBit = static_cast<std::uint16_t>(1u << (joint.group & 0x0Fu));
            const std::uint16_t colliderBit = static_cast<std::uint16_t>(1u << (collider.group & 0x0Fu));
            if ((jointBit & collider.collisionMask) != 0 && (colliderBit & joint.collisionMask) != 0) {
                anyJointReaches = true;
                break;
            }
        }
        if (anyJointReaches) {
            ++reachable;
        }
    }
    return reachable;
}
```

Then replace the readout block:

```cpp
        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
        if (chain.collisionEnabled) {
            const std::size_t reachable = CountCollidersReachableByChain(chain, model->colliders);
            ImGui::TextDisabled(
                "Collides against %zu of %zu auto-detected Static rigid-body collider(s) for this model "
                "(the rest are excluded by this chain's own PMX collision-group/layer rules).",
                reachable, model->colliders.size());
            if (model->colliders.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
            } else if (reachable == 0) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "None of this model's detected colliders are reachable by this chain's own PMX collision "
                    "group/mask - enabling this currently has no visible effect for this chain specifically.");
            }
        }
```

Apply this identical replacement at BOTH call sites (the two locations are
already textually identical today, per Step 2 point 1 — keep them identical
after this edit too).

### 3.2 — `src/Editor/Panels/InspectorPanel.cpp`: surface each joint's own collision radius

Immediately after the existing per-joint sliders
(`ImGui::DragFloat("Damping", ...)` / `"Stiffness"` / `"Weight (Mass)"`, both
call sites), add one read-only readout:

```cpp
        ImGui::BeginDisabled();
        ImGui::DragFloat("Collision Radius (from PMX rigid body shape)", &settings.collisionRadius);
        ImGui::EndDisabled();
```

This is READ-ONLY by design (`BeginDisabled()`/`EndDisabled()`, matching the
existing `restLength` readout's own established pattern a few lines above in
this same file, confirmed lines 424-429) — `collisionRadius` is derived data
from the model's own PMX file (PHASE2), not a free-tuning knob like
`damping`/`stiffness`/`mass`; exposing it editable would misleadingly suggest
a user's edit could ever be authored back into the source model, which it
cannot (mirrors this file's own already-established `restLength` precedent
exactly, for the same reason).

**Note:** the per-chain "Dynamic Chain Physics" section's own joint loop
(confirmed lines 632-640) does NOT currently show a `restLength` readout at
all (only the single-part inspector, lines 424-429, does) — this is
pre-existing, deliberate asymmetry between the two call sites (the per-chain
section shows every joint at once and keeps each one terse; the single-part
inspector focuses on exactly one selected joint and can afford one more
line), not a bug this phase should "fix" by unifying them. Add the
`collisionRadius` readout to BOTH call sites regardless (it is a single
short line, cheap even in the terser per-chain loop), but do not add
`restLength` to the per-chain loop as some kind of consistency pass — that is
out of scope for this phase and not something either the user or any prior
phase asked for.

### 3.3 — `src/Editor/BoneViewerWindow.h`: add `collisionGroupMask` to `RigidBodyEntry`

**(v2 — corrected placement, see this phase's own top-of-file "v2 change
summary," finding #1).** Append the new field strictly AFTER `motionType`
(today's true last field), never between `group` and `motionType`:

```cpp
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians;
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1;
        std::uint8_t group = 0;
        RigidBodyMotionType motionType = RigidBodyMotionType::Static;
        // task_manager/verlet-integration-10, PHASE4 (v2 - appended AFTER
        // motionType, the struct's own true last field BEFORE this campaign,
        // never inserted between `group` and `motionType` - the ONE real
        // construction call site in BoneViewerWindow.cpp populates this
        // struct via plain 8-argument POSITIONAL aggregate-init today; any
        // field inserted in the MIDDLE of this struct would either silently
        // absorb the wrong value or, in this specific case, fail to compile
        // outright, since `motionType` - an enum class with no implicit
        // integer conversion - would be forced into whatever new field took
        // its old position. See PHASE0_MASTER_STRATEGY.md's Step 2 point 12
        // for the general rule this struct is a concrete, previously-broken
        // example of.) - this body's own PMX collision-group MASK
        // (Assets/PhysicsData.h's own RigidBody::collisionGroupMask), added
        // purely so the Verlet overlay (BuildOverlayGeometry()'s own Verlet
        // branch) can visually distinguish a Static collider the
        // CURRENTLY-EXPANDED chain's own joints could never actually reach
        // (PHASE1's group/mask filter) from one it genuinely collides
        // against - never used for selection logic (unlike `group` above,
        // which the unrelated "Select All (Group)" toolbar button already
        // uses).
        std::uint16_t collisionGroupMask = 0xFFFF;
    };
```

Then update the ONE real construction call site
(`BoneViewerWindow.cpp`, confirmed lines 411-413) with a 9th trailing
positional argument, in the SAME edit:

```cpp
        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group, body.motionType, body.collisionGroupMask });
        }
```

**Before proceeding, re-run a `search_in_dir` sweep for `RigidBodyEntry{` (and
`RigidBodyEntry {`, with a space) across the whole repository to confirm this
truly is the only construction call site** — this campaign's own investigation
found exactly one, but re-confirming immediately before this specific edit
(the one edit in this entire campaign with a real, demonstrated compile-break
history) costs nothing and is the single most important verification step in
this phase.

### 3.4 — `src/Editor/BoneViewerWindow.cpp`: dim/annotate unreachable colliders in the 3D overlay

**(v2 — corrected variable name and matching heuristic, see this phase's own
"v2 change summary," findings #5 and #6).** Locate the existing per-model
Static-collider wireframe pass (Step 2, point 3 above, confirmed lines
1500-1532). This loop already sits inside `if (verletModel != nullptr) {
... }` (the real, confirmed local variable name — a local
`const DynamicChainRigCache::ModelEntry*`, not a member, no `m_` prefix),
immediately AFTER the closing brace of the per-chain
`for (const DynamicChainDefinition& chain : verletModel->chains) { ... }`
loop that draws tree edges (confirmed: this Static-collider loop runs once
PER MODEL, not once per chain, exactly as its own existing comment already
states). Insert the following block immediately BEFORE this existing
wireframe loop, still inside the same `if (verletModel != nullptr) { ... }`
scope:

```cpp
                    // task_manager/verlet-integration-10, PHASE4 - collect
                    // the union of every collider reachable by AT LEAST ONE
                    // collision-enabled chain in this model, so an
                    // unreachable collider (excluded by every chain's own
                    // PMX group/mask) can be drawn more dimly than a
                    // genuinely-active one, instead of looking identical.
                    // Matched to its own RigidBodyEntry by (boneIndex, shape,
                    // shapeSize) as a combined key rather than boneIndex
                    // alone (v2 - see this phase's own "v2 change summary,"
                    // finding #6) - ModelColliderDefinition does not retain
                    // which original PMX rigid-body index it came from, and
                    // a model COULD validly have more than one Static rigid
                    // body on the same bone; the combined key resolves that
                    // ambiguity in every realistic case, for a purely
                    // cosmetic Editor overlay. `group` is masked (`& 0x0Fu`)
                    // before use as a shift amount - see PHASE1's own
                    // GroupBit() doc comment for why this is a safety
                    // requirement, not a style choice.
                    std::vector<bool> colliderIsReachableByAnyChain(verletModel->colliders.size(), false);
                    for (const DynamicChainDefinition& c : verletModel->chains) {
                        if (!c.collisionEnabled) {
                            continue;
                        }
                        for (std::size_t ci = 0; ci < verletModel->colliders.size(); ++ci) {
                            const ModelColliderDefinition& colliderDef = verletModel->colliders[ci];
                            for (const DynamicJointSettings& joint : c.jointSettings) {
                                const std::uint16_t jointBit = static_cast<std::uint16_t>(1u << (joint.group & 0x0Fu));
                                const std::uint16_t colliderBit = static_cast<std::uint16_t>(1u << (colliderDef.group & 0x0Fu));
                                if ((jointBit & colliderDef.collisionMask) != 0 && (colliderBit & joint.collisionMask) != 0) {
                                    colliderIsReachableByAnyChain[ci] = true;
                                    break;
                                }
                            }
                        }
                    }
```

Then, inside the existing per-`RigidBodyEntry` wireframe-drawing loop
(confirmed lines 1519-1532), look up this body's own matching
`ModelColliderDefinition` index by the combined `(boneIndex, shape,
shapeSize)` key (not `boneIndex` alone) and pick a dimmer alpha (e.g.
`IM_COL32(255, 90, 170, 30)` instead of the existing `90`) when the match is
found and `!colliderIsReachableByAnyChain[matchedIndex]` — if no match is
found at all (should not normally happen for a body that passed the same
`motionType == Static` filter `DetectModelColliders()` itself applies, but
never assumed blindly per this codebase's own convention), draw it at the
existing, un-dimmed alpha exactly as today, since "no PHASE1 data available
for this body" must never be visually confused with "PHASE1 confirmed no
chain can reach it."

**If, after re-reading the real current file, this combined-key matching
still feels like a larger restructuring than this phase intends**, the
acceptable, smaller fallback for this phase (unchanged from v1) is: skip the
per-collider dimming entirely and instead add ONE plain, static text line
above the overlay/list (e.g. in the Verlet tree pane, right after the
existing per-chain `"Collision: enabled (collides against this model's
auto-detected colliders)"` line) reading `"(PMX collision-group/layer rules
may exclude some of these for this specific chain - see the Inspector's own
per-chain readout for an exact reachable count.)"` — this still satisfies the
phase's own goal (nothing misleading left unaddressed) with a strictly
smaller, lower-risk diff. Prefer the fuller 3.4 treatment above if the real
file's structure supports it cleanly (it should, per this v2 review's own
direct confirmation of `verletModel`'s scope); use this fallback only if
live re-inspection at implementation time reveals a structural obstacle this
review did not anticipate.

### 3.5 — `src/Editor/BoneViewerWindow.cpp`: per-chain tree-pane text update

Update the existing line (Step 2, point 2, confirmed line 742):

```cpp
        if (chain.collisionEnabled) {
            ImGui::TextDisabled("Collision: enabled (collides against this model's auto-detected colliders, "
                "subject to PMX collision-group/layer rules)");
        }
```

## Step 4 — Verification (code-only)

- After every edit above, re-read the two full call sites in
  `InspectorPanel.cpp` and the affected block(s) in `BoneViewerWindow.cpp`/
  `.h` in full, confirming every brace/parenthesis balances and every new
  local variable/helper is actually declared before first use (this phase
  introduces two new free functions/helpers — `CountCollidersReachableByChain()`
  and the inline reachability-union block — and one new struct field, all of
  which must be visible at their own call sites).
- **Specifically re-confirm `RigidBodyEntry`'s single construction call site
  compiles**: count the braces' own positional arguments (must be exactly 9
  after this edit, matching the struct's own 9 fields in order) — this is
  the one edit in this entire campaign with a demonstrated history of
  breaking compilation in v1; treat it with proportionate care.
- Confirm `GTE_ENABLE_EDITOR`/`GTE_ENABLE_PROJECT_PANEL` preprocessor guards
  around each edited block are left exactly as they were (this phase does
  not change which build configuration compiles these files — only edits
  code that was already inside those same guards).
