// Shader.h

#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <unordered_map>

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
    
    void use() const;
    GLuint getID() const { return ID; }
    
    // Uniform setters
    void setUniform(const std::string &name, float value) const;
    void setUniform(const std::string &name, const glm::mat4 &matrix) const;
    void setUniform(const std::string &name, const glm::vec3 &vec) const;
    
private:
    GLuint ID;
    std::string loadSource(const char* filePath);
    GLuint compileShader(GLenum type, const char* source);

    // Cache for uniform locations. Marked mutable so it can be updated in const functions.
    mutable std::unordered_map<std::string, GLint> uniformLocationCache;

    // Helper function to get (and cache) uniform locations.
    GLint getUniformLocation(const std::string &name) const;
};

#endif // SHADER_H