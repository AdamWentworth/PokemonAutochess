#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "engine/render/DebugGeometry.h"
#include "engine/render/IRenderBackend.h"

namespace engine::render::d3d12_internal {

using DebugVertex = engine::render::debug::Vertex2D;

struct SpriteVertex {
    float x;
    float y;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

struct WorldVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float r;
    float g;
    float b;
    float a;
};

static_assert(
    sizeof(WorldVertex) == sizeof(IRenderBackend::WorldMeshVertex),
    "WorldVertex and WorldMeshVertex layout must match for fast memcpy upload path.");
static_assert(std::is_trivially_copyable_v<WorldVertex>, "WorldVertex must be trivially copyable.");
static_assert(
    std::is_trivially_copyable_v<IRenderBackend::WorldMeshVertex>,
    "WorldMeshVertex must be trivially copyable.");

inline constexpr std::size_t kMaxSpriteQuads = 2048;
inline constexpr std::size_t kMaxSpriteVertices = kMaxSpriteQuads * 6;
inline constexpr std::size_t kMaxDebugQuads = 4096;
inline constexpr std::size_t kMaxDebugLines = 8192;
inline constexpr std::size_t kMaxDebugTriangles = 65536;
inline constexpr std::size_t kMaxDebugVertices = kMaxDebugTriangles * 3;
inline constexpr std::size_t kMaxWorldTriangles = 180000;
inline constexpr std::size_t kMaxWorldVertices = kMaxWorldTriangles * 3;
inline constexpr std::size_t kMaxWorldIndices = kMaxWorldTriangles * 3;
inline constexpr std::size_t kMaxSrvDescriptors = 2048;
inline constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";
inline constexpr int kGlRepeat = 10497;
inline constexpr int kGlMirroredRepeat = 33648;
inline constexpr int kGlClampToEdge = 33071;

inline std::size_t alignUp(std::size_t value, std::size_t alignment) {
    if (alignment == 0u) return value;
    const std::size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

inline float sanitizeWrapMode(int wrapMode) {
    if (wrapMode == kGlClampToEdge || wrapMode == kGlMirroredRepeat || wrapMode == kGlRepeat) {
        return static_cast<float>(wrapMode);
    }
    return static_cast<float>(kGlRepeat);
}

struct WorldPsConstants {
    float useTexture = 0.0f;
    float wrapS = static_cast<float>(kGlRepeat);
    float wrapT = static_cast<float>(kGlRepeat);
    float alphaMode = 0.0f;
    float alphaCutoff = 0.5f;
    float materialMode = 0.0f;
    float materialTimeSec = 0.0f;
    float materialFlags = 0.0f;
    float materialAtlasWidth = 0.0f;
    float materialAtlasHeight = 0.0f;
    float materialRect0U = 0.0f;
    float materialRect0V = 0.0f;
    float materialRect0W = 1.0f;
    float materialRect0H = 1.0f;
    float materialRect1U = 0.0f;
    float materialRect1V = 0.0f;
    float materialRect1W = 1.0f;
    float materialRect1H = 1.0f;
    float materialFlipbook0Cols = 1.0f;
    float materialFlipbook0Rows = 1.0f;
    float materialFlipbook0Frames = 1.0f;
    float materialFlipbook0Fps = 0.0f;
    float materialFlipbook1Cols = 1.0f;
    float materialFlipbook1Rows = 1.0f;
    float materialFlipbook1Frames = 1.0f;
    float materialFlipbook1Fps = 0.0f;
};

inline WorldPsConstants makeWorldPsConstants(const IRenderBackend::WorldTextureData* textureData, float useTexture) {
    WorldPsConstants constants;
    constants.useTexture = useTexture;
    if (!textureData) return constants;
    constants.wrapS = sanitizeWrapMode(textureData->wrapS);
    constants.wrapT = sanitizeWrapMode(textureData->wrapT);
    constants.alphaMode = static_cast<float>(std::min<std::uint8_t>(2u, textureData->alphaMode));
    constants.alphaCutoff = std::clamp(textureData->alphaCutoff, 0.0f, 1.0f);
    constants.materialMode = static_cast<float>(textureData->materialMode);
    constants.materialTimeSec = textureData->materialTimeSec;
    constants.materialFlags = textureData->materialFlags;
    constants.materialAtlasWidth = (std::max)(0.0f, textureData->materialAtlasWidth);
    constants.materialAtlasHeight = (std::max)(0.0f, textureData->materialAtlasHeight);
    constants.materialRect0U = textureData->materialRect0U;
    constants.materialRect0V = textureData->materialRect0V;
    constants.materialRect0W = textureData->materialRect0W;
    constants.materialRect0H = textureData->materialRect0H;
    constants.materialRect1U = textureData->materialRect1U;
    constants.materialRect1V = textureData->materialRect1V;
    constants.materialRect1W = textureData->materialRect1W;
    constants.materialRect1H = textureData->materialRect1H;
    constants.materialFlipbook0Cols = textureData->materialFlipbook0Cols;
    constants.materialFlipbook0Rows = textureData->materialFlipbook0Rows;
    constants.materialFlipbook0Frames = textureData->materialFlipbook0Frames;
    constants.materialFlipbook0Fps = textureData->materialFlipbook0Fps;
    constants.materialFlipbook1Cols = textureData->materialFlipbook1Cols;
    constants.materialFlipbook1Rows = textureData->materialFlipbook1Rows;
    constants.materialFlipbook1Frames = textureData->materialFlipbook1Frames;
    constants.materialFlipbook1Fps = textureData->materialFlipbook1Fps;
    return constants;
}

} // namespace engine::render::d3d12_internal

