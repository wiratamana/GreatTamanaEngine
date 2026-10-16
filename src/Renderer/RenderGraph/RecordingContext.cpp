#include "RecordingContext.h"

#include "RecordingAccess.h"
#include "RenderGraph.h"

namespace gte::rg {

CommandBuffer RecordingContext::Commands() const noexcept
{
    if (m_pass != nullptr) {
        return m_pass->Cmd();
    }
    return RecordingAccess::Adopt(RecordingAccess::Native(*this), m_renderer);
}

} // namespace gte::rg
