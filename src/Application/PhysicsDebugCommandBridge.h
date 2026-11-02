#pragma once
#include "SyncCommandBridge.h"
#include "../Physics/PhysicsDebugReport.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gte {

enum class PhysicsDebugCommandKind { List, State, Set, Preset, Play, Seek, Reset, Run, Capture, Metrics, Joints, Colliders, Image };

struct PhysicsDebugCommandRequest {
    PhysicsDebugCommandKind kind = PhysicsDebugCommandKind::List;
    std::string entityName; // every kind except List, and Set/Preset/Capture without an entity.

    std::vector<std::pair<std::string, std::string>> pairs; // Set.
    std::string presetName;                                  // Preset.

    // Play and Seek.
    std::string animationGtaPath;
    float frame = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool hasFrame = false;
    bool hasSpeed = false;
    bool hasLoop = false;
    bool resetPhysics = true; // Seek.

    PhysicsRunRequest run; // Run.

    // Capture, and Metrics (windowFrames).
    bool captureEnabled = false;
    std::uint32_t windowFrames = 120;

    // Joints, and Metrics (groupFilter).
    std::string groupFilter;
    std::int32_t chainFilter = -1;
    std::uint32_t first = 0;
    std::uint32_t count = 200;

    PhysicsDebugImageRequest image; // Image.
};

struct PhysicsDebugCommandResult {
    PhysicsDebugCommandKind kind = PhysicsDebugCommandKind::List;
    bool success = false;
    int httpStatus = 200;
    std::string errorMessage;
    std::string errorKey;

    PhysicsDebugListReport list;
    PhysicsDebugStateReport state;
    PhysicsRunReport run;           // Run and Metrics (run.metrics).
    PhysicsJointsReport joints;
    PhysicsCollidersReport colliders;
    PhysicsDebugSnapshot snapshot;
    PhysicsAnimatorReport animator; // Play and Seek.
    PhysicsCaptureStatus capture;   // Capture.
    std::vector<std::pair<std::string, std::string>> settings; // Set and Preset.
};

class PhysicsDebugCommandBridge : public SyncCommandBridge<PhysicsDebugCommandRequest, PhysicsDebugCommandResult> {};

} // namespace gte
