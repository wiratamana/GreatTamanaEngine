// src/Core/ScreenPostProcessPassPriorityAssignment.cpp
//
// better-render-pass-1 campaign, PHASE9. See
// ScreenPostProcessPassPriorityAssignment.h for the full contract.
#include "ScreenPostProcessPassPriorityAssignment.h"

namespace gte {

std::int32_t NextAutoScreenPostProcessPassPriority(std::int32_t& counter)
{
    return counter++;
}

} // namespace gte
