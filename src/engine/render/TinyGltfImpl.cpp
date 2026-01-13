// src/engine/render/TinyGltfImpl.cpp
// The one-and-only translation unit that provides tinygltf implementation.

#include <nlohmann/json.hpp>

// If you have vcpkg "stb" and want tinygltf to use it via includes,
// do NOT define TINYGLTF_NO_STB_IMAGE / TINYGLTF_NO_STB_IMAGE_WRITE.

// We DO want to prevent tinygltf from including json itself (we include it above).
#ifndef TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_INCLUDE_JSON
#endif

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>
