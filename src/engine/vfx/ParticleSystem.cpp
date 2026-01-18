// src/engine/vfx/ParticleSystem.cpp
#include "ParticleSystem.h"

#include "../utils/ShaderLibrary.h"
#include "../utils/Shader.h"
#include "../render/Camera3D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

ParticleSystem::~ParticleSystem() {
    shutdown();
}

void ParticleSystem::init() {
    if (initialized) return;

    shader = ShaderLibrary::get(
        "assets/shaders/vfx/particle.vert",
        "assets/shaders/vfx/particle.frag"
    );

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

        // Fire rises
        p.vel += glm::vec3(0.0f, 2.4f, 0.0f) * dt;

        // Damping
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

    gpuBuffer.resize(particles.size());
    for (size_t i = 0; i < particles.size(); ++i) {
        const Particle& p = particles[i];
        float age01 = 1.0f - (p.lifeSec / std::max(0.0001f, p.maxLifeSec));
        age01 = std::clamp(age01, 0.0f, 1.0f);

        gpuBuffer[i] = GPUParticle{
            p.pos,
            age01,
            p.sizePx,
            p.seed
        };
    }

    // Save GL state
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasProgPoint = glIsEnabled(GL_PROGRAM_POINT_SIZE);

    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    // ✅ REQUIRED so vertex shader gl_PointSize is honored
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Additive blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    shader->use();

    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    shader->setUniform("u_ViewProj", viewProj);
    shader->setUniform("u_Time", timeSec);
    shader->setUniform("u_PointScale", pointScale);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 gpuBuffer.size() * sizeof(GPUParticle),
                 gpuBuffer.data(),
                 GL_STREAM_DRAW);

    glDrawArrays(GL_POINTS, 0, (GLsizei)gpuBuffer.size());

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Restore state
    glDepthMask(prevDepthMask);

    if (!wasDepth) glDisable(GL_DEPTH_TEST);
    if (!wasBlend) glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrcRGB, prevBlendDstRGB);

    if (!wasProgPoint) glDisable(GL_PROGRAM_POINT_SIZE);
}
