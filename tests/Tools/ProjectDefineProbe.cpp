// Compiled with the same flags a project assembly gets. Fails to build when engine switches are not forwarded.
#include "Profiling/ScopeTimer.h"
#include "Jobs/JobSystem.h"

#ifndef GTE_ENABLE_JOB_SYSTEM
#error "project targets must get GTE_ENABLE_JOB_SYSTEM"
#endif
#ifndef GTE_ENABLE_PROJECT_PANEL
#error "project targets must get GTE_ENABLE_PROJECT_PANEL"
#endif

int ProjectDefineProbeAnchor()
{
    GTE_PROFILE_FUNCTION();
    return 0;
}
