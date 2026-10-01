#pragma once

// better-render-pass-2 campaign, PHASE1 (PHASE1_RELOCATE_SHARED_DEPENDENCIES.md) -
// `IEditorPanelModule_v1`/`kIEditorPanelModule_v1_Name` were RELOCATED to
// `src/Core/EditorPanelModule.h` (a `gte_core`-owned header) since Project
// Assembly's own custom Editor panel capability genuinely needs this
// interface and must keep working after `plugins/gte_plugin_abi` is fully
// removed (PHASE4). This file is now a temporary re-export shim, kept only
// so nothing still pointed at this old path breaks mid-campaign - deleted
// for real in PHASE4.
#include "../../src/Core/EditorPanelModule.h"
