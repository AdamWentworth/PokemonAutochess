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

// Simple RGBA texture loader (no mipmaps)
static unsigned int loadTextureRGBA(const std::string& path) {
    int w = 0, h = 0, comp = 0;

    // Keep consistent with your current engine expectations.
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return 0;
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

    flipbookTex = loadTextureRGBA(flipbookPath);
    flipbookDirty = false;
}

void ParticleSystem::init() {
    if (initialized) return;

    shader = ShaderLibrary::get(
        "assets/shaders/vfx/particle.vert",
        "assets/shaders/vfx/particle.frag"
    );

    ensureFlipbookLoaded();

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // Stream buffer; size grows via glBufferData each frame in render()
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

    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);

    vbo = 0;
    vao = 0;

    particles.clear();
    gpuBuffer.clear();
    shader.reset();

    initialized = false;
}

void ParticleSystem::emit(const Particle& p) {
    particles.push_back(p);
}

void ParticleSystem::update(float dt) {
    if (!initialized) init();

    dt = std::clamp(dt, 0.0f, 0.05f);
    timeSec += dt;

    for (size_t i = 0; i < particles.size();) {
        Particle& p = particles[i];

        p.lifeSec -= dt;
        if (p.lifeSec <= 0.0f || p.maxLifeSec <= 0.0f) {
            particles[i] = particles.back();
            particles.pop_back();
            continue;
        }

        // Fire feel: rise + damping
        p.vel += glm::vec3(0.0f, 1.2f, 0.0f) * dt;

        float damp = std::pow(0.07f, dt);
        p.vel *= damp;

        p.pos += p.vel * dt;

        ++i;
    }
}

void ParticleSystem::render(const Camera3D& camera) {
    if (!initialized) init();
    if (!shader || vao == 0) return;
    if (particles.empty()) return;

    ensureFlipbookLoaded();
    if (flipbookTex == 0) return;

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

    // Fire rendering states
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);

    // Additive fire (bright core, no muddy dark edges)
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    shader->use();

    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    shader->setUniform("u_ViewProj", viewProj);
    shader->setUniform("u_Time", timeSec);
    shader->setUniform("u_PointScale", pointScale);

    shader->setUniform("u_Flipbook", 0);
    shader->setUniform("u_FlipbookGrid", glm::vec2((float)flipbookCols, (float)flipbookRows));
    shader->setUniform("u_FrameCount", (float)flipbookFrames);
    shader->setUniform("u_Fps", flipbookFps);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, flipbookTex);

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
