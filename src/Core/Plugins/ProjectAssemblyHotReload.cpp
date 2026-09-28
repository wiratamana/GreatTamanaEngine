#include "ProjectAssemblyHotReload.h"
#include "../Logging.h"
#include "ProjectAssemblyHotReloadDebugStatus.h"

namespace gte {

void PerformProjectAssemblyHotReload(const std::string& projectName, Core& /*core*/, Renderer& /*renderer*/,
    EditorHost* /*editorHost*/, const std::filesystem::path& /*outputDirectory*/,
    const std::filesystem::path& /*buildDirectory*/)
{
    // editor-core-separation-14 campaign, PHASE3 - TEMPORARY body, replaced
    // in full by PHASE4. Proves the whole cross-thread wiring chain
    // (bridge -> EditorHost::Run() drain point -> this function -> status
    // singleton -> HTTP response) end-to-end THIS phase, without yet
    // touching backup/unload/compile/reload. outputDirectory/buildDirectory
    // are unused this phase (PHASE4 is the first body to actually need
    // them) - already threaded through the signature now so PHASE4 never
    // has to touch any CALL SITE, only this function's own body.
    GTE_LOG_WARNING("ProjectAssemblyHotReload",
        "PerformProjectAssemblyHotReload('" + projectName + "') called - PHASE4 has not replaced this temporary body yet; no real reload occurred.");
    ProjectAssemblyHotReloadDebugStatus::Instance().Set("CapturingState", projectName);
    ProjectAssemblyHotReloadDebugStatus::Instance().Finish("CriticalFailure", "PHASE3 temporary stub - PHASE4 not yet implemented");
}

} // namespace gte
