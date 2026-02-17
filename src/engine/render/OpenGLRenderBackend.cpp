#include "engine/render/OpenGLRenderBackend.h"

#include <glad/glad.h>

#include "engine/render/Renderer.h"

OpenGLRenderBackend::OpenGLRenderBackend()
    : renderer_(std::make_unique<Renderer>()) {
    glEnable(GL_DEPTH_TEST);
}

OpenGLRenderBackend::~OpenGLRenderBackend() {
    shutdown();
}

void OpenGLRenderBackend::beginFrame(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderBackend::shutdown() {
    if (renderer_) {
        renderer_->shutdown();
        renderer_.reset();
    }
}
