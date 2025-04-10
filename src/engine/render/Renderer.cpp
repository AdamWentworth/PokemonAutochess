// Renderer.cpp

#include "Renderer.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <glm/gtc/type_ptr.hpp>

// Helper function to check for OpenGL errors.
static void checkGLError(const std::string& context) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        std::cerr << "[OpenGL Error] " << context << ": " << err << "\n";
    }
}

Renderer::Renderer() 
    : vbo(GL_ARRAY_BUFFER)  // Initialize vbo with GL_ARRAY_BUFFER
{
    std::cout << "[Renderer] Constructing Renderer...\n";
    float vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };

    std::cout << "[Renderer] Creating shader program using our Shader class...\n";
    // Create the shader using std::make_unique
    shader = std::make_unique<Shader>("assets/shaders/engine/default.vert", "assets/shaders/engine/default.frag");
    std::cout << "[Renderer] Shader program created with ID: " << shader->getID() << "\n";
    checkGLError("After shader program creation");

    glBindVertexArray(vao.getID());
    glBindBuffer(vbo.getTarget(), vbo.getID());
    glBufferData(vbo.getTarget(), sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    checkGLError("After setting up vertex attributes");

    // Get the uniform location for u_MVP from our shader.
    mvpLocation = glGetUniformLocation(shader->getID(), "u_MVP");
    if (mvpLocation == -1) {
        std::cerr << "[Renderer] ERROR: u_MVP uniform not found in shader.\n";
    }
    std::cout << "[Renderer] u_MVP location = " << mvpLocation << "\n";
}

Renderer::~Renderer() {
    // RAII objects (vao and vbo) automatically clean up when Renderer is destroyed.
}

void Renderer::render() {
    shader->use();
    glBindVertexArray(vao.getID());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::shutdown() {
    // No need to explicitly delete vao and vbo; their destructors will handle it.
}
