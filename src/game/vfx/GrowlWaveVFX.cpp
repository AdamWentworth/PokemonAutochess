// src/game/vfx/GrowlWaveVFX.cpp
#include "GrowlWaveVFX.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glad/glad.h>
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "engine/utils/Shader.h"

namespace {
unsigned int create1x1WhiteTextureRGBA() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    static const unsigned char kWhite[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhite);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

unsigned int loadTextureRGBAOrWhite(const std::string& path) {
    if (path.empty()) return create1x1WhiteTextureRGBA();

    stbi_set_flip_vertically_on_load(false);

    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return create1x1WhiteTextureRGBA();
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // EID 1076 setup: repeat + linear filtering.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}
} // namespace

GrowlWaveVFX::~GrowlWaveVFX() {
    releaseResources();
}

void GrowlWaveVFX::setConfig(const Config& c) {
    cfg = c;
    rings.clear();
    configFailed = false;
    releaseResources();
}

void GrowlWaveVFX::releaseResources() {
    if (textureID != 0) {
        glDeleteTextures(1, &textureID);
        textureID = 0;
    }
    shader.reset();
    meshModel.reset();
    locMVP = -1;
    locTexture = -1;
    locFade = -1;
    locTevC0 = -1;
    locTevC1 = -1;
    locTevK0 = -1;
    locTevK1A = -1;
    locTintColor = -1;
    locUseAlphaMaskForColor = -1;
    configured = false;
}

void GrowlWaveVFX::ensureConfigured() {
    if (configured || configFailed) return;

    try {
        meshModel = std::make_unique<Model>(cfg.meshPath);
        shader = std::make_shared<Shader>(cfg.vertShaderPath, cfg.fragShaderPath);
        textureID = loadTextureRGBAOrWhite(cfg.texturePath);

        locMVP = glGetUniformLocation(shader->getID(), "uMVP");
        locTexture = glGetUniformLocation(shader->getID(), "uTexture");
        locFade = glGetUniformLocation(shader->getID(), "uFade");
        locTevC0 = glGetUniformLocation(shader->getID(), "uTevC0");
        locTevC1 = glGetUniformLocation(shader->getID(), "uTevC1");
        locTevK0 = glGetUniformLocation(shader->getID(), "uTevK0");
        locTevK1A = glGetUniformLocation(shader->getID(), "uTevK1A");
        locTintColor = glGetUniformLocation(shader->getID(), "uTintColor");
        locUseAlphaMaskForColor = glGetUniformLocation(shader->getID(), "uUseAlphaMaskForColor");

        shader->use();
        if (locTexture >= 0) glUniform1i(locTexture, 0);
        if (locFade >= 0) glUniform1f(locFade, 1.0f);
        if (locTevC0 >= 0) glUniform3f(locTevC0, cfg.tevC0.x, cfg.tevC0.y, cfg.tevC0.z);
        if (locTevC1 >= 0) glUniform3f(locTevC1, cfg.tevC1.x, cfg.tevC1.y, cfg.tevC1.z);
        if (locTevK0 >= 0) glUniform3f(locTevK0, cfg.tevK0.x, cfg.tevK0.y, cfg.tevK0.z);
        if (locTevK1A >= 0) glUniform1f(locTevK1A, cfg.tevK1A);
        if (locTintColor >= 0) glUniform3f(locTintColor, cfg.tintColor.x, cfg.tintColor.y, cfg.tintColor.z);
        if (locUseAlphaMaskForColor >= 0) glUniform1i(locUseAlphaMaskForColor, cfg.useAlphaMaskForColor ? 1 : 0);

        configured = true;
    } catch (const std::exception& e) {
        std::cerr << "[GrowlWaveVFX] Failed to configure growl ring pipeline: " << e.what() << "\n";
        releaseResources();
        configFailed = true;
    }
}

float GrowlWaveVFX::rand01() {
    return engine::random::nextFloat01(rng);
}

float GrowlWaveVFX::randRange(float a, float b) {
    if (b < a) std::swap(a, b);
    return a + (b - a) * rand01();
}

glm::vec3 GrowlWaveVFX::safeForwardXZ(const glm::vec3& v) const {
    glm::vec3 f(v.x, 0.0f, v.z);
    const float len = glm::length(f);
    if (len <= 0.0001f) return glm::vec3(0.0f, 0.0f, 1.0f);
    return f / len;
}

glm::quat GrowlWaveVFX::rotationFromToSafe(const glm::vec3& from, const glm::vec3& to) const {
    glm::vec3 a = from;
    glm::vec3 b = to;
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la <= 0.0001f || lb <= 0.0001f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a /= la;
    b /= lb;

    const float d = glm::clamp(glm::dot(a, b), -1.0f, 1.0f);
    if (d > 0.9999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (d < -0.9999f) {
        glm::vec3 ortho = glm::cross(a, glm::vec3(1.0f, 0.0f, 0.0f));
        if (glm::dot(ortho, ortho) <= 0.0001f) ortho = glm::cross(a, glm::vec3(0.0f, 1.0f, 0.0f));
        ortho = glm::normalize(ortho);
        return glm::angleAxis(3.14159265f, ortho);
    }

    const glm::vec3 axis = glm::normalize(glm::cross(a, b));
    const float angle = std::acos(d);
    return glm::angleAxis(angle, axis);
}

void GrowlWaveVFX::update(float dt) {
    ensureConfigured();
    if (!configured) return;

    dt = std::clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f) return;

    for (auto& r : rings) {
        r.ageSec += dt;
        r.pos += r.vel * dt;
    }

    rings.erase(
        std::remove_if(rings.begin(), rings.end(),
            [](const RingInstance& r) { return r.ageSec >= r.lifeSec; }),
        rings.end());
}

void GrowlWaveVFX::render(const Camera3D& camera) {
    ensureConfigured();
    if (!configured || rings.empty() || !meshModel || !shader || textureID == 0) return;

    const GLboolean prevCullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevDepthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevBlendEnabled = glIsEnabled(GL_BLEND);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    GLint prevDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    GLint prevSrcRGB = GL_ONE, prevDstRGB = GL_ZERO, prevSrcA = GL_ONE, prevDstA = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);

    GLint prevEqRGB = GL_FUNC_ADD, prevEqA = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevEqA);

    GLint prevProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);

    GLint prevActiveTex = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);

    GLint prevTex2D = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);

    GLint prevVAO = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);

    // EID 1076 state block.
    glDisable(GL_CULL_FACE);

    if (cfg.depthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(cfg.depthWrite ? GL_TRUE : GL_FALSE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBlendEquation(GL_FUNC_ADD);

    shader->use();
    if (locTexture >= 0) glUniform1i(locTexture, 0);
    if (locTevC0 >= 0) glUniform3f(locTevC0, cfg.tevC0.x, cfg.tevC0.y, cfg.tevC0.z);
    if (locTevC1 >= 0) glUniform3f(locTevC1, cfg.tevC1.x, cfg.tevC1.y, cfg.tevC1.z);
    if (locTevK0 >= 0) glUniform3f(locTevK0, cfg.tevK0.x, cfg.tevK0.y, cfg.tevK0.z);
    if (locTevK1A >= 0) glUniform1f(locTevK1A, cfg.tevK1A);
    if (locTintColor >= 0) glUniform3f(locTintColor, cfg.tintColor.x, cfg.tintColor.y, cfg.tintColor.z);
    if (locUseAlphaMaskForColor >= 0) glUniform1i(locUseAlphaMaskForColor, cfg.useAlphaMaskForColor ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);

    for (const auto& r : rings) {
        const float life = std::max(0.0001f, r.lifeSec);
        const float age01 = glm::clamp(r.ageSec / life, 0.0f, 1.0f);
        const float scale = glm::mix(r.startScale, r.endScale, age01);

        float fade = 1.0f;
        const float fadeStart = glm::clamp(cfg.fadeStart, 0.0f, 1.0f);
        if (age01 > fadeStart) {
            const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
            fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
        }
        if (locFade >= 0) glUniform1f(locFade, fade);

        const glm::mat4 world =
            glm::translate(glm::mat4(1.0f), r.pos) *
            glm::mat4_cast(r.rot) *
            glm::scale(glm::mat4(1.0f), glm::vec3(scale));

        meshModel->drawGeometryWithBoundShader(camera, world, locMVP);
    }

    glBindVertexArray((GLuint)prevVAO);
    glUseProgram((GLuint)prevProgram);

    glActiveTexture((GLenum)prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex2D);

    glDepthMask(prevDepthMask);
    glDepthFunc((GLenum)prevDepthFunc);
    if (prevDepthEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);

    if (prevBlendEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevSrcRGB, prevDstRGB, prevSrcA, prevDstA);
    glBlendEquationSeparate(prevEqRGB, prevEqA);

    if (prevCullEnabled) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
}

void GrowlWaveVFX::emitFrom(const glm::vec3& mouthWorldPos,
                            const glm::vec3& forwardDir,
                            const glm::mat4* viewMatrix) {
    (void)viewMatrix;

    ensureConfigured();
    if (!configured) return;

    const glm::vec3 fwd = safeForwardXZ(forwardDir);
    glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), fwd);
    if (glm::length(right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
    else right = glm::normalize(right);

    const glm::vec3 origin =
        mouthWorldPos +
        glm::vec3(0.0f, cfg.spawnHeightOffset, 0.0f) +
        fwd * cfg.spawnForwardOffset;

    const int trailCount = std::max(0, cfg.ringTrailCount);
    const int totalRings = 1 + trailCount;

    float forwardOffset = cfg.ringForwardOffset;
    const float spacingMin = std::max(0.0f, std::min(cfg.ringTrailSpacingMin, cfg.ringTrailSpacingMax));
    const float spacingMax = std::max(0.0f, std::max(cfg.ringTrailSpacingMin, cfg.ringTrailSpacingMax));

    const float speedFalloff = glm::clamp(cfg.ringTrailSpeedFalloff, 0.35f, 1.0f);
    const float lifeFalloff = glm::clamp(cfg.ringTrailLifeFalloff, 0.35f, 1.0f);
    const float sizeFalloff = glm::clamp(cfg.ringTrailSizeFalloff, 0.35f, 1.0f);

    const glm::vec3 meshForward =
        (glm::dot(cfg.meshForwardAxis, cfg.meshForwardAxis) <= 0.0001f)
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : glm::normalize(cfg.meshForwardAxis);

    for (int i = 0; i < totalRings; ++i) {
        if (i > 0) forwardOffset += randRange(spacingMin, spacingMax);

        const float speedScale = std::pow(speedFalloff, static_cast<float>(i));
        const float lifeScale = std::pow(lifeFalloff, static_cast<float>(i));
        float sizeScale = std::pow(sizeFalloff, static_cast<float>(i));
        if (i == 0) sizeScale *= std::max(1.0f, cfg.ringLeadSizeMul);

        const float lateral = (i == 0)
            ? 0.0f
            : randRange(-cfg.ringTrailLateralJitter, cfg.ringTrailLateralJitter);
        const float vertical = (i == 0)
            ? 0.0f
            : randRange(-cfg.ringTrailLateralJitter * 0.35f, cfg.ringTrailLateralJitter * 0.35f);

        RingInstance r;
        r.pos = origin + fwd * forwardOffset + right * lateral + glm::vec3(0.0f, vertical, 0.0f);
        r.vel = fwd * randRange(cfg.ringMinSpeed, cfg.ringMaxSpeed) * speedScale;
        r.lifeSec = randRange(cfg.ringMinLifeSec, cfg.ringMaxLifeSec) * lifeScale;
        r.ageSec = 0.0f;
        r.startScale = randRange(cfg.ringMinSize, cfg.ringMaxSize) * sizeScale;
        r.endScale = r.startScale * std::max(1.0f, cfg.ringScaleGrowth);
        r.rot = rotationFromToSafe(meshForward, fwd);

        rings.push_back(r);
    }
}
