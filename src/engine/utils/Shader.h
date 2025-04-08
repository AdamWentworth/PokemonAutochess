// Shader.h

#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>  // Ensure this is included

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
    
    void use() const;
    GLuint getID() const { return ID; }
    
    // Uniform setters
    void setUniform(const std::string &name, float value) const;
    void setUniform(const std::string &name, const glm::mat4 &matrix) const;  // New overload
    void setUniform(const std::string &name, const glm::vec3 &vec) const;     // New overload
    
private:
    GLuint ID;
    std::string loadSource(const char* filePath);
    GLuint compileShader(GLenum type, const char* source);
};

#endif // SHADER_H
