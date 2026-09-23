#pragma once

#include "../Core/IHostServices.h"

namespace gte {

// editor-core-separation-1 campaign, PHASE15
// (PHASE15_EDITORHOST_COMPOSITION_ROOT_CORE_CONSTRUCTION.md) - EditorHost's
// own small IHostServices adapter, needed only because Core's constructor
// requires a real IHostServices& (Core's own frozen public contract, design
// doc Section 5.2). Mirrors Application::ApplicationHostServices' exact
// shape/precedent (PHASE12, src/Application/Application.h) almost verbatim -
// this is NOT a second, competing logging path: Log() forwards straight
// into the SAME global log-sink mechanism GTE_LOG_* itself already uses
// (Core/LogSink.h). EditorHost's own constructor installs the real sink
// (LoggerLogSink::Instance(), see Editor/Logger.h) itself - by the time
// Core (or anything Core constructs) ever calls Log() through this adapter,
// a real sink is already listening, exactly the same "install once, before
// first use" timing rule SdlMemoryTracker::Install()/InstallLogSink()
// already establish elsewhere in this codebase.
//
// Unlike Application::ApplicationHostServices (a private nested struct,
// retired alongside Application in PHASE17), this is a real, permanent,
// standalone gte_editor-owned type - EditorHost is the composition root
// that survives this campaign, so its own IHostServices adapter gets a
// real header/source file pair instead of a temporary nested struct.
class EditorHostServices : public IHostServices {
public:
    void Log(LogLevel level, std::string_view message) override;
};

} // namespace gte
