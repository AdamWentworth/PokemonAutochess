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
        tex.lightProjectionUvRowU = {0.11f, 0.12f, 0.13f, 0.14f};
        tex.lightProjectionUvRowV = {0.21f, 0.22f, 0.23f, 0.24f};
        tex.projectedShadowMatrix = {
            1.0f, 2.0f, 3.0f, 0.0f,
            4.0f, 5.0f, 6.0f, 0.0f,
            7.0f, 8.0f, 9.0f, 0.0f,
            10.0f, 11.0f, 12.0f, 1.0f};

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
                        nearf(c.materialFlipbook1Fps, 9.0f) &&
                        nearf(c.projectedShadowRowX[0], 1.0f) &&
                        nearf(c.projectedShadowRowX[1], 4.0f) &&
                        nearf(c.projectedShadowRowX[2], 7.0f) &&
                        nearf(c.projectedShadowRowX[3], 10.0f) &&
                        nearf(c.projectedShadowRowY[0], 2.0f) &&
                        nearf(c.projectedShadowRowY[3], 11.0f) &&
                        nearf(c.projectedShadowRowZ[0], 3.0f) &&
                        nearf(c.projectedShadowRowZ[3], 12.0f) &&
                        nearf(c.lightProjectionUvRowU[0], 0.11f) &&
                        nearf(c.lightProjectionUvRowU[3], 0.14f) &&
                        nearf(c.lightProjectionUvRowV[0], 0.21f) &&
                        nearf(c.lightProjectionUvRowV[3], 0.24f),
                    "makeWorldPsConstants should sanitize/clamp wrap+alpha values while forwarding material payload fields.",
                    outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 20u;
        tex.emissiveFactorR = 0.31f;
        tex.emissiveFactorG = 0.32f;
        tex.emissiveFactorB = 0.33f;
        tex.materialTimeSec = 0.41f;
        tex.materialFlags = 0.42f;
        tex.materialAtlasWidth = 0.43f;
        tex.materialAtlasHeight = 0.44f;
        tex.materialRect0U = 0.45f;
        tex.materialRect0V = 0.46f;
        tex.materialFlipbook0Fps = 0.47f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 20.0f) &&
                    nearf(c.materialTimeSec, 0.41f) &&
                    nearf(c.materialFlags, 0.42f) &&
                    nearf(c.materialAtlasWidth, 0.43f) &&
                    nearf(c.materialAtlasHeight, 0.44f) &&
                    nearf(c.materialRect0U, 0.45f) &&
                    nearf(c.materialRect0V, 0.46f) &&
                    nearf(c.materialRect0W, 0.31f) &&
                    nearf(c.materialRect0H, 0.32f) &&
                    nearf(c.materialRect1U, 0.33f) &&
                    nearf(c.materialFlipbook0Fps, 0.47f),
                "Build-model flower review mode should preserve the source flower payload used by its Blender-compensated surface.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 27u;
        tex.materialRect0U = 0.05f;
        tex.materialRect0V = 5.0f;
        tex.materialRect1U = 1.0f;
        tex.materialRect1V = 1.0f;
        tex.materialRect1W = 0.125f;
        tex.materialRect1H = 0.25f;
        tex.materialFlipbook0Cols = 5.0f;
        tex.materialFlipbook0Rows = 0.075f;
        tex.materialFlipbook0Frames = 0.0295f;
        tex.materialFlipbook0Fps = 1.0f;
        tex.materialFlipbook1Cols = 4.0f;
        tex.materialFlipbook1Rows = 0.8f;
        tex.materialFlipbook1Frames = 0.18f;
        tex.materialFlipbook1Fps = 1.0f;
        tex.normalScale = 0.9f;
        tex.metallicFactor = 0.8f;
        tex.roughnessFactor = 0.7f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 27.0f) &&
                    nearf(c.materialRect0U, 0.05f) &&
                    nearf(c.materialRect0V, 5.0f) &&
                    nearf(c.materialRect1U, 1.0f) &&
                    nearf(c.materialRect1V, 1.0f) &&
                    nearf(c.materialRect1W, 0.125f) &&
                    nearf(c.materialRect1H, 0.25f) &&
                    nearf(c.materialFlipbook0Cols, 5.0f) &&
                    nearf(c.materialFlipbook0Rows, 0.075f) &&
                    nearf(c.materialFlipbook0Frames, 0.0295f) &&
                    nearf(c.materialFlipbook1Cols, 4.0f) &&
                    nearf(c.materialFlipbook1Rows, 0.8f) &&
                    nearf(c.materialFlipbook1Frames, 0.18f),
                "Native layered Unlit mode must bypass generic PBR packing and preserve Scarlet material parameters.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 28u;
        tex.roughnessFactor = 0.61f;
        tex.materialRect0U = 0.17f; // Native RoughnessClearCoat.
        tex.materialRect1H = -1.0f; // Plain-Eye no-coat marker.
        tex.cameraPosX = 3.0f;
        tex.cameraPosY = 4.0f;
        tex.cameraPosZ = 19.0f;

        const auto front = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        tex.cameraPosZ = -19.0f;
        const auto rear = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        tex.materialMode = 30u;
        tex.lightProjectionUvRowU = {2.0f, 4.0f, 0.5f, 0.25f};
        const auto animated =
            d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(front.materialRect0U, 0.61f) &&
                    nearf(front.materialRect1H, 19.0f) &&
                    nearf(rear.materialRect1H, -19.0f) &&
                    nearf(front.materialFlipbook1Frames, 0.17f) &&
                    nearf(rear.materialFlipbook1Frames, 0.17f) &&
                    nearf(front.materialTimeSec, -1.0f) &&
                    nearf(rear.materialTimeSec, -1.0f) &&
                    nearf(animated.materialMode, 30.0f) &&
                    nearf(animated.materialFlipbook1Frames, 0.17f) &&
                    nearf(animated.materialTimeSec, -1.0f) &&
                    nearf(animated.lightProjectionUvRowU[0], 2.0f) &&
                    nearf(animated.lightProjectionUvRowU[1], 4.0f) &&
                    nearf(animated.lightProjectionUvRowU[2], 0.5f) &&
                    nearf(animated.lightProjectionUvRowU[3], 0.25f),
                "D3D12 native-eye coat and animated UV parameters must remain independent of camera packing.",
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
        tex.materialMode = 6u;
        tex.emissiveFactorR = 0.05949648097f;
        tex.emissiveFactorG = 0.2319999933f;
        tex.emissiveFactorB = 0.03874399886f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialFlipbook0Cols, tex.emissiveFactorR) &&
                    nearf(c.materialFlipbook0Rows, tex.emissiveFactorG) &&
                    nearf(c.materialFlipbook0Frames, tex.emissiveFactorB),
                "FieldTreeShader05 mode should preserve its captured lightColor upload in the D3D12 specialized payload.",
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

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 11u;
        tex.normalScale = 0.11f;
        tex.metallicFactor = 0.12f;
        tex.roughnessFactor = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.31f;
        tex.materialRect0U = 0.32f;
        tex.materialRect0V = 0.33f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 11.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.31f) &&
                    nearf(c.materialRect0U, 0.32f) &&
                    nearf(c.materialRect0V, 0.33f),
                "FieldGrassShader04 mode should preserve Shadow_Color, Transparent, OnGame, and alpha payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 12u;
        tex.normalScale = 0.11f;
        tex.metallicFactor = 0.12f;
        tex.roughnessFactor = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.24f;
        tex.materialRect0U = 0.25f;
        tex.materialRect0V = 0.31f;
        tex.materialRect0W = 0.32f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 12.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.24f) &&
                    nearf(c.materialRect0U, 0.25f) &&
                    nearf(c.materialRect0V, 0.31f) &&
                    nearf(c.materialRect0W, 0.32f),
                "FieldGrassShader05 mode should preserve Shadow_Color, OnGame, and authored scroll payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 13u;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.24f;
        tex.materialRect0U = 0.25f;
        tex.materialRect0V = 0.26f;
        tex.materialFlipbook0Fps = -2.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 13.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.24f) &&
                    nearf(c.materialRect0U, 0.25f) &&
                    nearf(c.materialRect0V, 0.26f) &&
                    nearf(c.materialFlipbook0Fps, -2.0f),
                "Roadstone overlay mode should preserve Shadow_Color, OnGame, opacity, and source mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 14u;
        tex.normalScale = 0.31f;
        tex.metallicFactor = 0.32f;
        tex.roughnessFactor = 0.33f;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.24f;
        tex.materialRect0U = 0.25f;
        tex.materialFlipbook0Fps = -2.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 14.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.24f) &&
                    nearf(c.materialRect0U, 0.25f) &&
                    nearf(c.materialFlipbook0Cols, 0.31f) &&
                    nearf(c.materialFlipbook0Rows, 0.32f) &&
                    nearf(c.materialFlipbook0Frames, 0.33f) &&
                    nearf(c.materialFlipbook0Fps, -2.0f),
                "Rock-mask overlay mode should preserve independent Shadow_Color, Color, OnGame, opacity, and mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 15u;
        tex.vertexColorMulR = 0.71f;
        tex.vertexColorMulG = 0.72f;
        tex.vertexColorMulB = 0.73f;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.24f;
        tex.materialRect0U = 0.25f;
        tex.materialRect0V = 0.26f;
        tex.materialFlipbook0Fps = 0.27f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 15.0f) &&
                    nearf(c.vertexColorMulR, 0.71f) &&
                    nearf(c.vertexColorMulG, 0.72f) &&
                    nearf(c.vertexColorMulB, 0.73f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.24f) &&
                    nearf(c.materialRect0U, 0.25f) &&
                    nearf(c.materialRect0V, 0.26f) &&
                    nearf(c.materialRect0W, 0.11f) &&
                    nearf(c.materialRect0H, 0.12f) &&
                    nearf(c.materialRect1U, 0.13f) &&
                    nearf(c.materialFlipbook0Fps, 0.27f),
                "Field-flower mode should preserve source Shadow_Color, OnGame, transparency, and mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 16u;
        tex.normalScale = 0.31f;
        tex.metallicFactor = 0.32f;
        tex.roughnessFactor = 0.33f;
        tex.occlusionStrength = 0.34f;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.materialTimeSec = 0.21f;
        tex.materialFlags = 0.22f;
        tex.materialAtlasWidth = 0.23f;
        tex.materialAtlasHeight = 0.24f;
        tex.materialRect0U = 0.25f;
        tex.materialFlipbook0Fps = 0.27f;
        tex.cameraPosX = 1.1f;
        tex.cameraPosY = 1.2f;
        tex.cameraPosZ = 1.3f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 16.0f) &&
                    nearf(c.vertexColorMulR, 0.31f) &&
                    nearf(c.vertexColorMulG, 0.32f) &&
                    nearf(c.vertexColorMulB, 0.33f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.24f) &&
                    nearf(c.materialRect0U, 0.25f) &&
                    nearf(c.materialRect0V, 0.34f) &&
                    nearf(c.materialRect0W, 0.11f) &&
                    nearf(c.materialRect0H, 0.12f) &&
                    nearf(c.materialRect1U, 0.13f) &&
                    nearf(c.materialRect1V, 1.1f) &&
                    nearf(c.materialRect1W, 1.2f) &&
                    nearf(c.materialRect1H, 1.3f) &&
                    nearf(c.materialFlipbook0Fps, 0.27f),
                "Field-rock mode should preserve independent light, rim, shadow, camera, and mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 17u;
        tex.emissiveFactorR = 0.11f;
        tex.emissiveFactorG = 0.12f;
        tex.emissiveFactorB = 0.13f;
        tex.normalScale = 0.21f;
        tex.metallicFactor = 0.22f;
        tex.roughnessFactor = 0.23f;
        tex.materialTimeSec = 0.31f;
        tex.materialFlags = 0.32f;
        tex.materialAtlasWidth = 0.33f;
        tex.materialAtlasHeight = 0.41f;
        tex.materialRect0U = 0.42f;
        tex.materialRect0V = 0.43f;
        tex.materialFlipbook0Fps = 0.51f;
        tex.cameraPosX = 1.1f;
        tex.cameraPosY = 1.2f;
        tex.cameraPosZ = 1.3f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 17.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.31f) &&
                    nearf(c.materialRect0U, 0.32f) &&
                    nearf(c.materialRect0V, 0.33f) &&
                    nearf(c.materialRect0W, 0.41f) &&
                    nearf(c.materialRect0H, 0.42f) &&
                    nearf(c.materialRect1U, 0.43f) &&
                    nearf(c.materialRect1V, 1.1f) &&
                    nearf(c.materialRect1W, 1.2f) &&
                    nearf(c.materialRect1H, 1.3f) &&
                    nearf(c.materialFlipbook0Fps, 0.51f),
                "Field-sign mode should preserve independent light, shadow, rim, camera, and mip-bias payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 18u;
        tex.normalScale = 0.21f;
        tex.metallicFactor = 0.22f;
        tex.roughnessFactor = 0.23f;
        tex.materialTimeSec = 0.31f;
        tex.materialFlags = 0.32f;
        tex.materialAtlasWidth = 0.33f;
        tex.materialAtlasHeight = 0.41f;
        tex.materialRect0U = 0.42f;
        tex.materialRect0V = 0.43f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 18.0f) &&
                    nearf(c.materialTimeSec, 0.21f) &&
                    nearf(c.materialFlags, 0.22f) &&
                    nearf(c.materialAtlasWidth, 0.23f) &&
                    nearf(c.materialAtlasHeight, 0.31f) &&
                    nearf(c.materialRect0U, 0.32f) &&
                    nearf(c.materialRect0V, 0.33f) &&
                    nearf(c.materialRect0W, 0.41f) &&
                    nearf(c.materialRect0H, 0.42f) &&
                    nearf(c.materialRect1U, 0.43f),
                "Encounter-grass mode should preserve independent shadow and rim payloads.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 19u;
        tex.normalScale = 0.11f;
        tex.metallicFactor = 0.12f;
        tex.roughnessFactor = 0.13f;
        tex.emissiveFactorR = 0.21f;
        tex.emissiveFactorG = 0.22f;
        tex.emissiveFactorB = 0.23f;
        tex.materialTimeSec = 0.31f;
        tex.materialFlags = 0.32f;
        tex.materialAtlasWidth = 0.33f;
        tex.materialAtlasHeight = 0.41f;
        tex.materialRect0U = 0.42f;
        tex.materialRect0V = 0.43f;
        tex.materialRect0W = 0.51f;
        tex.materialRect0H = 0.52f;
        tex.materialRect1U = 0.53f;
        tex.materialRect1V = 0.61f;
        tex.materialRect1W = 0.62f;
        tex.materialRect1H = 0.63f;
        tex.cameraPosX = 1.1f;
        tex.cameraPosY = 1.2f;
        tex.cameraPosZ = 1.3f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 19.0f) &&
                    nearf(c.vertexColorMulR, 0.11f) &&
                    nearf(c.vertexColorMulG, 0.12f) &&
                    nearf(c.vertexColorMulB, 0.13f) &&
                    nearf(c.materialRect0W, 0.21f) &&
                    nearf(c.materialRect0H, 0.22f) &&
                    nearf(c.materialRect1U, 0.23f) &&
                    nearf(c.materialRect1V, 0.51f) &&
                    nearf(c.materialRect1W, 0.52f) &&
                    nearf(c.materialRect1H, 0.53f) &&
                    nearf(c.materialFlipbook0Cols, 0.61f) &&
                    nearf(c.materialFlipbook0Rows, 0.62f) &&
                    nearf(c.materialFlipbook0Frames, 0.63f) &&
                    nearf(c.materialFlipbook1Cols, 1.1f) &&
                    nearf(c.materialFlipbook1Rows, 1.2f) &&
                    nearf(c.materialFlipbook1Frames, 1.3f),
                "Grass02 mode should preserve the FieldTreeShader02 payload while selecting its cloud-gated program.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 27u;
        tex.cameraPosX = 7.0f;
        tex.cameraPosY = 8.0f;
        tex.cameraPosZ = 9.0f;
        tex.materialFlipbook0Cols = 0.21f;
        tex.materialFlipbook0Rows = 0.22f;
        tex.materialFlipbook0Frames = 0.23f;

        tex.materialFlags = 3.0f;
        const auto za = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        tex.materialFlags = 3.25f;
        const auto scarlet = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(za.materialFlipbook0Cols, 7.0f) &&
                    nearf(za.materialFlipbook0Rows, 8.0f) &&
                    nearf(za.materialFlipbook0Frames, 9.0f) &&
                    nearf(scarlet.materialFlipbook0Cols, 0.21f) &&
                    nearf(scarlet.materialFlipbook0Rows, 0.22f) &&
                    nearf(scarlet.materialFlipbook0Frames, 0.23f),
                "D3D12 must reserve view-rim camera packing for Z-A Gastly smoke and leave SV NonDirectional colors untouched.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        const unsigned char metallicRoughness[4]{255u, 255u, 0u, 128u};
        tex.materialMode = 2u;
        tex.materialFlags =
            engine::render::backend::kNativeSpecularStrengthMaterialFlag;
        tex.materialRect0U = 0.04f;
        tex.metallicRoughnessRgba = metallicRoughness;
        tex.metallicRoughnessWidth = 1;
        tex.metallicRoughnessHeight = 1;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        const auto pbrFlags = static_cast<std::uint32_t>(c.materialFlags + 0.5f);
        if (!expect(
                (pbrFlags & (1u << 1)) != 0u &&
                    (pbrFlags & (1u << 4)) != 0u &&
                    nearf(c.materialFlipbook1Frames, 0.04f),
                "D3D12 generic PBR packing should preserve native specular-mask opt-in and intensity.",
                outFail)) {
            return false;
        }

        tex.materialFlags = 0.0f;
        const auto generic = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        const auto genericFlags =
            static_cast<std::uint32_t>(generic.materialFlags + 0.5f);
        if (!expect(
                (genericFlags & (1u << 1)) != 0u &&
                    (genericFlags & (1u << 4)) == 0u &&
                    nearf(generic.materialFlipbook1Frames, 1.0f),
                "D3D12 generic PBR materials should continue to ignore metallic/roughness alpha.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode = 31u;
        tex.materialFlags = 4.0f;
        tex.materialRect0H = 1.0f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 31.0f) &&
                    nearf(c.materialFlipbook1Fps, 4.0f) &&
                    nearf(c.materialTimeSec, 1.0f),
                "D3D12 Gastly face packing must retain both its subtype and per-pose tongue concealment guard.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        tex.materialMode =
            engine::render::backend::kNativeIkCharacterMaterialMode;
        tex.materialRect0U = 0.27f;
        tex.materialRect0V = 0.64f;
        tex.materialRect0W = 2.0f;
        tex.materialRect0H = 0.45f;
        tex.occlusionStrength = 1.7f;
        tex.materialRect1U = 0.82f;
        tex.materialRect1V = -0.35f;
        tex.materialRect1W = 0.18f;
        tex.materialRect1H = 0.57f;
        tex.materialFlipbook0Cols = 0.12f;
        tex.materialFlipbook0Rows = 0.21f;
        tex.materialFlipbook0Frames = 40.0f / 360.0f;
        tex.materialFlipbook0Fps = -0.14f;
        tex.materialFlipbook1Cols = 0.31f;
        tex.materialFlipbook1Rows = 300.0f / 360.0f;
        tex.materialFlipbook1Frames = -0.40f;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                nearf(c.materialMode, 32.0f) &&
                    nearf(c.materialRect0V, 1.7f) &&
                    nearf(c.materialTimeSec, 0.27f) &&
                    nearf(c.materialFlipbook1Frames, 0.45f) &&
                    nearf(c.materialFlipbook1Fps, 2060.64f) &&
                    nearf(c.projectedShadowRowX[0], 0.82f) &&
                    nearf(c.projectedShadowRowX[1], -0.35f) &&
                    nearf(c.projectedShadowRowX[2], 0.18f) &&
                    nearf(c.projectedShadowRowX[3], 0.57f) &&
                    nearf(c.projectedShadowRowY[0], 0.12f) &&
                    nearf(c.projectedShadowRowY[1], 0.21f) &&
                    nearf(c.projectedShadowRowY[2], 40.0f / 360.0f) &&
                    nearf(c.projectedShadowRowY[3], -0.14f) &&
                    nearf(c.projectedShadowRowZ[0], 0.31f) &&
                    nearf(c.projectedShadowRowZ[1], 300.0f / 360.0f),
                "D3D12 native IkCharacter packing must retain quality, source surface, GI, shadow-domain, and tonal-domain controls.",
                outFail)) {
            return false;
        }

        tex.materialFlipbook1Frames = 0.45f;
        const auto medium = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        if (!expect(
                    nearf(medium.materialFlipbook1Fps, 2145.64f),
                "D3D12 native IkCharacter packing must preserve the Medium tier's two-decimal LOD without corrupting diffusion.",
                outFail)) {
            return false;
        }
    }

    {
        IRenderBackend::WorldTextureData tex;
        const unsigned char map[4]{255u, 255u, 255u, 255u};
        tex.materialMode =
            engine::render::backend::kNativeSssMaterialMode;
        tex.materialFlags =
            engine::render::backend::kNativeSssSurfaceFibre;
        tex.metallicRoughnessRgba = map;
        tex.metallicRoughnessWidth = 1;
        tex.metallicRoughnessHeight = 1;
        tex.emissiveRgba = map;
        tex.emissiveWidth = 1;
        tex.emissiveHeight = 1;

        const auto c = d3d12i::makeWorldPsConstants(&tex, 1.0f);
        const auto pbrFlags =
            static_cast<std::uint32_t>(c.materialFlags + 0.5f);
        if (!expect(
                nearf(c.materialMode, 33.0f) &&
                    nearf(
                        c.materialTimeSec,
                        engine::render::backend::
                            kNativeSssSurfaceFibre) &&
                    (pbrFlags & (1u << 1)) != 0u &&
                    (pbrFlags & (1u << 3)) != 0u,
                "D3D12 native SSS packing must preserve its surface qualifier alongside roughness and mask presence.",
                outFail)) {
            return false;
        }
    }

    return true;
}

