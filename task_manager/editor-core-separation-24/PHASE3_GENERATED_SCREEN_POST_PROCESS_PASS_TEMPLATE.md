# PHASE3 — The Generated Screen Post-Process Pass Template

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first, in full).
Design doc Step: Step 3 of
`DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_02_EDITOR_INTEGRATION_2026-09-29.txt`
— **read that Step's own full text before starting; the exact template text
quoted there is the CONTENT the generated `.cpp` file must contain once
`<Name>`/`<Priority>` are substituted in. This phase file's own internal
template-builder implementation below intentionally uses different,
collision-safe substitution TOKENS than the source document's own
`__NAME__`/`__PRIORITY__` sketch (see Step 3's own "Two placeholder tokens"
sub-section for exactly why, and confirm this deviation is still applied
before writing any code — it closes a real, confirmed bug the source
document's own sketch does not protect against).**

## Step 1: The Goal

Write a new, pure, free function,
`BuildScreenPostProcessPassCppContent(const std::string& name, std::int32_t priority)`,
that returns the COMPLETE, real, immediately-compileable content of
`Assets/<Name>ScreenPass.cpp` — a genuinely working translucent-red-tint
Screen Post-Process Pass, not a placeholder stub. This is a pure string
builder with no filesystem/side-effect of its own — PHASE6 is the only
caller, and is the one that actually writes it to disk.

## Step 2: The Situation

`src/Editor/EditorProjectLifecycleCapability.cpp` already has THREE precedent
template builders to mirror the style of:
`BuildRenderPassCppContent(name)` (lines 134-167), `BuildComputeShaderGlslContent`/
`BuildComputePassCppContent` (lines 171-216), `BuildVertexShaderContent`/
`BuildFragmentShaderContent` (lines 222-239) — all built via the file's own
existing `ReplaceAll(std::string text, const std::string& token, const std::string& replacement)`
helper (lines 114-122), which does a plain, non-regex, loop-based
find-and-replace-all — this new function reuses that SAME helper, never a
`<regex>`/new substitution mechanism, extended to substitute TWO tokens
instead of one (see Step 3's own token-choice sub-section for the exact two
literal tokens to use — deliberately NOT `__NAME__`/`__PRIORITY__`, unlike the
three precedent builders above, which only ever substitute ONE token each and
are therefore not exposed to the hazard described below).

`ProjectRenderFeatureCallback` (`src/Core/Plugins/ProjectRenderFeatureCallback.h`)
is `std::function<void(rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D)>`
(confirmed via `docs/conventions/project-assembly-system.md` lines 313-319, and
independently re-confirmed directly against that header file itself) — the
generated template's own lambda signature below matches this exactly.
`gte::RenderFeatureStage`/`gte::RenderFeatureBlendMode` are bare `gte::` types
(`plugins/gte_plugin_abi/RenderFeatureDescriptor.h`) — never `gte::rg::` (that
namespace is reserved for the render-graph vocabulary the same callback is
also handed: `RenderGraphBuilder`, `TextureHandle`, `PassContext`, `PassKind`,
`RenderPassDrawKind`, `RenderPassEvent`). Double-check this distinction stays
correct in the generated `#include`/type-usage below — mixing them up would
be a real, silent compile error at PROJECT-COMPILE time (inside the user's own
`.dll` build), not caught anywhere in the engine's own build.

**`BuildScreenPostProcessPassCppContent()` itself will be placed in the SAME
anonymous namespace as its three precedent siblings above, which means it has
internal linkage — exactly like `BuildRenderPassCppContent()`/
`BuildComputePassCppContent()`/etc. today, NONE of which has, or needs, its
own direct/isolated Tier-1 test (confirmed: `tests/Editor/AssetScaffoldTemplateTests.cpp`
already exists and its own header comment explicitly documents this — it
exercises every one of those three existing template builders ONLY through
the one real, externally-linked, public entry point, `CreateAssetScaffold()`,
never by calling a builder function directly). This phase's own new builder
follows the SAME pattern for the SAME reason — do not attempt to give it a
separate, direct unit test in isolation from a different translation unit;
PHASE6's own dispatch-branch test (which calls the real, public
`CreateAssetScaffold(ScreenPostProcessPass, ...)` entry point) is what proves
this function's real output, end to end, once PHASE6 lands.**

## Step 3: The Plan

### 3.1 — Token choice: a real, confirmed bug in the naive `__NAME__`/`__PRIORITY__` sketch, and its fix

The source design document's own Step 3 sketch substitutes two SEQUENTIAL,
literal tokens, `__NAME__` then `__PRIORITY__`, via two back-to-back
`ReplaceAll()` calls. This is safe for `__NAME__` (see below) but NOT safe for
`__PRIORITY__`, for a concrete, mechanically-reproducible reason:

`name` is validated by `IsValidProjectAssemblyIdentifierName()`
(`src/Core/Plugins/ProjectAssemblyNameValidation.h`) before this function is
ever called — confirmed, by reading that header directly, to accept any
string matching `^[A-Za-z_][A-Za-z0-9_]*$` (plus a Windows-reserved-device-name
exclusion). That regex-shaped rule has NO exclusion for any particular
substring — a perfectly valid project/pass name like `"Foo__PRIORITY__Bar"`
passes it cleanly. If a user picks such a name:

1. The FIRST `ReplaceAll(kTemplate, "__NAME__", name)` call substitutes `name`
   (e.g. `"Foo__PRIORITY__Bar"`) into every `__NAME__` occurrence in the
   template — including inside the generated function's own name,
   `Register__NAME__ScreenPass` → `RegisterFoo__PRIORITY__BarScreenPass`. This
   step ITSELF is fine (see the `__NAME__`-self-collision note below).
2. The SECOND `ReplaceAll(content, "__PRIORITY__", std::to_string(priority))`
   call then finds and replaces EVERY occurrence of the literal text
   `"__PRIORITY__"` in the ALREADY-`__NAME__`-substituted content — which now
   includes the fragment that came FROM the user's own name, not just the one,
   original `/*priority=*/__PRIORITY__,` spot the template author intended.
   The generated function name silently becomes `RegisterFoo0BarScreenPass`
   (assuming `priority == 0`) instead of the correct
   `RegisterFoo__PRIORITY__BarScreenPass` → this is a REAL, confirmed
   corruption: PHASE6's own separately-computed
   `registerFn = "Register" + name + "ScreenPass"` string (used for the
   auto-wire forward declaration/call line) would then read
   `"RegisterFoo__PRIORITY__BarScreenPass"` — literally MISMATCHING the
   generated `.cpp` file's own actual function name — producing a genuine
   linker error (undefined reference) the very first time the project is
   compiled, for a perfectly valid, scaffolding-tool-accepted name.

**The fix, applied in this phase**: use a substitution token for priority that
can NEVER legally appear as a substring of any name
`IsValidProjectAssemblyIdentifierName()` accepts. Since that validator only
ever allows `[A-Za-z0-9_]` characters, ANY token containing a character
outside that set is unconditionally collision-proof, regardless of what a
user names their pass, now or in the future, with zero extra validation
needed anywhere. This phase uses:

- `__NAME__` — kept, UNCHANGED, exactly like the three precedent builders
  already use. Self-collision is impossible to make harmful here even though
  `name` itself COULD legally equal `"__NAME__"` — replacing every
  `"__NAME__"` occurrence with the literal string `"__NAME__"` is a no-op
  transformation (the text is byte-for-byte identical before and after), and
  there is no SECOND, later substitution pass that could ever re-scan and
  further mutate a `__NAME__`-shaped fragment the way the priority token's own
  second pass does above — this token is only ever substituted ONCE, last,
  with nothing applied afterward that could reinterpret its result.
- `@@PRIORITY@@` — a NEW token, replacing the source document's own
  `__PRIORITY__` sketch, chosen specifically because `@` can never appear in
  any name this scaffolding tool accepts. Substitute `__NAME__` FIRST, then
  `@@PRIORITY@@` SECOND (same order as the source document's own sketch) —
  the order no longer matters for correctness once the priority token itself
  is collision-proof, but keeping `__NAME__` first still matches this file's
  existing convention and every precedent template's own single-token
  ordering habit.

This changes ONLY the internal substitution token used inside this phase's
own template-builder source code — it has ZERO effect on the FINAL generated
`.cpp` file's own text, which still reads the literal, human-readable
`/*priority=*/0,` comment-and-number exactly as the source document
specifies (PHASE5's `ComputeNextScreenPassPriority()` scans for the literal
`"/*priority=*/"` substring in the FINAL, already-substituted file text, which
is completely unaffected by which internal token this builder used to get
there).

### 3.2 — The function itself

Add this new function, placed alongside the other `Build*CppContent()`
helpers in the same anonymous namespace:

```cpp
// Kind 4 - Screen Post-Process Pass (Assets/<Name>ScreenPass.cpp, ONE file).
// Unlike BuildRenderPassCppContent() above, this is a REAL, WORKING tint, not
// a placeholder - see docs/conventions/project-assembly-system.md's
// "On-screen Game View compositing" section for the mechanism this calls
// into (Core::RegisterProjectRenderFeature(), editor-core-separation-23
// campaign, BIG-STEP 1).
//
// Uses "@@PRIORITY@@", NOT "__PRIORITY__", as its second substitution token -
// see this phase's own Step 3.1 for the exact, confirmed name-collision bug
// this choice closes (a valid project/pass name may legally contain the
// literal substring "__PRIORITY__", which would otherwise corrupt the
// generated function name during the second ReplaceAll() pass).
std::string BuildScreenPostProcessPassCppContent(const std::string& name, std::int32_t priority)
{
    static const char* kTemplate =
        "// __NAME__ScreenPass.cpp - generated by the Editor's \"Create -> Screen\n"
        "// Post-Process Pass\" action. Once wired in and compiled, this applies a\n"
        "// simple, translucent red tint directly onto the real, on-screen Game\n"
        "// View - no shader file, no compute dispatch, no manual texture import,\n"
        "// no barrier code required.\n"
        "//\n"
        "// WIRING: the Editor's scaffolding tool tries to automatically insert\n"
        "// the one required call, Register__NAME__ScreenPass(core);, into your\n"
        "// project's own RegisterProject() function (Assets/<ProjectName>Game.cpp)\n"
        "// for you. Check the Create-Asset window's own success message (or this\n"
        "// file's own Compile output) to see whether that succeeded. If your\n"
        "// project was created before auto-wiring existed (no\n"
        "// \"GTE_AUTO_REGISTER_ANCHOR\" comment in that file), you must add the line\n"
        "// yourself, exactly as shown, inside RegisterProject().\n"
        "#include \"../../../src/Core/Core.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphBuilder.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphTypes.h\"\n"
        "\n"
        "#include <array>\n"
        "\n"
        "void Register__NAME__ScreenPass(gte::Core& core)\n"
        "{\n"
        "    const bool registered = core.RegisterProjectRenderFeature(\n"
        "        \"__NAME__.ScreenTint\",\n"
        "        gte::RenderFeatureStage::PostComposite,\n"
        "        gte::RenderFeatureBlendMode::AlphaOver,\n"
        "        /*priority=*/@@PRIORITY@@, // auto-assigned - keeps multiple Screen Post-Process Passes in this same project collision-free; change by hand only if you want a specific relative blend order. This number is only guaranteed unique among YOUR OWN project's Screen Post-Process Passes - a currently-loaded plugin may already use the same priority in the same stage; that is safe (logged, never crashing), just check the engine log if the blend order looks wrong.\n"
        "        [](gte::rg::RenderGraphBuilder& builder, gte::rg::TextureHandle privateTarget, VkExtent2D /*extent*/) {\n"
        "            // This is the whole effect: clear your own private target to\n"
        "            // a translucent red every frame - only Render Graph API calls\n"
        "            // are used here (builder.AddRenderPass()/PassBuilder::\n"
        "            // WriteColorAttachment()) - never bypass this by touching a\n"
        "            // VkImage/VkCommandBuffer directly. Tagged\n"
        "            // RenderPassEvent::AfterEverything to match every other pass\n"
        "            // this on-screen compositing chain already contains.\n"
        "            builder.AddRenderPass(\"__NAME__.ScreenTint.Clear\", gte::rg::PassKind::Graphics,\n"
        "                [privateTarget](gte::rg::RenderGraphBuilder::PassBuilder& pass) {\n"
        "                    pass.WriteColorAttachment(privateTarget, std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f });\n"
        "                },\n"
        "                [](gte::rg::PassContext& /*ctx*/) {\n"
        "                    // Intentionally empty - the clear color above IS the\n"
        "                    // entire effect for this starting point. TODO:\n"
        "                    // replace with a real draw or compute dispatch once\n"
        "                    // you're ready - see\n"
        "                    // Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp.\n"
        "                },\n"
        "                gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);\n"
        "        });\n"
        "    // `registered` is intentionally unused here - Core::RegisterProjectRenderFeature()\n"
        "    // already logs a loud, specific reason (duplicate name, over-length\n"
        "    // name, or no free project-feature slot available) on failure; this\n"
        "    // generated stub has nothing more useful to add on top of that log\n"
        "    // line. A future hand edit is free to check `registered` and react\n"
        "    // however that project needs to.\n"
        "}\n";
    std::string content = ReplaceAll(kTemplate, "__NAME__", name);
    content = ReplaceAll(content, "@@PRIORITY@@", std::to_string(priority));
    return content;
}
```

Notes specific to implementing this correctly:

- `ReplaceAll()` is applied TWICE, sequentially (`__NAME__` first, then
  `@@PRIORITY@@`) — per Step 3.1 above, `@@PRIORITY@@` can never legally
  appear inside `name` (the identifier validator excludes `@` outright), so
  the SECOND substitution can only ever touch the ONE, original
  `/*priority=*/@@PRIORITY@@,` spot the template itself declares — this is
  the actual, confirmed guarantee (not "unlikely to collide", but
  "structurally cannot collide, by construction").
- `std::to_string(priority)` can only ever produce ASCII digits (and a
  leading `-` for a negative value, which `ComputeNextScreenPassPriority()`,
  PHASE5, never actually produces from its own `(highest + 1)` arithmetic
  starting at `0`) — neither can ever legally contain `@` either, so there is
  no SECOND-ORDER hazard from the replacement VALUE of the second
  substitution accidentally containing `__NAME__` or `@@PRIORITY@@` text of
  its own.
- `(void)registered` is NOT written — the generated code simply never reads
  the `registered` local beyond the comment explaining why; confirm this
  does not itself trigger an "unused variable" warning in project-compile
  builds severe enough to break the build (this repository enables no
  `-Werror` for Project Assembly builds either — confirmed by this whole
  system's own pre-existing `BuildRenderPassCppContent()`/
  `BuildComputePassCppContent()` templates, which already leave several
  parameters commented-out/unused with the same `(void)`-free convention in
  places — but double check by actually compiling a scaffolded file in Step 4
  below rather than assuming).
- Nothing in this function touches the filesystem, `ActiveProjectAssemblyState`,
  or priority COMPUTATION — `priority` arrives as an already-resolved
  `std::int32_t` parameter; PHASE5 is what computes it, PHASE6 is what calls
  this function with that computed value.

## Step 4: Verification (this phase only)

1. Incremental build (`cmake --build build`) — confirm
   `EditorProjectLifecycleCapability.cpp` still compiles cleanly with this new,
   currently-UNCALLED function added (an unused static function in an
   anonymous namespace may itself warn on some toolchains — if this build
   emits such a warning, note it in the completion report; do not silence it
   with an `[[maybe_unused]]` unless the build genuinely fails or the phase
   file for PHASE6 says otherwise, since PHASE6 calls this function within
   the SAME campaign and the warning window is expected to be short-lived).
2. A focused, temporary manual check: call this function directly from a
   throwaway `main()`-less unit test snippet, or (simpler) temporarily invoke
   it from a scratch call site, dump its return value to a `.cpp` file via
   `write_file`, and attempt to compile THAT generated file in isolation
   (`gcc`/`g++` tool, `-c`, with the right include paths resolved relative to
   a real `Assets/` folder location, e.g. copy it into
   `Projects/ProjectAssemblyProbe/Assets/` temporarily) to prove the generated
   C++ text itself is syntactically and semantically valid BEFORE PHASE6 wires
   it into the real dispatch path. Also include ONE deliberately adversarial
   case in this same manual check: call the function with
   `name == "Foo__PRIORITY__Bar"` (the exact hazard Step 3.1 describes) and
   confirm the generated text's function name reads
   `RegisterFoo__PRIORITY__BarScreenPass` — i.e. completely UNCORRUPTED,
   proving the `@@PRIORITY@@` token fix actually closes the gap, not merely in
   theory. Revert/delete the temporary file and any scratch call site
   afterward — this phase adds ONLY the pure builder function itself to the
   real source tree.
3. `tests/Editor/AssetScaffoldTemplateTests.cpp` already exists (confirmed) and
   already documents, in its own header comment, why every template builder in
   this file is tested ONLY through the public `CreateAssetScaffold()` entry
   point, never directly (internal linkage — see this phase's own Step 2
   note). Do NOT add a direct test for `BuildScreenPostProcessPassCppContent()`
   itself in this phase — PHASE6's own dedicated dispatch-branch test is the
   correct, and only, place that exercises this function's real output,
   end-to-end, through `CreateAssetScaffold()`. This phase's own Step 4 items 1
   and 2 above (compile-check + adversarial name check) are this phase's own
   complete verification story.

## Step 5: Completion

Write `PHASE3_COMPLETION_REPORT.md` in this same folder, including the
isolated-compile proof's own evidence (the exact `gcc`/`g++` command and its
output) AND the adversarial-name-check's own evidence (the exact generated
text for `name == "Foo__PRIORITY__Bar"`, confirming it is uncorrupted).
`git_add` + `git_commit` covering the code change and the report. Do
not run a full build/regression here (Locked Decision 2, PHASE0).
