#include <cmath>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "engine/render/d3d12/D3D12RenderBackendInternal.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

bool nearf(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_d3d12_world_material_constants_contract(std::string& outFail) {
    namespace d3d12i = engine::render::d3d12_internal;

    if (!expect(d3d12i::alignUp(0u, 256u) == 0u &&
                    d3d12i::alignUp(1u, 256u) == 256u &&
                    d3d12i::alignUp(512u, 256u) == 512u,
                "alignUp should preserve aligned values and round up unaligned values.",
                outFail)) {
        return false;
    }

    if (!expect(d3d12i::frameSliceBase(0u, 4096u) == 0u &&
                    d3d12i::frameSliceBase(1u, 4096u) == 4096u &&
                    d3d12i::frameSliceEnd(0u, 4096u) == 4096u &&
                    d3d12i::frameSliceEnd(1u, 4096u) == 8192u,
                "frame slice helpers should compute non-overlapping per-frame upload ranges.",
                outFail)) {
        return false;
    }

    if (!expect(nearf(d3d12i::sanitizeWrapMode(33071), 33071.0f) &&
                    nearf(d3d12i::sanitizeWrapMode(33648), 33648.0f) &&
                    nearf(d3d12i::sanitizeWrapMode(10497), 10497.0f) &&
                    nearf(d3d12i::sanitizeWrapMode(-123), 10497.0f),
                "sanitizeWrapMode should accept clamp/mirror/repeat and fall back invalid values to repeat.",
                outFail)) {
        return false;
    }

    {
        const auto c = d3d12i::makeWorldPsConstants(nullptr, 0.75f);
        if (!expect(nearf(c.useTexture, 0.75f) &&
                        nearf(c.wrapS, 10497.0f) &&
                        nearf(c.wrapT, 10497.0f) &&
                        nearf(c.alphaCutoff, 0.5f),
                    "makeWorldPsConstants(nullptr, ...) should preserve defaults and requested useTexture.",
                    outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.wrapS = 33071;
        tex.wrapT = 99999; // invalid -> repeat
        tex.alphaMode = 9u; // clamp to BLEND (2)
        tex.alphaCutoff = 1.5f; // clamp to 1
        tex.materialMode = 1u;
        tex.materialTimeSec = 12.0f;
        tex.materialFlags = 3.0f;
        tex.materialAtlasWidth = -5.0f;  // clamp to 0
        tex.materialAtlasHeight = 64.0f;
        tex.materialRect0U = 0.1f;
        tex.materialRect0V = 0.2f;
        tex.materialRect0W = 0.3f;
        tex.materialRect0H = 0.4f;
        tex.materialFlipbook0Cols = 4.0f;
        tex.materialFlipbook0Rows = 5.0f;
        tex.materialFlipbook0Frames = 6.0f;
        tex.materialFlipbook0Fps = 7.0f;
        tex.materialFlipbook1Frames = 8.0f;
        tex.materialFlipbook1Fps = 9.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(nearf(c.useTexture, 1.0f) &&
                        nearf(c.wrapS, 33071.0f) &&
                        nearf(c.wrapT, 10497.0f) &&
                        nearf(c.alphaMode, 2.0f) &&
                        nearf(c.alphaCutoff, 1.0f) &&
                        nearf(c.materialMode, 1.0f) &&
                        nearf(c.materialTimeSec, 12.0f) &&
                        nearf(c.materialFlags, 3.0f) &&
                        nearf(c.materialAtlasWidth, 0.0f) &&
                        nearf(c.materialAtlasHeight, 64.0f) &&
                        nearf(c.materialRect0U, 0.1f) &&
                        nearf(c.materialRect0V, 0.2f) &&
                        nearf(c.materialRect0W, 0.3f) &&
                        nearf(c.materialRect0H, 0.4f) &&
                        nearf(c.materialFlipbook0Cols, 4.0f) &&
                        nearf(c.materialFlipbook0Rows, 5.0f) &&
                        nearf(c.materialFlipbook0Frames, 6.0f) &&
                        nearf(c.materialFlipbook0Fps, 7.0f) &&
                        nearf(c.materialFlipbook1Frames, 8.0f) &&
                        nearf(c.materialFlipbook1Fps, 9.0f),
                    "makeWorldPsConstants should sanitize/clamp wrap+alpha values while forwarding material payload fields.",
                    outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 7u;
        tex.normalScale = 0.234547868f;
        tex.metallicFactor = 0.3613101f;
        tex.roughnessFactor = 0.391571164f;
        tex.materialTimeSec = 0.0f;
        tex.materialFlags = 1.0f;
        tex.materialAtlasWidth = 1.0f;
        tex.materialAtlasHeight = 0.455134153f;
        tex.materialRect0U = 1.00002408f;
        tex.materialRect0V = 0.833229f;
        tex.cameraPosX = 17.0f;
        tex.cameraPosY = 18.0f;
        tex.cameraPosZ = 19.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 7.0f) &&
                    nearf(c.vertexColorMulR, 0.234547868f) &&
                    nearf(c.vertexColorMulG, 0.3613101f) &&
                    nearf(c.vertexColorMulB, 0.391571164f) &&
                    nearf(c.materialFlags, 1.0f) &&
                    nearf(c.materialAtlasWidth, 1.0f) &&
                    nearf(c.materialAtlasHeight, 0.455134153f) &&
                    nearf(c.materialRect0U, 1.00002408f) &&
                    nearf(c.materialRect0V, 0.833229f) &&
                    nearf(c.materialRect1V, 17.0f) &&
                    nearf(c.materialRect1W, 18.0f) &&
                    nearf(c.materialRect1H, 19.0f),
                "Tree-miki mode should preserve its source Shadow_Color and rim payload alongside the D3D12 camera packing.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 8u;
        tex.normalScale = 0.01f;
        tex.metallicFactor = 0.02f;
        tex.roughnessFactor = 0.03f;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.31f;
        tex.materialRect0U = 0.32f;
        tex.materialRect0V = 0.33f;
        tex.materialRect0W = 0.41f;
        tex.materialRect0H = 0.42f;
        tex.materialRect1U = 0.43f;
        tex.materialRect1V = 0.51f;
        tex.materialRect1W = 0.52f;
        tex.materialRect1H = 0.53f;
        tex.cameraPosX = 17.0f;
        tex.cameraPosY = 18.0f;
        tex.cameraPosZ = 19.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 8.0f) &&
                    nearf(c.vertexColorMulR, 0.01f) &&
                    nearf(c.vertexColorMulG, 0.02f) &&
                    nearf(c.vertexColorMulB, 0.03f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.31f) &&
                    nearf(c.materialRect0U, 0.32f) &&
                    nearf(c.materialRect0V, 0.33f) &&
                    nearf(c.materialRect0W, 0.11f) &&
                    nearf(c.materialRect0H, 0.12f) &&
                    nearf(c.materialRect1U, 0.13f) &&
                    nearf(c.materialRect1V, 0.41f) &&
                    nearf(c.materialRect1W, 0.42f) &&
                    nearf(c.materialRect1H, 0.43f) &&
                    nearf(c.materialFlipbook0Cols, 0.51f) &&
                    nearf(c.materialFlipbook0Rows, 0.52f) &&
                    nearf(c.materialFlipbook0Frames, 0.53f) &&
                    nearf(c.materialFlipbook1Cols, 17.0f) &&
                    nearf(c.materialFlipbook1Rows, 18.0f) &&
                    nearf(c.materialFlipbook1Frames, 19.0f),
                "FieldTreeShader02 mode should preserve all five source colors, rim constants, and the D3D12 camera payload.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 9u;
        tex.normalScale = 0.11f;
        tex.metallicFactor = 0.12f;
        tex.roughnessFactor = 0.13f;
        tex.emissiveFactorR = 0.21f;
        tex.emissiveFactorG = 0.22f;
        tex.emissiveFactorB = 0.23f;
        tex.materialTimeSec = 0.31f;
        tex.materialFlags = 0.32f;
        tex.materialAtlasWidth = 0.33f;
        tex.materialAtlasHeight = 0.34f;
        tex.materialRect0U = 0.35f;
        tex.materialFlipbook0Fps = -2.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 9.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialRect0W, 0.21f) &&
                    nearf(c.materialRect0H, 0.22f) &&
                    nearf(c.materialRect1U, 0.23f) &&
                    nearf(c.materialTimeSec, 0.31f) &&
                    nearf(c.materialFlags, 0.32f) &&
                    nearf(c.materialAtlasWidth, 0.33f) &&
                    nearf(c.materialAtlasHeight, 0.34f) &&
                    nearf(c.materialRect0U, 0.35f) &&
                    nearf(c.materialFlipbook0Fps, -2.0f),
                "FieldGrassShader02 mode should preserve Color, Shadow_Color, OnGame, and source mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 10u;
        tex.normalScale = 0.11f;
        tex.metallicFactor = 0.12f;
        tex.roughnessFactor = 0.13f;
        tex.emissiveFactorR = 0.21f;
        tex.emissiveFactorG = 0.22f;
        tex.emissiveFactorB = 0.23f;
        tex.materialTimeSec = 0.41f;
        tex.materialFlags = 0.42f;
        tex.materialAtlasWidth = 0.43f;
        tex.materialAtlasHeight = 0.51f;
        tex.materialRect0U = 0.52f;
        tex.materialRect0V = 0.53f;
        tex.materialFlipbook0Fps = 0.0f;
        tex.cameraPosX = 17.0f;
        tex.cameraPosY = 18.0f;
        tex.cameraPosZ = 19.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 10.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialRect0W, 0.21f) &&
                    nearf(c.materialRect0H, 0.22f) &&
                    nearf(c.materialRect1U, 0.23f) &&
                    nearf(c.materialTimeSec, 0.41f) &&
                    nearf(c.materialFlags, 0.42f) &&
                    nearf(c.materialAtlasWidth, 0.43f) &&
                    nearf(c.materialAtlasHeight, 0.51f) &&
                    nearf(c.materialRect0U, 0.52f) &&
                    nearf(c.materialRect0V, 0.53f) &&
                    nearf(c.materialRect1V, 17.0f) &&
                    nearf(c.materialRect1W, 18.0f) &&
                    nearf(c.materialRect1H, 19.0f),
                "FieldGrassShader01 mode should preserve Color, Shadow_Color, rim, mip-bias, and camera payloads.",
                outFail)) {
            return false;
        }
    }

    return true;
}

