#ifndef SCENE_DEPTH_CONSTANTS_GLSL
#define SCENE_DEPTH_CONSTANTS_GLSL

// Depth convention: standard-Z, zero-to-one. Depth at or beyond this means nothing was drawn at the pixel.
const float kClearDepthThreshold = 0.999999;

#endif
