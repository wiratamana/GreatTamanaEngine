// The ONE place in the entire engine that compiles stb_image's actual
// implementation (STB_IMAGE_IMPLEMENTATION) - deliberately living here, in
// this always-compiled module (src/Assets/, no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency), rather than under src/Editor/ where
// it used to live (see AssetPreviewTexture.cpp's own history): the PNG/JPEG
// -> KTX2 import pipeline (see Ktx2Encoder.h) needs real image decoding in
// EVERY build configuration, including a release build with the Editor
// switched off entirely - not just when there's an Inspector panel around
// to preview a texture.
//
// Every OTHER translation unit that needs stb_image's decode functions
// (e.g. src/Editor/AssetPreviewTexture.cpp) must `#include <stb_image.h>`
// WITHOUT defining STB_IMAGE_IMPLEMENTATION itself - defining it more than
// once would be an ODR violation (multiple definitions of every stbi_*
// function across translation units). gte_core links stb_image PUBLIC (see
// the root CMakeLists.txt), so both GreatTamanaEngine.exe and
// GreatTamanaEngineTests always get exactly one copy of these symbols from
// this file, regardless of GTE_ENABLE_EDITOR.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
