// Cluster draw buffers read by the vertex stage. Set 2: sets 0 and 1 belong to the material and scene services.

struct ClusterInstance {
    mat4 world;
    uvec4 info;
};

layout(std430, set = 2, binding = 0) readonly buffer VisibleBlock {
    uvec2 items[]; // x = instance index, y = cluster index.
} visible;

layout(std430, set = 2, binding = 1) readonly buffer InstancesBlock {
    ClusterInstance items[];
} instances;
