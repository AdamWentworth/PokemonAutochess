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

Renderer::Renderer() {
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

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    checkGLError("After generating VAO and VBO");

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
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
    // unique_ptr automatically cleans up the shader.
}

void Renderer::render() {
    shader->use();
    // Set any required uniforms here.
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::shutdown() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    // No need to delete the shader manually.
}
