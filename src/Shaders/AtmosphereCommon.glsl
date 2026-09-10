// AtmosphereCommon.glsl
//
// Shared GLSL math for the Atmosphere Scattering + Aerial Perspective
// campaign (see task_manager/atmosphere-scattering-1/
// ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md). Deliberately EMPTY as of Phase 1
// (ATMOSPHERE_PHASE1_REFERENCE_ANALYSIS_AND_PHYSICAL_MODEL_FOUNDATIONS_v1.md)
// - this file exists purely to give every later phase's own .comp/.frag
// source a real, already-verified-to-compile #include target from day one
// (see that phase's own Step 3.2, which proved glslc's #include resolution
// and this project's CMake incremental-rebuild dependency tracking both work
// via a throwaway probe shader, since deleted).
//
// Populated starting Phase 3 (ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md) with
// the GLSL mirror of src/Renderer/Atmosphere/AtmosphereMath.h's density
// profile/optical-depth/transmittance/phase-function functions - see that
// header's own file comment for why the CPU side is written FIRST and
// treated as the permanent ground truth ("oracle") every GLSL function here
// must faithfully reproduce, never the other way around.
//
// Every .comp/.frag file that #includes this one must also list it in its
// own gte_add_shader(... EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)
// call in the root CMakeLists.txt, so an edit here correctly triggers glslc
// to recompile every shader that depends on it (see
// cmake/CompileShaders.cmake's own EXTRA_DEPENDS doc comment).
