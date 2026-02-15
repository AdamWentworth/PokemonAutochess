// src/game/vfx/GrowlWaveVFX.cpp
#include "GrowlWaveVFX.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

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

bool parseVec3ArrayList(const nlohmann::json& j, std::vector<glm::vec3>& out) {
    if (!j.is_array()) return false;

    std::vector<glm::vec3> parsed;
    parsed.reserve(j.size());
    for (const auto& it : j) {
        glm::vec3 v;
        if (!parseVec3Array(it, v)) continue;
        if (glm::dot(v, v) <= 0.000001f) continue;
        parsed.push_back(v);
    }
    if (parsed.empty()) return false;

    out = std::move(parsed);
    return true;
}

float hash01(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    const std::uint32_t v = x >> 8;
    return static_cast<float>(v) * (1.0f / 16777216.0f);
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
                p.renderMode = it.value("render_mode", p.renderMode);
                p.meshPath = it.value("mesh", p.meshPath);
                p.texturePath = it.value("texture", p.texturePath);
                p.vertShaderPath = it.value("vert_shader", p.vertShaderPath);
                p.fragShaderPath = it.value("frag_shader", p.fragShaderPath);
                if (p.renderMode == "texture_quarter_ring") p.textureQuarterRing = true;
                p.textureQuarterRing = it.value("texture_quarter_ring", p.textureQuarterRing);
                p.quarterCount = std::clamp(it.value("quarter_count", p.quarterCount), 1, 64);
                p.quarterStepDeg = it.value("quarter_step_deg", p.quarterStepDeg);
                p.quarterStartDeg = it.value("quarter_start_deg", p.quarterStartDeg);
                p.useAlphaMaskForColor = it.value("use_alpha_mask_for_color", p.useAlphaMaskForColor);
                p.scaleMul = it.value("scale_mul", p.scaleMul);
                p.alphaMul = it.value("alpha_mul", p.alphaMul);
                p.forwardOffset = it.value("forward_offset", p.forwardOffset);
                p.heightOffset = it.value("height_offset", p.heightOffset);
                p.startRadiusMul = it.value("start_radius_mul", p.startRadiusMul);
                p.radiusMul = it.value("radius_mul", p.radiusMul);
                p.thicknessMul = it.value("thickness_mul", p.thicknessMul);
                p.overrideTev = it.value("override_tev", p.overrideTev);
                p.enabled = it.value("enabled", p.enabled);

                glm::vec3 tint;
                if (it.contains("tint_color") && parseVec3Array(it["tint_color"], tint)) p.tintColor = tint;
                glm::vec3 passMeshForwardAxis;
                if (it.contains("mesh_forward_axis") && parseVec3Array(it["mesh_forward_axis"], passMeshForwardAxis)) {
                    if (glm::dot(passMeshForwardAxis, passMeshForwardAxis) > 0.000001f) {
                        p.meshForwardAxis = passMeshForwardAxis;
                        p.overrideMeshForwardAxis = true;
                    }
                }
                glm::vec3 directionLocal;
                if (it.contains("direction_local") && parseVec3Array(it["direction_local"], directionLocal)) {
                    if (glm::dot(directionLocal, directionLocal) > 0.000001f) {
                        p.directionLocal = directionLocal;
                        p.overrideDirection = true;
                    }
                }
                std::vector<glm::vec3> directionsLocal;
                if (it.contains("directions_local") && parseVec3ArrayList(it["directions_local"], directionsLocal)) {
                    p.directionsLocal = std::move(directionsLocal);
                    p.overrideDirection = true;
                }
                p.directionSpacingJitterDeg =
                    std::max(0.0f, it.value("direction_spacing_jitter_deg", p.directionSpacingJitterDeg));
                p.lineAlphaMin = std::max(0.0f, it.value("line_alpha_min", p.lineAlphaMin));
                p.lineAlphaMax = std::max(0.0f, it.value("line_alpha_max", p.lineAlphaMax));
                if (p.lineAlphaMax < p.lineAlphaMin) std::swap(p.lineAlphaMin, p.lineAlphaMax);
                glm::vec3 tev;
                if (it.contains("tev_c0") && parseVec3Array(it["tev_c0"], tev)) {
                    p.tevC0 = tev;
                    p.overrideTev = true;
                }
                if (it.contains("tev_c1") && parseVec3Array(it["tev_c1"], tev)) {
                    p.tevC1 = tev;
                    p.overrideTev = true;
                }
                if (it.contains("tev_k0") && parseVec3Array(it["tev_k0"], tev)) {
                    p.tevK0 = tev;
                    p.overrideTev = true;
                }
                if (it.contains("tev_k1a") && it["tev_k1a"].is_number()) {
                    p.tevK1A = it["tev_k1a"].get<float>();
                    p.overrideTev = true;
                }

                if (p.textureQuarterRing && !it.contains("mesh")) p.meshPath.clear();
                if (!p.meshPath.empty() || p.textureQuarterRing) parsed.push_back(std::move(p));
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
        p.shader.reset();
    }
    if (quarterQuadVBO != 0) {
        glDeleteBuffers(1, &quarterQuadVBO);
        quarterQuadVBO = 0;
    }
    if (quarterQuadVAO != 0) {
        glDeleteVertexArrays(1, &quarterQuadVAO);
        quarterQuadVAO = 0;
    }
    drawPasses.clear();
    configured = false;
}

void GrowlWaveVFX::ensureQuarterQuadResources() {
    if (quarterQuadVAO != 0 && quarterQuadVBO != 0) return;

    static const float kVerts[] = {
        // pos.xyz      uv
        // UVs are flipped to place the quarter texture's circular center at the local origin.
        0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    };

    glGenVertexArrays(1, &quarterQuadVAO);
    glGenBuffers(1, &quarterQuadVBO);

    glBindVertexArray(quarterQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quarterQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVerts), kVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void GrowlWaveVFX::drawQuarterQuad(const Camera3D& camera, const glm::mat4& world, int locMVP) const {
    if (quarterQuadVAO == 0 || locMVP < 0) return;
    const glm::mat4 mvp = camera.getProjectionMatrix() * camera.getViewMatrix() * world;
    glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glBindVertexArray(quarterQuadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GrowlWaveVFX::ensureConfigured() {
    if (configured || configFailed) return;
    applyDrawManifestOverrides();

    try {
        std::unordered_map<std::string, std::shared_ptr<Shader>> shaderCache;
        drawPasses.clear();
        drawPasses.reserve(cfg.drawPasses.size());

        for (const auto& passCfg : cfg.drawPasses) {
            if (!passCfg.enabled) continue;

            DrawPassRuntime runtime;
            runtime.cfg = passCfg;
            if (passCfg.textureQuarterRing) {
                ensureQuarterQuadResources();
            } else if (!passCfg.meshPath.empty()) {
                runtime.meshModel = std::make_unique<Model>(passCfg.meshPath);
            } else {
                throw std::runtime_error("Draw pass missing mesh path: " + passCfg.id);
            }
            runtime.textureID = loadTextureRGBAOrWhite(passCfg.texturePath);

            const std::string vertPath =
                passCfg.vertShaderPath.empty() ? cfg.vertShaderPath : passCfg.vertShaderPath;
            const std::string fragPath =
                passCfg.fragShaderPath.empty() ? cfg.fragShaderPath : passCfg.fragShaderPath;

            if (vertPath.empty() || fragPath.empty()) {
                throw std::runtime_error("Draw pass missing shader path(s): " + passCfg.id);
            }

            const std::string shaderKey = vertPath + "|" + fragPath;
            auto itShader = shaderCache.find(shaderKey);
            if (itShader == shaderCache.end()) {
                auto compiled = std::make_shared<Shader>(vertPath, fragPath);
                itShader = shaderCache.emplace(shaderKey, compiled).first;
            }
            runtime.shader = itShader->second;

            const GLuint pid = runtime.shader->getID();
            runtime.locMVP = glGetUniformLocation(pid, "uMVP");
            runtime.locTexture = glGetUniformLocation(pid, "uTexture");
            runtime.locFade = glGetUniformLocation(pid, "uFade");
            runtime.locTevC0 = glGetUniformLocation(pid, "uTevC0");
            runtime.locTevC1 = glGetUniformLocation(pid, "uTevC1");
            runtime.locTevK0 = glGetUniformLocation(pid, "uTevK0");
            runtime.locTevK1A = glGetUniformLocation(pid, "uTevK1A");
            runtime.locTintColor = glGetUniformLocation(pid, "uTintColor");
            runtime.locUseAlphaMaskForColor = glGetUniformLocation(pid, "uUseAlphaMaskForColor");
            runtime.locPassAlphaMul = glGetUniformLocation(pid, "uPassAlphaMul");

            runtime.shader->use();
            if (runtime.locTexture >= 0) glUniform1i(runtime.locTexture, 0);
            if (runtime.locFade >= 0) glUniform1f(runtime.locFade, 1.0f);
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
    if (!configured || rings.empty() || drawPasses.empty()) return;

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

    for (const auto& pass : drawPasses) {
        const bool drawMesh = (pass.meshModel != nullptr);
        const bool drawQuarterRing = pass.cfg.textureQuarterRing;
        if ((!drawMesh && !drawQuarterRing) || !pass.shader || pass.textureID == 0 || !pass.cfg.enabled) continue;

        const glm::vec3 passMeshForwardAxis =
            pass.cfg.overrideMeshForwardAxis ? pass.cfg.meshForwardAxis : cfg.meshForwardAxis;
        const glm::vec3 meshForwardLocal =
            (glm::dot(passMeshForwardAxis, passMeshForwardAxis) <= 0.0001f)
            ? glm::vec3(0.0f, 1.0f, 0.0f)
            : glm::normalize(passMeshForwardAxis);
        const glm::vec3 meshForwardAxisWeight = meshForwardLocal * meshForwardLocal;

        const glm::vec3 passTevC0 = pass.cfg.overrideTev ? pass.cfg.tevC0 : cfg.tevC0;
        const glm::vec3 passTevC1 = pass.cfg.overrideTev ? pass.cfg.tevC1 : cfg.tevC1;
        const glm::vec3 passTevK0 = pass.cfg.overrideTev ? pass.cfg.tevK0 : cfg.tevK0;
        const float passTevK1A = pass.cfg.overrideTev ? pass.cfg.tevK1A : cfg.tevK1A;

        pass.shader->use();
        if (pass.locTexture >= 0) glUniform1i(pass.locTexture, 0);
        if (pass.locTevC0 >= 0) glUniform3f(pass.locTevC0, passTevC0.x, passTevC0.y, passTevC0.z);
        if (pass.locTevC1 >= 0) glUniform3f(pass.locTevC1, passTevC1.x, passTevC1.y, passTevC1.z);
        if (pass.locTevK0 >= 0) glUniform3f(pass.locTevK0, passTevK0.x, passTevK0.y, passTevK0.z);
        if (pass.locTevK1A >= 0) glUniform1f(pass.locTevK1A, passTevK1A);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, pass.textureID);
        if (pass.locTintColor >= 0) {
            glUniform3f(pass.locTintColor, pass.cfg.tintColor.x, pass.cfg.tintColor.y, pass.cfg.tintColor.z);
        }
        if (pass.locUseAlphaMaskForColor >= 0) {
            glUniform1i(pass.locUseAlphaMaskForColor, pass.cfg.useAlphaMaskForColor ? 1 : 0);
        }

        for (const auto& r : rings) {
            const float life = std::max(0.0001f, r.lifeSec);
            const float age01 = glm::clamp(r.ageSec / life, 0.0f, 1.0f);
            const float scale = glm::mix(r.startScale, r.endScale, age01) * std::max(0.0f, pass.cfg.scaleMul);
            const glm::vec3 ringForward =
                (glm::dot(r.forward, r.forward) > 0.0001f)
                ? glm::normalize(r.forward)
                : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), ringForward);
            if (glm::dot(right, right) <= 0.0001f) right = glm::vec3(1.0f, 0.0f, 0.0f);
            else right = glm::normalize(right);

            glm::vec3 up = glm::cross(ringForward, right);
            if (glm::dot(up, up) <= 0.0001f) up = glm::vec3(0.0f, 1.0f, 0.0f);
            else up = glm::normalize(up);

            std::vector<glm::vec3> localDirectionsFallback;
            const std::vector<glm::vec3>* localDirections = &pass.cfg.directionsLocal;
            if (localDirections->empty()) {
                localDirectionsFallback.push_back(
                    pass.cfg.overrideDirection
                        ? pass.cfg.directionLocal
                        : glm::vec3(0.0f, 0.0f, 1.0f));
                localDirections = &localDirectionsFallback;
            }

            const float radiusMul = std::max(0.0f, pass.cfg.radiusMul);
            const float thicknessMul = std::max(0.0f, pass.cfg.thicknessMul);
            const glm::vec3 axisScale =
                glm::vec3(radiusMul) + (thicknessMul - radiusMul) * meshForwardAxisWeight;
            const glm::vec3 finalScale = glm::vec3(scale) * axisScale;

            float fade = 1.0f;
            const float fadeStart = glm::clamp(cfg.fadeStart, 0.0f, 1.0f);
            if (age01 > fadeStart) {
                const float t = (age01 - fadeStart) / std::max(0.0001f, (1.0f - fadeStart));
                fade = 1.0f - glm::clamp(t, 0.0f, 1.0f);
            }
            if (pass.locFade >= 0) glUniform1f(pass.locFade, fade);

            for (size_t dirIndex = 0; dirIndex < localDirections->size(); ++dirIndex) {
                glm::vec3 localDirBasisRaw = (*localDirections)[dirIndex];
                if (glm::dot(localDirBasisRaw, localDirBasisRaw) <= 0.000001f) continue;

                if (pass.cfg.directionSpacingJitterDeg > 0.0001f && localDirections->size() > 1) {
                    const glm::vec2 baseXY(localDirBasisRaw.x, localDirBasisRaw.y);
                    const float xyLen = glm::length(baseXY);
                    if (xyLen > 0.0001f) {
                        const float baseAngle = std::atan2(baseXY.y, baseXY.x);
                        const std::uint32_t passSalt =
                            static_cast<std::uint32_t>(pass.cfg.eid) * 0x9e3779b9u;
                        const std::uint32_t dirSalt =
                            static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                        const float noise = hash01(r.randomSeed ^ passSalt ^ dirSalt ^ 0x68e31da4u);
                        const float delta =
                            glm::radians(pass.cfg.directionSpacingJitterDeg) * (noise * 2.0f - 1.0f);
                        const float angle = baseAngle + delta;
                        localDirBasisRaw.x = std::cos(angle) * xyLen;
                        localDirBasisRaw.y = std::sin(angle) * xyLen;
                    }
                }

                float lineAlphaMul = std::max(0.0f, pass.cfg.alphaMul);
                if (pass.cfg.lineAlphaMax > pass.cfg.lineAlphaMin + 0.0001f) {
                    const std::uint32_t passSalt =
                        static_cast<std::uint32_t>(pass.cfg.eid) * 0x9e3779b9u;
                    const std::uint32_t dirSalt =
                        static_cast<std::uint32_t>(dirIndex) * 0x85ebca6bu;
                    const float noise = hash01(r.randomSeed ^ passSalt ^ dirSalt ^ 0x4f1bbcdcu);
                    const float lineFactor = glm::mix(pass.cfg.lineAlphaMin, pass.cfg.lineAlphaMax, noise);
                    lineAlphaMul *= lineFactor;
                }
                if (pass.locPassAlphaMul >= 0) {
                    glUniform1f(pass.locPassAlphaMul, lineAlphaMul);
                }

                const glm::vec3 localDir = glm::normalize(localDirBasisRaw);
                const glm::vec3 worldDir =
                    right * localDir.x +
                    up * localDir.y +
                    ringForward * localDir.z;
                if (glm::dot(worldDir, worldDir) <= 0.0001f) continue;

                const glm::vec3 passForward = glm::normalize(worldDir);
                const glm::quat passRot = rotationFromToSafe(meshForwardLocal, passForward);
                // heightOffset controls base radial spawn spread. startRadiusMul scales that spread
                // without changing direction angles.
                const float radialRadius = pass.cfg.heightOffset * std::max(0.0f, pass.cfg.startRadiusMul);
                const glm::vec3 radialStartOffset =
                    (right * localDirBasisRaw.x + up * localDirBasisRaw.y) * radialRadius;
                const glm::vec3 passPos =
                    r.pos +
                    passForward * pass.cfg.forwardOffset +
                    radialStartOffset;

                const glm::mat4 world =
                    glm::translate(glm::mat4(1.0f), passPos) *
                    glm::mat4_cast(passRot) *
                    glm::scale(glm::mat4(1.0f), finalScale);

                if (drawQuarterRing) {
                    const int quarterCount = std::max(1, pass.cfg.quarterCount);
                    for (int i = 0; i < quarterCount; ++i) {
                        const float quarterDeg = pass.cfg.quarterStartDeg + pass.cfg.quarterStepDeg * static_cast<float>(i);
                        const glm::quat quarterRot = glm::angleAxis(glm::radians(quarterDeg), meshForwardLocal);
                        const glm::mat4 quarterWorld =
                            glm::translate(glm::mat4(1.0f), passPos) *
                            glm::mat4_cast(passRot * quarterRot) *
                            glm::scale(glm::mat4(1.0f), finalScale);
                        drawQuarterQuad(camera, quarterWorld, pass.locMVP);
                    }
                } else {
                    pass.meshModel->drawGeometryWithBoundShader(camera, world, pass.locMVP);
                }
            }
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
        r.forward = fwd;
        r.lifeSec = randRange(cfg.ringMinLifeSec, cfg.ringMaxLifeSec) * lifeScale;
        r.ageSec = 0.0f;
        r.startScale = randRange(cfg.ringMinSize, cfg.ringMaxSize) * sizeScale;
        r.endScale = r.startScale * std::max(1.0f, cfg.ringScaleGrowth);
        r.rot = rotationFromToSafe(meshForward, fwd);
        r.randomSeed = engine::random::nextU32(rng);

        rings.push_back(r);
    }
}
