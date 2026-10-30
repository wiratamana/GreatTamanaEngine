# Documentation index
> Every doc of GreatTamanaEngine in one place. Pick by folder, or use the reading paths below.
> Verified against code: 2026-10-30

## Reading paths
| You are... | Read in this order |
|---|---|
| New to the repo | [README](../README.md), [Building](../BUILDING.md), [Getting started](getting-started.md), [Overview](architecture/overview.md) |
| Debugging a running editor | [HTTP cheat sheet](debugging/http-cheat-sheet.md), [Troubleshooting](troubleshooting.md), [Glossary](glossary.md) |
| Writing engine code | [Contributing](conventions/contributing.md), [Overview](architecture/overview.md), [Rendering](architecture/rendering.md) |
| Making a game project | [Project assembly](guides/project-assembly.md), [Getting started](getting-started.md) |

## Start here
| Doc | What it gives you | Who |
|---|---|---|
| [README](../README.md) | What the engine is, repo map | Everyone |
| [Building](../BUILDING.md) | Toolchain, build commands, CMake options | Everyone |
| [Testing](../TESTING.md) | Run and add tests, guard checks | Developers |
| [Getting started](getting-started.md) | 30-minute path from clone to first HTTP call | New users |
| [Glossary](glossary.md) | One line per term | Everyone |
| [Troubleshooting](troubleshooting.md) | Symptom, cause, fix | Everyone |
| [TODO](../TODO.md) | Open work, by area | Maintainers |
| [Changelog](CHANGELOG.md) | Long history, newest first | Maintainers |
| [AGENTS.md](../AGENTS.md) | Code rules. Owned by a human, never edited by tools | Humans and AI agents |

## Debugging (tools to find bugs)
| Doc | What it gives you | Who |
|---|---|---|
| [HTTP cheat sheet](debugging/http-cheat-sheet.md) | One-page guide and recipes for the HTTP debug server | Everyone |
| [HTTP routes](debugging/http-routes.md) | Capture, logs, scene, camera, editor, asset routes | Tool and script authors |
| [HTTP routes (debug)](debugging/http-routes-debug.md) | Profiler, faults, feature debug, frame debugger, render graph, project | Tool and script authors |
| [Profiling](debugging/profiling.md) | CPU and GPU profiler, how to read it | Performance work |
| [Frame debugger](debugging/frame-debugger.md) | Freeze one frame, inspect draw events | Render debugging |
| [Logging](debugging/logging.md) | Write and read log lines | Developers |

## Architecture (how it works)
| Doc | What it gives you | Who |
|---|---|---|
| [Overview](architecture/overview.md) | Layers, startup, frame loop, threads | Read first |
| [ECS](architecture/ecs.md) | Entities, components, hierarchy, reflection | Gameplay and tools code |
| [Math](architecture/math.md) | Axes, matrix layout, angles, depth range | Transform and camera code |
| [HTTP server](architecture/http-server.md) | Threads, bridges, timeouts, add a route | Engine developers |
| [Editor](architecture/editor.md) | Host, panels, bridges, UI rules | Editor developers |
| [Rendering](architecture/rendering.md) | Layers, frame flow, formats, materials | Render developers |
| [Render graph](architecture/render-graph.md) | The `rg` API for passes and recording | Render developers |
| [GPU-driven rendering](architecture/gpu-driven-rendering.md) | Cluster culling, Hi-Z, indirect draw | Render developers |
| [Mesh clusters](architecture/mesh-clusters.md) | Cluster table format and build | Asset and render developers |
| [Asset pipeline](architecture/asset-pipeline.md) | Source file to `.gta` to runtime object | Asset developers |
| [Animation and physics](architecture/animation-physics.md) | Pose, IK, Verlet chains, skinning | Animation developers |
| [Scene serialization](architecture/scene-serialization.md) | `.gtscene` save and load | Component authors |

## Conventions (rules to follow)
| Doc | What it gives you | Who |
|---|---|---|
| [Contributing](conventions/contributing.md) | Code rules in short form, CI guards | Everyone who codes |
| [Job system](conventions/job-system.md) | Worker pool rules, thread-safety table | Developers |
| [Time and playback pause](conventions/time-and-playback-pause.md) | Frame clock, pause, step | Gameplay code |
| [Memory tracking](conventions/memory-tracking.md) | GPU, SDL, ImGui memory trackers | Resource authors |
| [ImGui ID uniqueness](conventions/imgui-id-uniqueness.md) | Unique widget IDs in loops | Editor developers |
| [Render honesty](conventions/render-honesty.md) | Pass toggles and the Frame Debugger tree must agree | Pass authors |
| [PBR materials](conventions/pbr-materials.md) | glTF image roles, tangents, GGX | Material and import code |
| [Volumetric resources](conventions/volumetric-resources.md) | 3D textures in features | Feature authors |

## Guides (do one task)
| Doc | What it gives you | Who |
|---|---|---|
| [Project assembly](guides/project-assembly.md) | Make a game project, load and hot reload it | Game developers |
| [Writing a render feature](guides/writing-a-render-feature.md) | Add a built-in feature step by step | Engine developers |

## Reference (look-up tables)
| Doc | What it gives you | Who |
|---|---|---|
| [Feature kit](reference/feature-kit.md) | `gte::fx` verbs, resources, channels | Feature authors |
| [Render features](reference/render-features/README.md) | Master table of the 8 features | Everyone |
| [Atmosphere](reference/render-features/atmosphere.md) | Sky, aerial perspective, sun tint | Tuning, debugging |
| [Shadow](reference/render-features/shadow.md) | Sun cascade shadows | Tuning, debugging |
| [Scene lighting](reference/render-features/scene-lighting.md) | Sun and ambient data for materials | Tuning, debugging |
| [Tone mapping](reference/render-features/tone-mapping.md) | Exposure, curve, display encode | Tuning, debugging |
| [GTAO](reference/render-features/gtao.md) | Ambient occlusion | Tuning, debugging |
| [SSGI](reference/render-features/ssgi.md) | One-bounce screen-space GI | Tuning, debugging |
| [Volumetric light](reference/render-features/volumetric-light.md) | Light shafts and fog | Tuning, debugging |
| [GPU-driven scene](reference/render-features/gpu-driven-scene.md) | Culling feature, "Culling" panel | Tuning, debugging |

## Tools
| Tool | What it does |
|---|---|
| [culling_bench](../tools/culling_bench/README.md), [profiler_ab](../tools/profiler_ab/README.md) | Benchmark scripts that call the HTTP server |
| [volk_leak_check](../tools/ci/volk_leak_check/README.md), [native_leak_check](../tools/ci/native_leak_check/README.md), [feature_purity_check](../tools/ci/feature_purity_check/README.md), [header_purity_probe](../tools/ci/header_purity_probe/README.md) | Vulkan boundary guards |
| [blocking_call_check](../tools/ci/blocking_call_check/README.md), [engine_vocabulary_check](../tools/ci/engine_vocabulary_check/README.md), [profiler_vocabulary_check](../tools/ci/profiler_vocabulary_check/README.md), [removed_symbol_check](../tools/ci/removed_symbol_check/README.md), [atmosphere_core_decoupling_check](../tools/ci/atmosphere_core_decoupling_check/README.md) | Source scan guards |
| [gte_core_standalone_probe](../tools/ci/gte_core_standalone_probe/README.md), [gte_core_player_link_probe](../tools/ci/gte_core_player_link_probe/README.md), [gte_project_assembly_link_probe](../tools/ci/gte_project_assembly_link_probe/README.md) | Link probes for the library split |
