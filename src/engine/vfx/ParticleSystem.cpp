// --- FILE: src/engine/vfx/ParticleSystem.cpp ---
// src/engine/vfx/ParticleSystem.cpp
#include "ParticleSystem.h"

#include "engine/utils/Shader.h"
#include "engine/utils/ShaderLibrary.h"
#include "engine/render/Camera3D.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glad/glad.h>

// stb_image implementation is compiled in src/engine/utils/stb_image_impl.cpp.
#include <stb_image.h>

static GLuint create1x1WhiteTextureRGBA() {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    unsigned char white[4] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

    return tex;
}

// If path is empty or load fails, returns 1x1 white.
static unsigned int loadTextureRGBAOrWhite(const std::string& path) {
    if (path.empty()) {
        return create1x1WhiteTextureRGBA();
    }

    int w = 0, h = 0, comp = 0;

    // Keep consistent with your current engine expectations.
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return create1x1WhiteTextureRGBA();
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return tex;
}

ParticleSystem::~ParticleSystem() {
    shutdown();
}

void ParticleSystem::ensureFlipbookLoaded() {
    if (!flipbookDirty && flipbookTex != 0) return;

    if (flipbookTex) {
        glDeleteTextures(1, &flipbookTex);
        flipbookTex = 0;
    }

    flipbookTex = loadTextureRGBAOrWhite(flipbookPath);
    flipbookDirty = false;
}

void ParticleSystem::ensureSecondaryFlipbookLoaded() {
    if (!useSecondaryFlipbook) return;
    if (!flipbookDirty2 && flipbookTex2 != 0) return;

    if (flipbookTex2) {
        glDeleteTextures(1, &flipbookTex2);
        flipbookTex2 = 0;
    }

    flipbookTex2 = loadTextureRGBAOrWhite(flipbookPath2);
    flipbookDirty2 = false;
}

void ParticleSystem::ensureShaderLoaded() {
    if (!shaderDirty && shader) return;

    shader = ShaderLibrary::get(shaderVertPath, shaderFragPath);
    shaderDirty = false;
}

void ParticleSystem::init() {
    if (initialized) return;

    ensureShaderLoaded();

    // Only create/load flipbook texture if this system uses it.
    if (useFlipbook) {
        ensureFlipbookLoaded();
    }

    if (useFlipbook && useSecondaryFlipbook) {
        ensureSecondaryFlipbookLoaded();
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(GPUParticle), nullptr, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, age01));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, sizePx));

    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GPUParticle), (void*)offsetof(GPUParticle, seed));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    initialized = true;
}

void ParticleSystem::shutdown() {
    if (!initialized) return;

    if (flipbookTex) glDeleteTextures(1, &flipbookTex);
    flipbookTex = 0;

    if (flipbookTex2) glDeleteTextures(1, &flipbookTex2);
    flipbookTex2 = 0;

    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);

    vbo = 0;
    vao = 0;

    particles.clear();
    gpuBuffer.clear();

    initialized = false;
}

void ParticleSystem::emit(const Particle& p) {
    particles.push_back(p);
}

void ParticleSystem::update(float dt) {
    if (!initialized) init();

    dt = std::clamp(dt, 0.0f, 0.05f);
    timeSec += dt;

    // Integrate + cull dead
    for (auto& p : particles) {
        p.vel += updateSettings.acceleration * dt;

        // Exponential damping for framerate independence
        float damp = std::pow(updateSettings.dampingBase, dt);
        p.vel *= damp;

        p.pos += p.vel * dt;
        p.lifeSec -= dt;
    }

    particles.erase(
        std::remove_if(particles.begin(), particles.end(),
                       [](const Particle& p) { return p.lifeSec <= 0.0f; }),
        particles.end()
    );
}

static void applyBlendMode(ParticleSystem::BlendMode mode) {
    glEnable(GL_BLEND);

    switch (mode) {
        case ParticleSystem::BlendMode::Alpha:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case ParticleSystem::BlendMode::Additive:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
            break;
        case ParticleSystem::BlendMode::Premultiplied:
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void ParticleSystem::render(const Camera3D& camera) {
    if (!initialized) init();
    ensureShaderLoaded();

    if (!shader || vao == 0) return;
    if (particles.empty()) return;

    // Only require flipbook texture when enabled
    if (useFlipbook) {
        ensureFlipbookLoaded();
        if (flipbookTex == 0) return;
    }

    if (useFlipbook && useSecondaryFlipbook) {
        ensureSecondaryFlipbookLoaded();
        // Secondary can fall back to white; do not early-out on missing tex2.
    }

    // Build GPU buffer
    gpuBuffer.resize(particles.size());
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];

        float age01 = 1.0f - (p.lifeSec / std::max(0.0001f, p.maxLifeSec));
        age01 = std::clamp(age01, 0.0f, 1.0f);

        gpuBuffer[i] = GPUParticle{ p.pos, age01, p.sizePx, p.seed };
    }

    // Save GL state
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasProgPoint = glIsEnabled(GL_PROGRAM_POINT_SIZE);

    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0, prevBlendSrcA = 0, prevBlendDstA = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    GLint prevBlendEqRGB = 0, prevBlendEqA = 0;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqA);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    // Apply render settings (effect-owned)
    if (renderSettings.programPointSize) glEnable(GL_PROGRAM_POINT_SIZE);
    else glDisable(GL_PROGRAM_POINT_SIZE);

    applyBlendMode(renderSettings.blend);

    if (renderSettings.depthTest) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);

    glDepthMask(renderSettings.depthWrite ? GL_TRUE : GL_FALSE);

    shader->use();

    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    shader->setUniform("u_ViewProj", viewProj);
    shader->setUniform("u_Time", timeSec);
    shader->setUniform("u_PointScale", pointScale);

    // Tell shaders that support it whether flipbook sampling is valid (no warning spam)
    {
        GLint loc = glGetUniformLocation(shader->getID(), "u_UseFlipbook");
        if (loc != -1) glUniform1i(loc, useFlipbook ? 1 : 0);
    }    // Flipbook uniforms + bind only if enabled
    if (useFlipbook) {
        shader->setUniform("u_Flipbook", 0);
        shader->setUniform("u_FlipbookGrid", glm::vec2((float)flipbookCols, (float)flipbookRows));
        shader->setUniform("u_FrameCount", (float)flipbookFrames);
        shader->setUniform("u_Fps", flipbookFps);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, flipbookTex);

        // Secondary atlas (optional) — only set if the shader declares the uniforms.
        int has2 = (useSecondaryFlipbook && flipbookTex2 != 0) ? 1 : 0;
        {
            GLint locHas2 = glGetUniformLocation(shader->getID(), "u_HasFlipbook2");
            if (locHas2 != -1) glUniform1i(locHas2, has2);
        }

        if (has2) {
            GLint locSampler2 = glGetUniformLocation(shader->getID(), "u_Flipbook2");
            if (locSampler2 != -1) {
                glUniform1i(locSampler2, 1);

                GLint locGrid2 = glGetUniformLocation(shader->getID(), "u_FlipbookGrid2");
                if (locGrid2 != -1) glUniform2f(locGrid2, (float)flipbookCols2, (float)flipbookRows2);

                GLint locFrames2 = glGetUniformLocation(shader->getID(), "u_FrameCount2");
                if (locFrames2 != -1) glUniform1f(locFrames2, (float)flipbookFrames2);

                GLint locFps2 = glGetUniformLocation(shader->getID(), "u_Fps2");
                if (locFps2 != -1) glUniform1f(locFps2, flipbookFps2);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, flipbookTex2);

                // Restore active texture to 0 for safety.
                glActiveTexture(GL_TEXTURE0);
            }
        }
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(gpuBuffer.size() * sizeof(GPUParticle)),
                 gpuBuffer.data(),
                 GL_STREAM_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)gpuBuffer.size());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Restore GL state
    glDepthMask(prevDepthMask);

    if (!wasDepth) glDisable(GL_DEPTH_TEST);
    if (!wasBlend) glDisable(GL_BLEND);

    glBlendEquationSeparate(prevBlendEqRGB, prevBlendEqA);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcA, prevBlendDstA);

    if (!wasProgPoint) glDisable(GL_PROGRAM_POINT_SIZE);
}
