// src/game/vfx/GrowlWaveVFX.cpp
#include "GrowlWaveVFX.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <glad/glad.h>
#include <stb_image.h>

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/core/Paths.h"
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

    // EID draw setup: repeat + linear filtering.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return tex;
}

bool parseVec3Array(const nlohmann::json& j, glm::vec3& out) {
    if (!j.is_array() || j.size() < 3) return false;
    if (!j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return false;
    out = glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
    return true;
}
} // namespace

GrowlWaveVFX::~GrowlWaveVFX() {
    releaseResources();
}

void GrowlWaveVFX::setConfig(const Config& c) {
    cfg = c;
    applyDrawManifestOverrides();
    rings.clear();
    configFailed = false;
    releaseResources();
}

void GrowlWaveVFX::applyDrawManifestOverrides() {
    if (cfg.drawManifestPath.empty()) return;

    std::ifstream in(cfg.drawManifestPath);
    if (!in.is_open()) {
        const std::string alt = engine::paths::data(cfg.drawManifestPath);
        if (alt != cfg.drawManifestPath) in.open(alt);
    }
    if (!in.is_open()) return;

    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object()) return;

        if (j.contains("shared") && j["shared"].is_object()) {
            const auto& s = j["shared"];
            cfg.vertShaderPath = s.value("vert_shader", cfg.vertShaderPath);
            cfg.fragShaderPath = s.value("frag_shader", cfg.fragShaderPath);
            cfg.depthTest = s.value("depth_test", cfg.depthTest);
            cfg.depthWrite = s.value("depth_write", cfg.depthWrite);
            cfg.tevK1A = s.value("tev_k1a", cfg.tevK1A);

            glm::vec3 v3;
            if (s.contains("mesh_forward_axis") && parseVec3Array(s["mesh_forward_axis"], v3)) cfg.meshForwardAxis = v3;
            if (s.contains("tev_c0") && parseVec3Array(s["tev_c0"], v3)) cfg.tevC0 = v3;
            if (s.contains("tev_c1") && parseVec3Array(s["tev_c1"], v3)) cfg.tevC1 = v3;
            if (s.contains("tev_k0") && parseVec3Array(s["tev_k0"], v3)) cfg.tevK0 = v3;
        }

        if (j.contains("draw_passes") && j["draw_passes"].is_array()) {
            std::vector<Config::DrawPass> parsed;
            parsed.reserve(j["draw_passes"].size());

            for (const auto& it : j["draw_passes"]) {
                if (!it.is_object()) continue;
                Config::DrawPass p{};
                p.id = it.value("id", p.id);
                p.eid = it.value("eid", p.eid);
                p.meshPath = it.value("mesh", p.meshPath);
                p.texturePath = it.value("texture", p.texturePath);
                p.useAlphaMaskForColor = it.value("use_alpha_mask_for_color", p.useAlphaMaskForColor);
                p.scaleMul = it.value("scale_mul", p.scaleMul);
                p.alphaMul = it.value("alpha_mul", p.alphaMul);
                p.enabled = it.value("enabled", p.enabled);

                glm::vec3 tint;
                if (it.contains("tint_color") && parseVec3Array(it["tint_color"], tint)) p.tintColor = tint;

                if (!p.meshPath.empty() && !p.texturePath.empty()) parsed.push_back(std::move(p));
            }

            if (!parsed.empty()) cfg.drawPasses = std::move(parsed);
        }
    } catch (const std::exception& e) {
        std::cerr << "[GrowlWaveVFX] Failed to parse draw manifest '" << cfg.drawManifestPath
                  << "': " << e.what() << "\n";
    }
}

void GrowlWaveVFX::releaseResources() {
    for (auto& p : drawPasses) {
        if (p.textureID != 0) {
            glDeleteTextures(1, &p.textureID);
            p.textureID = 0;
        }
        p.meshModel.reset();
    }
    drawPasses.clear();
    shader.reset();

    locMVP = -1;
    locTexture = -1;
    locFade = -1;
    locTevC0 = -1;
    locTevC1 = -1;
    locTevK0 = -1;
    locTevK1A = -1;
    locTintColor = -1;
    locUseAlphaMaskForColor = -1;
    locPassAlphaMul = -1;
    configured = false;
}

void GrowlWaveVFX::ensureConfigured() {
    if (configured || configFailed) return;
    applyDrawManifestOverrides();

    try {
        shader = std::make_shared<Shader>(cfg.vertShaderPath, cfg.fragShaderPath);

        locMVP = glGetUniformLocation(shader->getID(), "uMVP");
        locTexture = glGetUniformLocation(shader->getID(), "uTexture");
        locFade = glGetUniformLocation(shader->getID(), "uFade");
        locTevC0 = glGetUniformLocation(shader->getID(), "uTevC0");
        locTevC1 = glGetUniformLocation(shader->getID(), "uTevC1");
        locTevK0 = glGetUniformLocation(shader->getID(), "uTevK0");
        locTevK1A = glGetUniformLocation(shader->getID(), "uTevK1A");
        locTintColor = glGetUniformLocation(shader->getID(), "uTintColor");
        locUseAlphaMaskForColor = glGetUniformLocation(shader->getID(), "uUseAlphaMaskForColor");
        locPassAlphaMul = glGetUniformLocation(shader->getID(), "uPassAlphaMul");

        shader->use();
        if (locTexture >= 0) glUniform1i(locTexture, 0);
        if (locFade >= 0) glUniform1f(locFade, 1.0f);
        if (locTevC0 >= 0) glUniform3f(locTevC0, cfg.tevC0.x, cfg.tevC0.y, cfg.tevC0.z);
        if (locTevC1 >= 0) glUniform3f(locTevC1, cfg.tevC1.x, cfg.tevC1.y, cfg.tevC1.z);
        if (locTevK0 >= 0) glUniform3f(locTevK0, cfg.tevK0.x, cfg.tevK0.y, cfg.tevK0.z);
        if (locTevK1A >= 0) glUniform1f(locTevK1A, cfg.tevK1A);

        drawPasses.clear();
        drawPasses.reserve(cfg.drawPasses.size());

        for (const auto& passCfg : cfg.drawPasses) {
            if (!passCfg.enabled) continue;

            DrawPassRuntime runtime;
            runtime.cfg = passCfg;
            runtime.meshModel = std::make_unique<Model>(passCfg.meshPath);
            runtime.textureID = loadTextureRGBAOrWhite(passCfg.texturePath);
            drawPasses.push_back(std::move(runtime));
        }

        if (drawPasses.empty()) {
            throw std::runtime_error("No enabled draw passes configured for Growl.");
        }

        configured = true;
    } catch (const std::exception& e) {
        std::cerr << "[GrowlWaveVFX] Failed to configure growl pipeline: " << e.what() << "\n";
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
    if (!configured || rings.empty() || !shader || drawPasses.empty()) return;

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
    glActiveTexture(GL_TEXTURE0);

    for (const auto& pass : drawPasses) {
        if (!pass.meshModel || pass.textureID == 0 || !pass.cfg.enabled) continue;

        glBindTexture(GL_TEXTURE_2D, pass.textureID);
        if (locTintColor >= 0) glUniform3f(locTintColor, pass.cfg.tintColor.x, pass.cfg.tintColor.y, pass.cfg.tintColor.z);
        if (locUseAlphaMaskForColor >= 0) glUniform1i(locUseAlphaMaskForColor, pass.cfg.useAlphaMaskForColor ? 1 : 0);
        if (locPassAlphaMul >= 0) glUniform1f(locPassAlphaMul, std::max(0.0f, pass.cfg.alphaMul));

        for (const auto& r : rings) {
            const float life = std::max(0.0001f, r.lifeSec);
            const float age01 = glm::clamp(r.ageSec / life, 0.0f, 1.0f);
            const float scale = glm::mix(r.startScale, r.endScale, age01) * std::max(0.0f, pass.cfg.scaleMul);

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

            pass.meshModel->drawGeometryWithBoundShader(camera, world, locMVP);
        }
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
