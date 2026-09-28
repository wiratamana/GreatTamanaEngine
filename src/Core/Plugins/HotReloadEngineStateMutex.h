#pragma once

// src/Core/Plugins/HotReloadEngineStateMutex.h
//
// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1), PHASE1. A single, process-wide mutex guarding read/write
// access to every piece of engine state a future BIG-STEP 3 hot-reload
// cycle will mutate OUTSIDE the per-frame ECS Registry itself -
// specifically ComponentTypeRegistry, EditorPanelRegistry, and
// ProjectAssemblyHost's own loaded-assembly list/registration ledger (once
// a future BIG-STEP 2 campaign builds that ledger).
//
// WHY A PLAIN MUTEX IS CORRECT HERE (see PHASE0_MASTER_STRATEGY.md, Section
// 3.1, Correction 2, for the full reasoning): today, and for this whole
// campaign's lifetime, ComponentTypeRegistry/EditorPanelRegistry/
// ProjectAssemblyHost are mutated ONLY once, at EditorHost construction time
// (LoadPlugins()/LoadProjectAssemblies()), strictly BEFORE
// NetworkServer::Start() is ever called - so there is, in fact, no real race
// for THIS campaign to guard against yet. This mutex is added now, proactively,
// so EditorHotReloadDebugCapability::GetLedgerEntry()/
// GetLoadedAssemblyFileNames()/GetRegisteredComponentTypeNames() (PHASE2)
// already take it before every read, and so a future BIG-STEP 2/3 campaign's
// own mutating code has an unambiguous, pre-existing, documented lock to
// take too, rather than discovering the need for one only after a live
// race is reported. Deliberately NOT applied to the ECS Registry itself -
// see HOTRELOAD's own GetSceneSnapshotJson/BuildSceneSnapshotJson reasoning
// (Core/EditorCapabilities.h's own IHotReloadDebugCapability doc comment)
// for why that needs the EngineCommandBridge instead, not this mutex.
//
// A FUTURE BIG-STEP 2/3 CAMPAIGN MUST lock this same mutex around every
// ComponentTypeRegistry::RegisterDescriptor()/UnregisterDescriptor(),
// EditorPanelRegistry::RegisterPluginPanel()/UnregisterPluginPanel(), and
// ProjectAssemblyHost load/unload call it makes during a reload cycle - this
// file's own comment is the permanent, citeable reminder that requirement
// exists.

#include <mutex>

namespace gte {

std::mutex& GetHotReloadEngineStateMutex();

} // namespace gte
