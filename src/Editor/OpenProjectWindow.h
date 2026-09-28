#pragma once

// src/Editor/OpenProjectWindow.h
//
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE4. The "Open Project" floating utility window -
// mirrors NewProjectWindow.h's own exact shape (non-dockable, one
// EditorContext bool drives open/close, calls the capability directly and
// writes into the shared ctx.projectWorkflowStatus* fields on completion),
// but shows a scrollable, tier-badged list instead of a text box.
//
// CRITICAL: this window calls
// IProjectLifecycleCapability::OpenProjectAssemblyOnMainThread() -
// NEVER OpenProjectAssembly() - see PHASE0_MASTER_STRATEGY.md, Section
// 2.2, and EditorCapabilities.h's own doc comments on each method, for why
// calling the other one from here would deadlock the whole Editor.

// NewProjectWindow.h's own real, current include list #includes
// "EditorContext.h" DIRECTLY (never just a forward declaration) - Build()'s
// own body needs the COMPLETE EditorContext type to read/write its
// `ctx.openProjectWindowOpen`/`ctx.projectWorkflowStatus*` fields, and
// neither NewProjectWindow.cpp nor this window's own .cpp (3.3 below)
// separately #includes EditorContext.h itself - mirror that exact
// precedent here too, or Build()'s body fails to compile with an
// incomplete-type error the moment it touches any `ctx.` field.
#include "EditorContext.h"

#include <string>
#include <vector>

namespace gte {

class IProjectLifecycleCapability;
// Deliberately NO forward-declaration of ProjectValidityTier here - this
// window only ever stores/compares the plain `tierName` STRING each
// ProjectListEntry already carries (see Row below), never the enum
// itself, so pulling in even a forward declaration of it would be a dead,
// unused dependency.

class OpenProjectWindow {
public:
    // Triggers an immediate rescan (calls ListProjectAssemblies()
    // directly, in-process - the exact same data GET
    // /project_assembly/list_projects would return, just read without an
    // HTTP round trip since we are already inside the process).
    void Open(IProjectLifecycleCapability* capability);
    void Build(EditorContext& ctx, IProjectLifecycleCapability* capability);

private:
    struct Row {
        std::string name;
        std::string tierName;
    };
    bool m_wasOpenLastFrame = false;
    std::vector<Row> m_rows;
    int m_selectedIndex = -1;
    std::string m_errorMessage;
};

} // namespace gte
