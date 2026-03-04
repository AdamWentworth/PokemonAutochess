#include "engine/render/OpenGLRenderBackend.h"
#include "engine/render/NeutralPmrem.h"
#include "engine/render/RendererParityContract.h"
#include "engine/core/Environment.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <glad/glad.h>

namespace {

float resolveVertexChannel(float channel, float fallback) {
    return (channel >= 0.0f) ? channel : fallback;
}

bool pbrBindingLogEnabled() {
    static const bool enabled = []() -> bool {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG");
        if (!env.has_value()) return false;
        const std::string raw = *env;
        if (raw == "0" || raw == "false" || raw == "FALSE" || raw == "off" || raw == "OFF") {
            return false;
        }
        return true;
    }();
    return enabled;
}

int pbrDebugViewMode() {
    static const int mode = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_DEBUG_VIEW");
        if (!env.has_value()) return 0;
        try {
            return std::clamp(std::atoi(env->c_str()), 0, 8);
        } catch (...) {
            return 0;
        }
    }();
    return mode;
}

int pbrBindingLogMaxEntries() {
    static const int maxEntries = []() -> int {
        const auto env = engine::env::get("PAC_BACKEND_PBR_BIND_LOG_MAX");
        if (!env.has_value()) return 64;
        try {
            return (std::max)(1, std::atoi(env->c_str()));
        } catch (...) {
            return 64;
        }
    }();
    return maxEntries;
}

void maybeLogPbrBindingOpenGL(const IRenderBackend::WorldTextureData* texture,
                              bool hasBase,
                              bool hasNormal,
                              bool hasMetallicRoughness,
                              bool hasOcclusion,
                              bool hasEmissive) {
    if (!pbrBindingLogEnabled()) return;
    if (!texture || texture->materialMode < 2u) return;
    static int sPrinted = 0;
    if (sPrinted >= pbrBindingLogMaxEntries()) return;
    ++sPrinted;
    std::cout
        << "[PBRBind][OpenGL] key=" << (texture->key ? texture->key : "<null>")
        << " mode=" << static_cast<int>(texture->materialMode)
        << " has(base/norm/mr/occ/emi)="
        << (hasBase ? "1" : "0") << "/"
        << (hasNormal ? "1" : "0") << "/"
        << (hasMetallicRoughness ? "1" : "0") << "/"
        << (hasOcclusion ? "1" : "0") << "/"
        << (hasEmissive ? "1" : "0")
        << " texSize=" << texture->width << "x" << texture->height
        << " normSize=" << texture->normalWidth << "x" << texture->normalHeight
        << " mrSize=" << texture->metallicRoughnessWidth << "x" << texture->metallicRoughnessHeight
        << " roughF=" << texture->roughnessFactor
        << " metalF=" << texture->metallicFactor
        << " occF=" << texture->occlusionStrength
        << "\n";
}

} // namespace

void OpenGLRenderBackend::drawWorldTriangles(const WorldTriangle* triangles,
                                             std::size_t triangleCount,
                                             const float* viewProjectionMatrix4x4,
                                             int surfaceWidth,
                                             int surfaceHeight) {
    if (!triangles || triangleCount == 0 || !viewProjectionMatrix4x4) return;
    constexpr std::size_t kMaxWorldTriangles = 180000;
    const std::size_t safeCount = std::min(triangleCount, kMaxWorldTriangles);
    if (safeCount == 0) return;

    std::vector<WorldMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(safeCount * 3u);
    indices.reserve(safeCount * 3u);
    for (std::size_t i = 0; i < safeCount; ++i) {
        const WorldTriangle& tri = triangles[i];
        const WorldMeshVertex v0{
            tri.x1, tri.y1, tri.z1,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r1, tri.r),
            resolveVertexChannel(tri.g1, tri.g),
            resolveVertexChannel(tri.b1, tri.b),
            resolveVertexChannel(tri.a1, tri.a)};
        const WorldMeshVertex v1{
            tri.x2, tri.y2, tri.z2,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r2, tri.r),
            resolveVertexChannel(tri.g2, tri.g),
            resolveVertexChannel(tri.b2, tri.b),
            resolveVertexChannel(tri.a2, tri.a)};
        const WorldMeshVertex v2{
            tri.x3, tri.y3, tri.z3,
            0.0f, 0.0f,
            resolveVertexChannel(tri.r3, tri.r),
            resolveVertexChannel(tri.g3, tri.g),
            resolveVertexChannel(tri.b3, tri.b),
            resolveVertexChannel(tri.a3, tri.a)};
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
    }
    if (vertices.empty() || indices.empty()) return;

    drawWorldIndexedMeshTextured(vertices.data(),
                                 vertices.size(),
                                 indices.data(),
                                 indices.size(),
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMesh(const WorldMeshVertex* vertices,
                                               std::size_t vertexCount,
                                               const std::uint32_t* indices,
                                               std::size_t indexCount,
                                               const float* viewProjectionMatrix4x4,
                                               int surfaceWidth,
                                               int surfaceHeight) {
    drawWorldIndexedMeshTextured(vertices,
                                 vertexCount,
                                 indices,
                                 indexCount,
                                 nullptr,
                                 viewProjectionMatrix4x4,
                                 surfaceWidth,
                                 surfaceHeight);
}

void OpenGLRenderBackend::drawWorldIndexedMeshCached(const char* geometryKey,
                                                     const WorldMeshVertex* vertices,
                                                     std::size_t vertexCount,
                                                     const std::uint32_t* indices,
                                                     std::size_t indexCount,
                                                     const float* viewProjectionMatrix4x4,
                                                     int surfaceWidth,
                                                     int surfaceHeight) {
    (void)geometryKey;
    drawWorldIndexedMesh(
        vertices,
        vertexCount,
        indices,
        indexCount,
        viewProjectionMatrix4x4,
        surfaceWidth,
        surfaceHeight);
}

void OpenGLRenderBackend::prewarmWorldIndexedMeshCached(const char* geometryKey,
                                                        const WorldMeshVertex* vertices,
                                                        std::size_t vertexCount,
                                                        const std::uint32_t* indices,
                                                        std::size_t indexCount) {
    (void)geometryKey;
    (void)vertices;
    (void)vertexCount;
    (void)indices;
    (void)indexCount;
}

void OpenGLRenderBackend::drawWorldIndexedMeshTextured(const WorldMeshVertex* vertices,
                                                       std::size_t vertexCount,
                                                       const std::uint32_t* indices,
                                                       std::size_t indexCount,
                                                       const WorldTextureData* texture,
                                                       const float* viewProjectionMatrix4x4,
                                                       int surfaceWidth,
                                                       int surfaceHeight) {
    if (!vertices || !indices || vertexCount == 0 || indexCount < 3 || !viewProjectionMatrix4x4) return;
    if (surfaceWidth <= 0 || surfaceHeight <= 0) return;
    ensureWorldPipeline();
    if (worldProgram_ == 0 || worldVao_ == 0 || worldVbo_ == 0 || worldIbo_ == 0 ||
        worldViewProjLoc_ < 0 || worldModelLoc_ < 0 ||
        worldUseTextureLoc_ < 0 || worldTextureSamplerLoc_ < 0 ||
        worldWrapSLoc_ < 0 || worldWrapTLoc_ < 0 || worldVertexColorMulLoc_ < 0 ||
        worldAlphaModeLoc_ < 0 || worldAlphaCutoffLoc_ < 0 ||
        worldCameraPosLoc_ < 0 || worldCameraForwardLoc_ < 0 ||
        worldMaterialModeLoc_ < 0 || worldMaterialTimeLoc_ < 0 || worldMaterialFlagsLoc_ < 0 ||
        worldMaterialAtlasSizeLoc_ < 0 || worldMaterialRect0Loc_ < 0 || worldMaterialRect1Loc_ < 0 ||
        worldMaterialFlipbook0Loc_ < 0 || worldMaterialFlipbook1Loc_ < 0 ||
        worldSkinningEnabledLoc_ < 0 || worldSkinMatrixCountLoc_ < 0 || worldSkinMatricesLoc_ < 0) {
        return;
    }

    constexpr std::size_t kMaxWorldVertices = 540000;
    constexpr std::size_t kMaxWorldIndices = 900000;
    const std::size_t safeVertexCount = std::min(vertexCount, kMaxWorldVertices);
    const std::size_t safeIndexCount = std::min(indexCount, kMaxWorldIndices);
    if (safeVertexCount == 0 || safeIndexCount < 3) return;

    for (std::size_t i = 0; i < safeIndexCount; ++i) {
        if (indices[i] >= safeVertexCount) return;
    }

    const std::uint8_t alphaMode = texture ? std::min<std::uint8_t>(2u, texture->alphaMode) : 0u;
    const std::uint8_t blendMode = texture ? std::min<std::uint8_t>(2u, texture->blendMode) : 0u;
    const float alphaCutoff = texture ? std::clamp(texture->alphaCutoff, 0.0f, 1.0f) : 0.5f;
    const std::uint8_t materialMode = texture ? texture->materialMode : 0u;
    const float vertexColorMulR = texture ? texture->vertexColorMulR : 1.0f;
    const float vertexColorMulG = texture ? texture->vertexColorMulG : 1.0f;
    const float vertexColorMulB = texture ? texture->vertexColorMulB : 1.0f;
    const float vertexColorMulA = texture ? texture->vertexColorMulA : 1.0f;
    const GLuint worldTexture = ensureWorldTexture(texture);
    const bool hasTexture = (worldTexture != 0u);
    static const unsigned char kFallbackWhiteRgba[4] = {255u, 255u, 255u, 255u};
    static const unsigned char kFallbackFlatNormalRgba[4] = {128u, 128u, 255u, 255u};
    const GLuint fallbackWhiteSrgbTexture = ensureWorldTextureRaw(
        "__world_fallback_white_srgb_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/true);
    const GLuint fallbackWhiteLinearTexture = ensureWorldTextureRaw(
        "__world_fallback_white_linear_1x1__",
        kFallbackWhiteRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const GLuint fallbackFlatNormalTexture = ensureWorldTextureRaw(
        "__world_fallback_flat_normal_1x1__",
        kFallbackFlatNormalRgba,
        1,
        1,
        33071,
        33071,
        /*srgb=*/false);
    const GLuint boundTexture = hasTexture ? worldTexture : fallbackWhiteSrgbTexture;
    const float useTexture = hasTexture ? 1.0f : 0.0f;
    const GLuint normalTexture = texture
        ? ensureWorldTextureRaw(
            texture->normalKey,
            texture->normalRgba,
            texture->normalWidth,
            texture->normalHeight,
            texture->normalWrapS,
            texture->normalWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasNormalTexture = (normalTexture != 0u);
    const GLuint boundNormalTexture = hasNormalTexture ? normalTexture : fallbackFlatNormalTexture;
    const GLuint metallicRoughnessTexture = texture
        ? ensureWorldTextureRaw(
            texture->metallicRoughnessKey,
            texture->metallicRoughnessRgba,
            texture->metallicRoughnessWidth,
            texture->metallicRoughnessHeight,
            texture->metallicRoughnessWrapS,
            texture->metallicRoughnessWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasMetallicRoughnessTexture = (metallicRoughnessTexture != 0u);
    const GLuint boundMetallicRoughnessTexture =
        hasMetallicRoughnessTexture ? metallicRoughnessTexture : fallbackWhiteLinearTexture;
    const GLuint occlusionTexture = texture
        ? ensureWorldTextureRaw(
            texture->occlusionKey,
            texture->occlusionRgba,
            texture->occlusionWidth,
            texture->occlusionHeight,
            texture->occlusionWrapS,
            texture->occlusionWrapT,
            /*srgb=*/false)
        : 0u;
    const bool hasOcclusionTexture = (occlusionTexture != 0u);
    const GLuint boundOcclusionTexture = hasOcclusionTexture ? occlusionTexture : fallbackWhiteLinearTexture;
    const GLuint emissiveTexture = texture
        ? ensureWorldTextureRaw(
            texture->emissiveKey,
            texture->emissiveRgba,
            texture->emissiveWidth,
            texture->emissiveHeight,
            texture->emissiveWrapS,
            texture->emissiveWrapT,
            /*srgb=*/true)
        : 0u;
    const bool hasEmissiveTexture = (emissiveTexture != 0u);
    const GLuint boundEmissiveTexture = hasEmissiveTexture ? emissiveTexture : fallbackWhiteSrgbTexture;
    const auto& neutralPmremAtlas = engine::render::neutral_pmrem::getNeutralRoomPmremAtlas();
    const GLuint neutralPmremTexture = !neutralPmremAtlas.rgba16f.empty()
        ? ensureWorldTextureRawHalfFloat(
            "__neutral_room_pmrem_rgba16f_v1__",
            neutralPmremAtlas.rgba16f.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071)
        : ensureWorldTextureRaw(
            "__neutral_room_pmrem_rgbm_v1__",
            neutralPmremAtlas.rgba.data(),
            neutralPmremAtlas.width,
            neutralPmremAtlas.height,
            33071,
            33071,
            /*srgb=*/false);
    const GLuint boundEnvTexture = (neutralPmremTexture != 0u) ? neutralPmremTexture : fallbackWhiteLinearTexture;
    const GLfloat wrapS = static_cast<GLfloat>(texture ? texture->wrapS : 10497);
    const GLfloat wrapT = static_cast<GLfloat>(texture ? texture->wrapT : 10497);
    const bool blendAlpha = (alphaMode == 2u);

    maybeLogPbrBindingOpenGL(
        texture,
        hasTexture,
        hasNormalTexture,
        hasMetallicRoughnessTexture,
        hasOcclusionTexture,
        hasEmissiveTexture);

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTexture2DOnActive = 0;
    GLint prevTexture2DOnUnit[6] = {0, 0, 0, 0, 0, 0};
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnActive);
    for (int unit = 0; unit < 6; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2DOnUnit[unit]);
    }
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    GLint prevFrontFace = GL_CCW;
    glGetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    GLboolean previousDepthMask = GL_TRUE;
    GLint previousDepthFunc = GL_LESS;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &previousDepthMask);
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
    GLint prevBlendSrcRgb = GL_SRC_ALPHA;
    GLint prevBlendDstRgb = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendSrcAlpha = GL_ONE;
    GLint prevBlendDstAlpha = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevBlendEqRgb = GL_FUNC_ADD;
    GLint prevBlendEqAlpha = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);

    glViewport(0, 0, std::max(1, surfaceWidth), std::max(1, surfaceHeight));
    glEnable(GL_DEPTH_TEST);
    glFrontFace(engine::render::parity_contract::kWorldFrontFaceClockwise ? GL_CW : GL_CCW);
    glDepthFunc(engine::render::parity_contract::kWorldDepthFuncLessEqual ? GL_LEQUAL : GL_LESS);
    if (engine::render::parity_contract::kWorldCullEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (blendAlpha) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        switch (blendMode) {
        case 1u:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case 2u:
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case 0u:
        default:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(blendAlpha ? GL_FALSE : GL_TRUE);

    glUseProgram(worldProgram_);
    glUniformMatrix4fv(worldViewProjLoc_, 1, GL_FALSE, viewProjectionMatrix4x4);
    static constexpr float kIdentityModel[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
    const float* modelMatrix = texture ? texture->modelMatrix.data() : kIdentityModel;
    glUniformMatrix4fv(worldModelLoc_, 1, GL_FALSE, modelMatrix);
    const float cameraPosX = texture ? texture->cameraPosX : 0.0f;
    const float cameraPosY = texture ? texture->cameraPosY : 7.0f;
    const float cameraPosZ = texture ? texture->cameraPosZ : 9.0f;
    const float cameraForwardX = texture ? texture->cameraForwardX : 0.0f;
    const float cameraForwardY = texture ? texture->cameraForwardY : -0.6139406f;
    const float cameraForwardZ = texture ? texture->cameraForwardZ : -0.7893522f;
    const float cameraTargetX = texture ? texture->cameraTargetX : 0.0f;
    const float cameraTargetY = texture ? texture->cameraTargetY : -1.0f;
    const float cameraTargetZ = texture ? texture->cameraTargetZ : 0.0f;
    glUniform3f(worldCameraPosLoc_, cameraPosX, cameraPosY, cameraPosZ);
    glUniform3f(worldCameraForwardLoc_, cameraForwardX, cameraForwardY, cameraForwardZ);
    if (worldCameraTargetLoc_ >= 0) {
        glUniform3f(worldCameraTargetLoc_, cameraTargetX, cameraTargetY, cameraTargetZ);
    }
    glUniform1f(worldUseTextureLoc_, useTexture);
    glUniform4f(
        worldVertexColorMulLoc_,
        vertexColorMulR,
        vertexColorMulG,
        vertexColorMulB,
        vertexColorMulA);
    if (worldUseNormalTextureLoc_ >= 0) {
        glUniform1f(worldUseNormalTextureLoc_, hasNormalTexture ? 1.0f : 0.0f);
    }
    if (worldUseMetallicRoughnessTextureLoc_ >= 0) {
        glUniform1f(worldUseMetallicRoughnessTextureLoc_, hasMetallicRoughnessTexture ? 1.0f : 0.0f);
    }
    if (worldUseOcclusionTextureLoc_ >= 0) {
        glUniform1f(worldUseOcclusionTextureLoc_, hasOcclusionTexture ? 1.0f : 0.0f);
    }
    if (worldUseEmissiveTextureLoc_ >= 0) {
        glUniform1f(worldUseEmissiveTextureLoc_, hasEmissiveTexture ? 1.0f : 0.0f);
    }
    glUniform1f(worldWrapSLoc_, wrapS);
    glUniform1f(worldWrapTLoc_, wrapT);
    glUniform1f(worldAlphaModeLoc_, static_cast<GLfloat>(alphaMode));
    glUniform1f(worldAlphaCutoffLoc_, alphaCutoff);
    if (worldNormalScaleLoc_ >= 0) {
        glUniform1f(worldNormalScaleLoc_, texture ? std::max(0.0f, texture->normalScale) : 1.0f);
    }
    if (worldMetallicFactorLoc_ >= 0) {
        glUniform1f(
            worldMetallicFactorLoc_,
            texture ? std::clamp(texture->metallicFactor, 0.0f, 1.0f) : 1.0f);
    }
    if (worldRoughnessFactorLoc_ >= 0) {
        glUniform1f(
            worldRoughnessFactorLoc_,
            texture ? std::clamp(texture->roughnessFactor, 0.0f, 1.0f) : 1.0f);
    }
    if (worldOcclusionStrengthLoc_ >= 0) {
        glUniform1f(
            worldOcclusionStrengthLoc_,
            texture ? std::clamp(texture->occlusionStrength, 0.0f, 1.0f) : 1.0f);
    }
    if (worldEmissiveFactorLoc_ >= 0) {
        glUniform3f(worldEmissiveFactorLoc_,
                    texture ? std::max(0.0f, texture->emissiveFactorR) : 0.0f,
                    texture ? std::max(0.0f, texture->emissiveFactorG) : 0.0f,
                    texture ? std::max(0.0f, texture->emissiveFactorB) : 0.0f);
    }
    if (worldCharacterInkingEnabledLoc_ >= 0) {
        glUniform1f(
            worldCharacterInkingEnabledLoc_,
            (texture && texture->characterInkingEnabled != 0u) ? 1.0f : 0.0f);
    }
    glUniform1f(worldMaterialModeLoc_, static_cast<GLfloat>(materialMode));
    glUniform1f(worldMaterialTimeLoc_, texture ? texture->materialTimeSec : 0.0f);
    glUniform1f(worldMaterialFlagsLoc_, texture ? texture->materialFlags : 0.0f);
    glUniform2f(worldMaterialAtlasSizeLoc_,
                texture ? texture->materialAtlasWidth : 0.0f,
                texture ? texture->materialAtlasHeight : 0.0f);
    glUniform4f(worldMaterialRect0Loc_,
                texture ? texture->materialRect0U : 0.0f,
                texture ? texture->materialRect0V : 0.0f,
                texture ? texture->materialRect0W : 1.0f,
                texture ? texture->materialRect0H : 1.0f);
    glUniform4f(worldMaterialRect1Loc_,
                texture ? texture->materialRect1U : 0.0f,
                texture ? texture->materialRect1V : 0.0f,
                texture ? texture->materialRect1W : 1.0f,
                texture ? texture->materialRect1H : 1.0f);
    glUniform4f(worldMaterialFlipbook0Loc_,
                texture ? texture->materialFlipbook0Cols : 1.0f,
                texture ? texture->materialFlipbook0Rows : 1.0f,
                texture ? texture->materialFlipbook0Frames : 1.0f,
                texture ? texture->materialFlipbook0Fps : 0.0f);
    glUniform4f(worldMaterialFlipbook1Loc_,
                texture ? texture->materialFlipbook1Cols : 1.0f,
                texture ? texture->materialFlipbook1Rows : 1.0f,
                texture ? texture->materialFlipbook1Frames : 1.0f,
                (texture && materialMode >= 2u)
                    ? static_cast<float>(pbrDebugViewMode())
                    : (texture ? texture->materialFlipbook1Fps : 0.0f));
    constexpr int kMaxGpuSkinMatrices = 64;
    const bool gpuSkinningEnabled =
        texture &&
        texture->gpuSkinning != 0u &&
        texture->skinMatrices != nullptr &&
        texture->skinMatrixCount > 0u;
    const int gpuSkinMatrixCount = gpuSkinningEnabled
        ? std::min<int>(static_cast<int>(texture->skinMatrixCount), kMaxGpuSkinMatrices)
        : 0;
    glUniform1f(worldSkinningEnabledLoc_, gpuSkinningEnabled ? 1.0f : 0.0f);
    glUniform1i(worldSkinMatrixCountLoc_, gpuSkinMatrixCount);
    if (gpuSkinMatrixCount > 0) {
        glUniformMatrix4fv(worldSkinMatricesLoc_, gpuSkinMatrixCount, GL_FALSE, texture->skinMatrices);
    }
    glUniform1i(worldTextureSamplerLoc_, 0);
    if (worldNormalTextureSamplerLoc_ >= 0) glUniform1i(worldNormalTextureSamplerLoc_, 1);
    if (worldMetallicRoughnessTextureSamplerLoc_ >= 0) glUniform1i(worldMetallicRoughnessTextureSamplerLoc_, 2);
    if (worldOcclusionTextureSamplerLoc_ >= 0) glUniform1i(worldOcclusionTextureSamplerLoc_, 3);
    if (worldEmissiveTextureSamplerLoc_ >= 0) glUniform1i(worldEmissiveTextureSamplerLoc_, 4);
    if (worldEnvTextureSamplerLoc_ >= 0) glUniform1i(worldEnvTextureSamplerLoc_, 5);
    if (worldEnvTexelSizeLoc_ >= 0) {
        glUniform2f(worldEnvTexelSizeLoc_, neutralPmremAtlas.texelWidth, neutralPmremAtlas.texelHeight);
    }
    if (worldEnvMaxMipLoc_ >= 0) {
        glUniform1f(worldEnvMaxMipLoc_, neutralPmremAtlas.maxMip);
    }
    if (worldEnvRgbmRangeLoc_ >= 0) {
        glUniform1f(worldEnvRgbmRangeLoc_, neutralPmremAtlas.rgbmRange);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, boundTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, boundNormalTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, boundMetallicRoughnessTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, boundOcclusionTexture);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, boundEmissiveTexture);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, boundEnvTexture);

    glBindVertexArray(worldVao_);
    glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                 vertices,
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(safeIndexCount * sizeof(std::uint32_t)),
                 indices,
                 GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(safeIndexCount), GL_UNSIGNED_INT, nullptr);
    ++frameDrawCalls_;
    frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);

    const bool drawCharacterOutline =
        texture &&
        texture->characterInkingEnabled != 0u &&
        materialMode >= 2u &&
        safeVertexCount > 0u;
    if (drawCharacterOutline) {
        static thread_local std::vector<WorldMeshVertex> outlineVertices;
        outlineVertices.resize(safeVertexCount);
        std::copy(vertices, vertices + safeVertexCount, outlineVertices.begin());

        // Keep this subtle: thin silhouette ring instead of heavy toon border.
        const float kOutlineExtrude = 0.001f;
        for (WorldMeshVertex& v : outlineVertices) {
            const float lenSq = v.nx * v.nx + v.ny * v.ny + v.nz * v.nz;
            if (lenSq > 1e-10f) {
                const float invLen = 1.0f / std::sqrt(lenSq);
                v.x += v.nx * invLen * kOutlineExtrude;
                v.y += v.ny * invLen * kOutlineExtrude;
                v.z += v.nz * invLen * kOutlineExtrude;
            }
            v.r = 0.0f;
            v.g = 0.0f;
            v.b = 0.0f;
            v.a = 1.0f;
        }

        glUniform1f(worldUseTextureLoc_, 0.0f);
        glUniform1f(worldMaterialModeLoc_, 3.0f);
        glDepthMask(GL_FALSE);

        glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(safeVertexCount * sizeof(WorldMeshVertex)),
                     outlineVertices.data(),
                     GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(safeIndexCount), GL_UNSIGNED_INT, nullptr);
        ++frameDrawCalls_;
        frameTriangles_ += static_cast<std::uint64_t>(safeIndexCount / 3u);
    }

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
    glUseProgram(static_cast<GLuint>(prevProgram));

    for (int unit = 0; unit < 6; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnUnit[unit]));
    }
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2DOnActive));

    glDepthMask(previousDepthMask);
    glDepthFunc(static_cast<GLenum>(previousDepthFunc));
    glBlendEquationSeparate(static_cast<GLenum>(prevBlendEqRgb), static_cast<GLenum>(prevBlendEqAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                        static_cast<GLenum>(prevBlendDstRgb),
                        static_cast<GLenum>(prevBlendSrcAlpha),
                        static_cast<GLenum>(prevBlendDstAlpha));
    if (!blendEnabled) glDisable(GL_BLEND);
    glFrontFace(static_cast<GLenum>(prevFrontFace));
    if (cullEnabled) glEnable(GL_CULL_FACE);
    if (!depthEnabled) glDisable(GL_DEPTH_TEST);
}
