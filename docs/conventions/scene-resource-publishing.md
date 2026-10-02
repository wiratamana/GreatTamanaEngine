# Scene-Resource Publishing (`NamedSceneResourceKey()`)

`rg::NamedSceneResourceKey(const char* name, rg::RenderViewId view)`
(`src/Renderer/RenderGraph/RenderPipeline.h`/`.cpp`) is a generic, Core-owned
helper that turns a human-readable, string-named resource identity (e.g.
`"Fog.VolumeTexture"`) plus a render view into a real `rg::RenderPassId` key,
suitable for `RenderPassBlackboard::Publish()`/`Fetch()`. It exists so that any
render pass can hand a resource to any later, unrelated pass declared against
the same blackboard, in the same frame, under a name a reader can actually
recognize — without that pass hand-rolling its own FNV-1a key-mixing logic.
This is the correct default choice for a low-frequency, ad hoc, unbounded,
cross-feature handoff; it is not a new resource mechanism, and `Publish()`/
`Fetch()` themselves (already general-purpose, already able to take any
caller-chosen key) are completely unchanged by this helper's existence.

## When to use this vs. the bounded scene-services registry

`SceneServiceBlackboardKey(std::uint32_t slotIndex, rg::RenderViewId view)`
(`src/Renderer/SceneServicesDescriptorSet.h`) already solves a DIFFERENT
problem: a small, fixed, bounded set of exactly `kSceneServiceSlotCount` (8)
slots, each with a durable, pre-assigned shader binding number, sampled
directly by a shader through a stable `layout(set = 1, binding = N)`
declaration. That registry is the right tool when a value needs to reach a
shader binding at all, and when the total number of such values is small and
known ahead of time.

`NamedSceneResourceKey()` is for the opposite shape: an unbounded, ad hoc set
of string-named values exchanged entirely through the blackboard — never
through a binding number, and never sampled by a shader directly from a slot
index. Do not let an adopter of this helper grow into a de facto
per-material, high-frequency, or large-cardinality lookup table published
through this path — that usage pattern belongs in the bounded scene-services
registry instead, where the slot count is deliberately kept small and every
slot is a reviewed, permanent addition.

## The view-qualification rule

Any pass that writes a resource must `Publish()` it under
`NamedSceneResourceKey("Owner.ResourceName", view)`, using the exact view its
own callback was invoked with THIS call — never a cached view from an earlier
invocation, and never a view borrowed from a different call site. Any reader
must `Fetch()` the identical key for the exact view it actually cares about.

Skipping this is a real, silent failure mode, not a theoretical one:
`RenderPassBlackboard::Publish()`'s own documented contract is last-publish-
wins. If two call sites — say, a Game-view pass and a Scene-view pass of the
same provider — both publish under a view-agnostic key (the view argument
dropped, or hardcoded to one view), the later-processed one silently
overwrites the earlier one's value within the same declared frame, with no
diagnostic of any kind: nothing distinguishes an honest overwrite between two
views from a genuine same-view double-publish, so neither diagnostic below
catches this particular mistake. Always pass the real, current view into
`NamedSceneResourceKey()` — do not reuse a single hardcoded view across what
is actually a per-view publish.

## The naming-clash risk and its real defense

Correct, collision-resistant hashing guarantees that two genuinely DIFFERENT
`(name, view)` pairs never alias onto the same key. It guarantees nothing
about two unrelated call sites that happen to choose the exact SAME name text
by coincidence — that is a naming clash, not a hashing defect, and at the
hashing level it is completely indistinguishable from a deliberate republish
of the same logical resource. Two real, debug-only, soft diagnostics are the
actual defense against this — not merely a documented hope that nobody ever
picks the same string twice:

- A null or empty `name` is logged once per process lifetime (both are
  treated as the same empty-string key, so this never crashes in any build
  configuration — it is purely a "you probably meant to pass a real name"
  nudge).
- Two `Publish()` calls under the identical key within one declared frame are
  logged exactly once per offending key per frame — not once per overwrite,
  so a third, fourth, or further `Publish()` under the same still-offending
  key produces no further noise that frame. This check lives inside
  `RenderPassBlackboard::Publish()` itself, so it applies to every caller of
  `Publish()`, not only to callers that go through this helper.

Both diagnostics are `#ifndef NDEBUG`-guarded, soft `std::fprintf(stderr, ...)`
warnings — never a hard assert, never a crash, in any build configuration —
and both compile to nothing at all in a release build.

## Which blackboard instance

State this plainly, up front, because it is easy to miss: this engine owns
exactly TWO independent `rg::RenderPipeline` instances, and each one owns its
OWN, separate `RenderPassBlackboard`. One is the long-lived offscreen
pipeline's blackboard, shared by every plugin-reachable feature stage
(PreOpaque, PostOpaque, PostTransparent, PostComposite, PreUI) across the
whole frame. The other belongs to the separate Present-stage pipeline, and is
constructed fresh, as a plain local variable, and discarded at the end of
every single frame — nothing from the offscreen blackboard is ever copied
into it.

"Any later pass can discover a published key" is true ONLY between two passes
declared against the exact same blackboard instance, within the exact same
frame-declaration call. A key published on one pipeline's blackboard is
PERMANENTLY invisible to the other — with no error, and no log line, in
either build configuration. A `Fetch()` issued against the wrong instance
returns the identical `std::nullopt` a key that was genuinely never published
anywhere would also return; there is no way to tell the two situations apart
from the call site alone. Know which pipeline your pass is declared under
before assuming a published key will ever be visible to a given reader.

## Side-channel honesty obligation

A `Publish()` call is a real, observable side effect — exactly the shape
[`render-pass-side-channel-honesty.md`](render-pass-side-channel-honesty.md)'s
Clause C already governs. That page's own reasoning is not repeated here;
read it directly. The obligation it imposes applies to any provider that
calls `Publish()` under a `NamedSceneResourceKey(...)` key exactly as it does
to any other blackboard publish:

- If the publishing provider is independently toggleable, it must first pass
  `src/Renderer/RenderGraph/RenderPassToggleGuard.h`'s
  `ShouldDeclareBuiltInPassThisFrame()` gate, at the very top of its body,
  before that `Publish()` call — not merely before its own `RenderPassDesc`
  reaches the graph.
- The moment any real feature adopts this helper for a value some OTHER,
  independently-toggled pass might read back and reproduce an effect from,
  that key must be added to
  `src/Editor/FrameDebuggerSideChannelChecker.h`'s
  `KnownRiskBlackboardKeyRules()`, as part of that same change, mirroring the
  shape of its existing entries exactly.

Neither of these is optional, and neither is automatically enforced for a
`NamedSceneResourceKey()`-produced key specifically — the generic Clause C
detector only ever catches keys that have actually been hand-added to
`KnownRiskBlackboardKeyRules()`, so a new adopter that skips this step is a
real, silent gap, not a theoretical one.

## Worked example

A publisher pass hands a resource to a later, unrelated reader pass, both
qualifying the key by the exact same current view:

```cpp
// Publisher (e.g. a PostOpaque provider):
frame.blackboard.Publish<rg::TextureHandle>(
    rg::NamedSceneResourceKey("Fog.VolumeTexture", frame.currentView),
    fogVolumeHandle);

// Reader (a later, unrelated provider declared against the same blackboard,
// same frame):
const std::optional<rg::TextureHandle> fogVolume =
    frame.blackboard.Fetch<rg::TextureHandle>(
        rg::NamedSceneResourceKey("Fog.VolumeTexture", frame.currentView));
if (fogVolume.has_value()) {
    // use *fogVolume
}
```

Both sides pass the exact same name string and the exact same live
`frame.currentView` — never a cached/stale view, never a different literal
spelling of the name on either side.
