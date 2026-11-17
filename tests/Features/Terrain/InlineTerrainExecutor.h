#pragma once

#include "Features/Terrain/TerrainStreamer.h"

namespace gte::terrain {

// Runs every job immediately on the calling thread.
class InlineTerrainExecutor final : public ITerrainExecutor {
public:
    void Run(Jobs::JobFunction fn, void* payload, Jobs::JobHandle&) override { fn(payload); }
};

} // namespace gte::terrain
