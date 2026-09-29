# PHASE1 — Asset Scaffolding Capability Interface + Corrected Templates

Parent: `PHASE0_MASTER_STRATEGY.md` (read that FIRST — it has the full
Goal/Situation/risk register/LDDs this phase restates by number only).

No UI, no HTTP route, no `ProjectPanel.cpp` change in this phase. Pure
interface + implementation + unit tests. Next phase (`PHASE2_*.md`) depends
on this one only for the `AssetScaffoldKind` enum's existence.

## STEP 1 — The Goal

A single new method, callable from ANY thread, that:
1. Fails cleanly with a clear message if there is no active project.
2. Validates the requested base name with the SAME validator every other
   name in this whole 5-file plan uses.
3. Computes the exact file list a given `AssetScaffoldKind` produces, checks
   EVERY one of them for a pre-existing, case-INSENSITIVE collision under
   the active project's `Assets/` folder, and writes NOTHING at all if any
   collision is found.
4. Writes every file for that kind, with `<Name>` substituted throughout,
   using CORRECTED (compiling, real-API) template content.
5. Returns a structured outcome (which files were created, and — for the
   two `.cpp`-producing kinds — a loud "remember to wire this in" reminder
   message).

## STEP 2 — The Situation (what exists right now, exact citations)

- `src/Core/EditorCapabilities.h` — `IProjectLifecycleCapability` is the
  last interface in the file, ending `}; } // namespace gte`. This phase
  inserts a NEW interface AFTER it, in the same file, same namespace.
- `src/Core/Plugins/ProjectAssemblyNameValidation.h` —
  `bool IsValidProjectAssemblyIdentifierName(const std::string& name,
  std::string& outErrorMessage);` — already used by
  `CreateNewProjectAssembly()`. Reuse this EXACT function; do not write a
  second validator.
- `src/Editor/ActiveProjectAssemblyState.h` —
  `ActiveProjectAssemblyInfo::GetActive()` already returns
  `hasActiveProject`, `name`, `sourceDirectory`, `assetsDirectory`,
  `isCompiled`, `isLoaded`. This phase's new method calls
  `ActiveProjectAssemblyState::Instance().GetActive()` and uses
  `.hasActiveProject`/`.assetsDirectory` only — never `.isCompiled`/
  `.isLoaded` (irrelevant to scaffolding), never calls `SetActive()`
  (scaffolding a file never changes WHICH project is active).
- `src/Editor/EditorProjectLifecycleCapability.h/.cpp` — already has
  `#include "ActiveProjectAssemblyState.h"`, `#include
  "../Core/Plugins/ProjectAssemblyNameValidation.h"`, `#include
  "../Core/Logging.h"` (for `GTE_LOG_INFO`/`GTE_LOG_ERROR`), and a private,
  anonymous-namespace `WriteTextFile(path, content)` helper this phase
  reuses verbatim (do not duplicate it).
- `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp` — the ONE real,
  live, currently-compiling example of `RegisterProjectRenderPassProvider()`
  usage. Every template below is grounded directly in this file's own real
  call shape, re-verified against `src/Renderer/RenderGraph/RenderPipeline.h`
  lines 403-411 (`RenderPassProvider`'s real `using` alias,
  `ProviderScope`'s real two-value enum).
- `cmake/templates/ProjectAssemblyExports.h` —
  `GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterFn)` expands to
  `extern "C" __declspec(dllexport) void GTE_RegisterProject(gte::Core&
  core) { RegisterFn(core); }` — confirms, mechanically, that calling this
  macro TWICE in the SAME `.dll`'s link unit produces two definitions of
  the same exported symbol name, `GTE_RegisterProject` — a real linker
  error. This is why every template below defines an ordinary,
  non-exported free function instead, and NEVER calls this macro itself.

## STEP 3 — The Plan (exact code)

### 3.1 — `src/Core/EditorCapabilities.h` additions

Insert AFTER the closing `};` of `IProjectLifecycleCapability`, BEFORE the
final `} // namespace gte`:

```cpp
// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4) - answers "can this build scaffold a new Render Pass/Compute
// Shader/Shader-Pair source file into the CURRENTLY ACTIVE Project
// Assembly's Assets/ folder". A deliberately SEPARATE interface from
// IProjectLifecycleCapability immediately above - scaffolding a file inside
// an already-active project is a genuinely different capability question
// from creating/opening the project itself (this file's own long-standing
// "one interface per genuinely new capability gap" convention). Mirrors
// every capability interface above: gte_core-tier code (NetworkServer.cpp)
// holds only a nullable pointer; nullptr means every route backed by this
// interface answers 503.
enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };

class IAssetScaffoldingCapability {
public:
    virtual ~IAssetScaffoldingCapability() = default;

    struct ScaffoldOutcome {
        bool success = false;
        std::string errorMessage;              // meaningful only when success == false
        std::vector<std::string> createdFiles; // relative to Assets/, meaningful only when success == true
        std::string reminderMessage;           // "" for ShaderPair (no companion .cpp is generated for that kind)
    };

    // Callable from ANY thread - pure filesystem I/O against the CURRENT
    // ActiveProjectAssemblyState, plus (LDD-CA1, PHASE0_MASTER_STRATEGY.md)
    // deliberately NO CMake reconfigure call - gte_add_project()'s own
    // CONFIGURE_DEPENDS glob over Assets/*.cpp (and the *.vert/*.frag/*.comp
    // shader glob) already re-triggers CMake's configure step automatically
    // on the NEXT `cmake --build`, unlike CreateNewProjectAssembly()'s own
    // brand-new-FOLDER case. Never touches ActiveProjectAssemblyState's own
    // "which project is active" state - only reads it.
    virtual ScaffoldOutcome CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name) = 0;
};
```

Also add `#include <vector>` at the top of this header if not already
present (it already is — `std::vector<std::string>` is used by
`IHotReloadDebugCapability` above; no change needed there, just confirm
before assuming).

### 3.2 — `src/Editor/EditorProjectLifecycleCapability.h` additions

```cpp
class EditorProjectLifecycleCapability : public IProjectLifecycleCapability,
                                          public IAssetScaffoldingCapability {
public:
    // ... existing methods unchanged ...

    // editor-core-separation-18 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 4). See IAssetScaffoldingCapability's own doc comment
    // (Core/EditorCapabilities.h) for the full contract.
    ScaffoldOutcome CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name) override;

private:
    // ... existing private members unchanged ...
};
```

(`ScaffoldOutcome` resolves via `IAssetScaffoldingCapability::ScaffoldOutcome`
— no `using` alias needed since this class already derives from that
interface; write the override exactly as shown, matching how
`CreateProjectOutcome`/`OpenProjectOutcome` are already used unqualified
inside this same class's method bodies today.)

### 3.3 — Corrected per-kind template content (`EditorProjectLifecycleCapability.cpp`,
anonymous namespace, alongside the existing `BuildGameStubCppContent()`)

**Substitution helper** (new, small, shared by every template builder
below — do NOT hand-roll string replacement three separate times):

```cpp
// A tiny, dependency-free "replace every occurrence of a literal token"
// helper - std::string::find/replace in a loop, exactly like this file's
// own existing style (no <regex> anywhere in src/Core/, matching
// ProjectAssemblyNameValidation.h's own stated convention).
std::string ReplaceAll(std::string text, const std::string& token, const std::string& replacement)
{
    std::size_t position = 0;
    while ((position = text.find(token, position)) != std::string::npos) {
        text.replace(position, token.size(), replacement);
        position += replacement.size();
    }
    return text;
}
```

**Kind 1 — Render Pass** (`Assets/<Name>RenderPass.cpp`, ONE file):

```cpp
std::string BuildRenderPassCppContent(const std::string& name)
{
    static const char* kTemplate =
        "// __NAME__RenderPass.cpp - generated by the Editor's \"Create -> Render\n"
        "// Pass\" action. This function does NOTHING until you call it - add\n"
        "// exactly one line, Register__NAME__RenderPass(core);, inside your\n"
        "// project's own RegisterProject() function (see Assets/<ProjectName>Game.cpp).\n"
        "#include \"../../../src/Core/Core.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphBuilder.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphTypes.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderPipeline.h\"\n"
        "\n"
        "#include <vector>\n"
        "\n"
        "void Register__NAME__RenderPass(gte::Core& core)\n"
        "{\n"
        "    core.RegisterProjectRenderPassProvider(\"__NAME__.RenderPass\", gte::rg::ProviderScope::Once,\n"
        "        [](const gte::rg::RenderPassFrameContext& frame, std::vector<gte::rg::RenderPassDesc>& outPasses) {\n"
        "            // TODO: mint any transient texture via frame.builder.CreateTexture()\n"
        "            // BEFORE constructing a gte::rg::RenderPassDesc below, then append it\n"
        "            // via outPasses.push_back(std::move(desc)) - see\n"
        "            // docs/conventions/project-assembly-system.md, \"Capability #2\",\n"
        "            // Findings B/E/F, and Assets/HelloGame.cpp in\n"
        "            // Projects/ProjectAssemblyProbe/ for a complete, real, working\n"
        "            // example to copy from. If your pass writes a transient texture\n"
        "            // nobody else reads, also append it to frame.finalTextureOutputs,\n"
        "            // or RenderGraphCompiler's backward-reachability culling scan will\n"
        "            // silently drop this whole pass.\n"
        "            (void)frame;\n"
        "            (void)outPasses;\n"
        "        });\n"
        "}\n";
    return ReplaceAll(kTemplate, "__NAME__", name);
}
```

(The `(void)frame; (void)outPasses;` lines are load-bearing, not
decorative — they silence an "unused parameter" warning for the
deliberately-empty stub body, matching this codebase's own zero-new-warning
CI expectation; confirm this against the project's actual warning flags
before assuming it's needed, but it is always harmless to include.)

**Kind 2 — Compute Shader** (TWO files: `Assets/<Name>.comp` +
`Assets/<Name>ComputePass.cpp`):

```cpp
std::string BuildComputeShaderGlslContent(const std::string& /*name*/)
{
    return
        "#version 450\n"
        "layout(local_size_x = 16, local_size_y = 16) in;\n"
        "void main() {\n"
        "    // TODO: your compute work here.\n"
        "}\n";
}

std::string BuildComputePassCppContent(const std::string& name)
{
    static const char* kTemplate =
        "// __NAME__ComputePass.cpp - generated by the Editor's \"Create -> Compute\n"
        "// Shader\" action. This function does NOTHING until you call it - add\n"
        "// exactly one line, Register__NAME__ComputePass(core);, inside your\n"
        "// project's own RegisterProject() function.\n"
        "#include \"../../../src/Core/Core.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphBuilder.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderGraphTypes.h\"\n"
        "#include \"../../../src/Renderer/RenderGraph/RenderPipeline.h\"\n"
        "#include \"../../../src/Renderer/ComputePipeline.h\"\n"
        "#include \"../../../src/Renderer/ComputeDescriptorSet.h\"\n"
        "#include \"../../../src/Renderer/ComputeDispatch.h\"\n"
        "\n"
        "#include <optional>\n"
        "#include <vector>\n"
        "\n"
        "namespace {\n"
        "std::optional<gte::ComputePipeline> g___NAME__ComputePipeline;\n"
        "}\n"
        "\n"
        "void Register__NAME__ComputePass(gte::Core& core)\n"
        "{\n"
        "    // TODO: build g___NAME__ComputePipeline on first use (see\n"
        "    // Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp for a complete,\n"
        "    // real, working example - Renderer::CreateComputePipeline(), NEVER a\n"
        "    // raw ComputePipeline constructor call), referencing the compiled\n"
        "    // shader at the bare relative path\n"
        "    // \"project_assemblies/shaders/__NAME__.comp.spv\", then register a\n"
        "    // render pass provider the same way the generated __NAME__RenderPass.cpp\n"
        "    // (if you also created one) does, whose execute step dispatches it.\n"
        "    (void)core;\n"
        "}\n";
    return ReplaceAll(kTemplate, "__NAME__", name);
}
```

**Kind 3 — Vertex/Fragment Shader Pair** (TWO plain text files, no
companion `.cpp`, matching the master-plan file's own explicit, narrower
reading — Non-Goal: "does NOT build a full, opinionated graphics-pipeline
scaffold"):

```cpp
std::string BuildVertexShaderContent(const std::string& /*name*/)
{
    return
        "#version 450\n"
        "void main() {\n"
        "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0); // TODO: real vertex transform.\n"
        "}\n";
}

std::string BuildFragmentShaderContent(const std::string& /*name*/)
{
    return
        "#version 450\n"
        "layout(location = 0) out vec4 outColor;\n"
        "void main() {\n"
        "    outColor = vec4(1.0, 0.0, 1.0, 1.0); // Placeholder \"hot magenta\" - unmistakably a stub.\n"
        "}\n";
}
```

### 3.4 — Per-kind file-list + collision + write logic (the real
`CreateAssetScaffold()` body)

```cpp
namespace {
// One entry per file a given kind produces - {relativeFileName, content}.
// Order matters for createdFiles' own reported order, but NOT for
// correctness (the collision scan below checks every entry before writing
// any of them).
struct ScaffoldFileSpec {
    std::string relativeFileName;
    std::string content;
};

std::vector<ScaffoldFileSpec> BuildScaffoldFileSpecs(AssetScaffoldKind kind, const std::string& name)
{
    switch (kind) {
    case AssetScaffoldKind::RenderPass:
        return { { name + "RenderPass.cpp", BuildRenderPassCppContent(name) } };
    case AssetScaffoldKind::ComputeShader:
        return {
            { name + ".comp", BuildComputeShaderGlslContent(name) },
            { name + "ComputePass.cpp", BuildComputePassCppContent(name) },
        };
    case AssetScaffoldKind::ShaderPair:
        return {
            { name + ".vert", BuildVertexShaderContent(name) },
            { name + ".frag", BuildFragmentShaderContent(name) },
        };
    }
    return {}; // unreachable - silences a "not all control paths return a value" warning.
}

std::string ReminderMessageForKind(AssetScaffoldKind kind, const std::string& name)
{
    switch (kind) {
    case AssetScaffoldKind::RenderPass:
        return "remember to call Register" + name + "RenderPass(core) from your project's RegisterProject() function!";
    case AssetScaffoldKind::ComputeShader:
        return "remember to call Register" + name + "ComputePass(core) from your project's RegisterProject() function!";
    case AssetScaffoldKind::ShaderPair:
        return ""; // no companion .cpp is generated for this kind - nothing to wire in.
    }
    return "";
}

// Windows' own filesystem is case-INSENSITIVE for collision purposes -
// "Foo.comp" and "foo.comp" are the same file. Explicit ASCII lowercase
// comparison (never std::filesystem::exists() alone with the literal typed
// case) so this logic is honest about WHY it's correct and independently
// unit-testable without touching a real filesystem.
std::string ToLowerAscii(const std::string& text)
{
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}
} // namespace

IAssetScaffoldingCapability::ScaffoldOutcome EditorProjectLifecycleCapability::CreateAssetScaffold(
    AssetScaffoldKind kind, const std::string& name)
{
    ScaffoldOutcome outcome;

    const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
    if (!active.hasActiveProject) {
        outcome.errorMessage = "no active project - use New Project or Open Project first";
        return outcome;
    }

    std::string validationError;
    if (!IsValidProjectAssemblyIdentifierName(name, validationError)) {
        outcome.errorMessage = validationError;
        return outcome;
    }

    const std::vector<ScaffoldFileSpec> fileSpecs = BuildScaffoldFileSpecs(kind, name);

    // Case-insensitive collision scan, EVERY file, BEFORE writing ANY of
    // them - all-or-nothing, mirrors CreateNewProjectAssembly()'s own
    // "never leave a half-written scaffold" rule.
    std::error_code iterationError;
    std::vector<std::string> existingLowerNames;
    for (const auto& entry : std::filesystem::directory_iterator(active.assetsDirectory, iterationError)) {
        if (!entry.is_regular_file()) continue;
        existingLowerNames.push_back(ToLowerAscii(entry.path().filename().string()));
    }
    for (const ScaffoldFileSpec& spec : fileSpecs) {
        const std::string lowerName = ToLowerAscii(spec.relativeFileName);
        if (std::find(existingLowerNames.begin(), existingLowerNames.end(), lowerName) != existingLowerNames.end()) {
            outcome.errorMessage = "a file named '" + spec.relativeFileName + "' already exists";
            return outcome;
        }
    }

    for (const ScaffoldFileSpec& spec : fileSpecs) {
        if (!WriteTextFile(active.assetsDirectory / spec.relativeFileName, spec.content)) {
            outcome.errorMessage = "failed to write '" + spec.relativeFileName + "'";
            GTE_LOG_ERROR("ProjectLifecycle", "CreateAssetScaffold('" + name + "'): " + outcome.errorMessage);
            return outcome;
        }
        outcome.createdFiles.push_back(spec.relativeFileName);
    }

    outcome.success = true;
    outcome.reminderMessage = ReminderMessageForKind(kind, name);
    GTE_LOG_INFO("ProjectLifecycle", "CreateAssetScaffold('" + name + "'): created " +
        std::to_string(outcome.createdFiles.size()) + " file(s) in " + active.assetsDirectory.string());
    return outcome;
}
```

**Note — why this deliberately has no `#if !GTE_ENABLE_PROJECT_ASSEMBLIES`
guard, unlike `CreateNewProjectAssembly()`/`ClassifyAndMarkActive()`**: when
that macro is `OFF`, no project can ever become active in the first place
(`ClassifyAndMarkActive()`'s own identical guard means `SetActive()` is
never reached, and `CreateNewProjectAssembly()`'s own identical guard means
it isn't either) — so `CreateAssetScaffold()`'s own "no active project"
check above already, structurally, covers this case for free. This is a
deliberate omission, not an oversight; do not add a duplicate guard that can
never actually change this method's observable behavior.

Required NEW includes at the top of `EditorProjectLifecycleCapability.cpp`:
`<algorithm>` (for `std::transform`/`std::find`) and `<cctype>` (for
`std::tolower`) — confirm these aren't already present before adding
duplicates.

**Important, easy-to-miss detail**: `active.assetsDirectory` may not exist
yet on disk for a Tier "NotBuildable" project (per BIG-STEP 3's own
`ProjectValidityTier` — a project folder with no `Assets/` folder at all
cannot happen via THIS engine's own `CreateNewProjectAssembly()`, which
always creates it, but a hand-authored or externally-copied project folder
could theoretically be missing it). Guard
`std::filesystem::directory_iterator` construction with its own
`std::error_code` overload (shown above) — never let a missing directory
throw; `directory_iterator`'s error-code constructor simply yields an
empty range on failure, which is the correct, safe behavior here (an
empty existing-file list, so nothing collides, and the subsequent
`WriteTextFile()` calls will themselves fail loudly if the directory
genuinely doesn't exist — `std::ofstream` cannot create a missing parent
directory, so add one `std::filesystem::create_directories(active.assetsDirectory,
ec)` best-effort call, mirroring `CreateNewProjectAssembly()`'s own
`create_directories()` pattern, immediately before the write loop, so a
technically-possible-but-unlikely missing `Assets/` folder doesn't turn a
legitimate "NotBuildable" project into a hard failure here for no reason).

## STEP 4 — Unit tests

**Mechanically confirmed correction to this section**: `BuildScaffoldFileSpecs()`,
`ReminderMessageForKind()`, every `Build...Content()` template builder, and
`ToLowerAscii()`/`ReplaceAll()` all live inside an UNNAMED (anonymous)
namespace physically inside `EditorProjectLifecycleCapability.cpp` (3.3/3.4
above) — they have internal linkage and are therefore NOT visible/callable
from a separate test `.cpp` file at all. This is not a gap to fix (do not
add a `...ForTesting()` export just for this) — it exactly matches this same
file's own pre-existing convention: `BuildGameStubCppContent()`/
`WriteTextFile()`/`ToTierName()` (already in this file, above) have ZERO
direct unit tests anywhere in `tests/` today; they are only ever exercised
INDIRECTLY, through the public `CreateNewProjectAssembly()`/
`OpenProjectAssembly()` methods (confirmed by grepping `tests/` for each
name). Test everything below through the one public entry point,
`CreateAssetScaffold()`, the same way.

New file: **`tests/Editor/AssetScaffoldTemplateTests.cpp`** — NOT
`tests/Core/Plugins/`. `IAssetScaffoldingCapability`'s only real
implementation, `EditorProjectLifecycleCapability`, is `gte_editor`-tier
code (physically compiled into the `gte_editor` static library, exactly
like every `Network/*ProjectEndpointEndToEndTests.cpp` file that already
links against it) — `tests/Core/Plugins/` is reserved for genuinely
`gte_core`-tier-only code needing no `gte_editor` symbol at all (e.g.
`ProjectAssemblyBuildRunnerTierClassificationTests.cpp`'s own
`ClassifyProjectAssemblyFolder()`). Add the new file to `tests/CMakeLists.txt`'s
`if(TRUE)` block — the same always-built, `gte_editor`-tier bucket
`Editor/LoggerTests.cpp`/`Network/CreateProjectEndpointEndToEndTests.cpp`/
`Network/OpenProjectEndpointEndToEndTests.cpp` already live in, NOT the
`if(GTE_ENABLE_PROJECT_PANEL)` sub-block a few lines below it (this feature
has no dependency on `GTE_ENABLE_PROJECT_PANEL` — that switch only gates the
unrelated content-asset "Project" panel/`ProjectPanelData.h` PHASE2 extends).

**Test-isolation hazard, made explicit so it is never silently
reintroduced**: `ActiveProjectAssemblyState` is a real, process-wide Meyers
singleton shared by EVERY test in `GreatTamanaEngineTests.exe`, including
`CreateProjectEndpointEndToEndTests.cpp`/`OpenProjectEndpointEndToEndTests.cpp`,
both of which already legitimately call `SetActive()` as a side effect of
their own successful paths — gtest gives no cross-file test-order guarantee,
so this file can NEVER assume `GetActive().hasActiveProject` starts `false`.
Follow `OpenProjectEndpointEndToEndTests.cpp`'s own established idiom
(`OpenProjectOnTier0OrNonexistentNameReturns400AndDoesNotMutateActiveState`):
capture `const ActiveProjectAssemblyInfo before =
ActiveProjectAssemblyState::Instance().GetActive();` first in any test that
reads or mutates this singleton. For the one test case that genuinely needs
`hasActiveProject == false` (the "no active project" error path),
explicitly force it via `ActiveProjectAssemblyState::Instance().Clear();`
immediately before that assertion, then RESTORE `before` afterward
(`SetActive(before.name, before.sourceDirectory)` again if
`before.hasActiveProject` was true, otherwise leave it cleared) — never
leave this singleton mutated for whichever test in this same binary happens
to run next.

Cover, at minimum, ALL exercised through `CreateAssetScaffold()` itself
(never a private helper):
- No active project (forced via `Clear()`, restored afterward per above) →
  `success == false`, `errorMessage` mentions "no active project".
- Against a real scratch temp directory (mirroring
  `tests/Core/Plugins/ProjectAssemblyBuildRunnerTierClassificationTests.cpp`'s
  own `TempOutputDirectory` helper for the create/cleanup SHAPE only — that
  file's own `ClassifyProjectAssemblyFolder()` is unrelated `gte_core`-tier
  code), call `ActiveProjectAssemblyState::Instance().SetActive(name,
  scratchDirectory)` with `scratchDirectory / "Assets"` pre-created via
  `std::filesystem::create_directories()`. This redirect is SAFE here —
  unlike `CreateNewProjectAssembly()` (see
  `CreateProjectEndpointEndToEndTests.cpp`'s own top-of-file comment on why
  IT cannot use a scratch directory) — because `CreateAssetScaffold()`
  itself never calls `ResolveCMakeBuildDirectory()`/
  `ResolveProjectAssemblySourceRootDirectory()`; it only ever reads
  `ActiveProjectAssemblyState::Instance().GetActive()`, a value this test
  fully controls. Then, for EACH of the 3 kinds:
  - Confirm `outcome.success == true` and `outcome.createdFiles` has exactly
    the expected count/names (1 for RenderPass, 2 for ComputeShader, 2 for
    ShaderPair) for a sample name (e.g. `"Foo"` → `"FooRenderPass.cpp"`).
  - Read every created file back off disk and confirm its content contains
    the substituted name in the expected position(s) and never the literal
    token `"__NAME__"` (`content.find("__NAME__") == std::string::npos`).
  - Confirm `outcome.reminderMessage` is non-empty for RenderPass/
    ComputeShader and empty for ShaderPair.
- Same name, same kind, called twice → second call `success == false`,
  `errorMessage` mentions "already exists", and the ORIGINAL file's content/
  mtime is unchanged (a real, mechanical proof, not an assumed side effect).
- Same name, DIFFERENT case, called twice → second call ALSO rejected
  (case-insensitive collision).

Add the new test file to `tests/CMakeLists.txt`'s `if(TRUE)` block (see
above) — same section `Network/CreateProjectEndpointEndToEndTests.cpp`/
`Network/OpenProjectEndpointEndToEndTests.cpp` are already listed in.

## STEP 5 — Fast compile check for this phase

`EditorProjectLifecycleCapability.cpp`/`.h` and `Core/EditorCapabilities.h`
are compiled into BOTH the main `GreatTamanaEditor` target AND the test
binary. After writing this phase's code:
1. `cmake --build build --target GreatTamanaEngineTests` (or the actual
   test target name — confirm via `tests/CMakeLists.txt`) to get a fast
   compile-error signal without a full engine relink.
2. Run just the new test file's cases (ctest supports `-R
   <regex-matching-test-name>` for a fast, scoped run — do NOT run the
   full suite yet, per the master Note 4).
3. Only once that's green, move to `PHASE2_*.md`.
