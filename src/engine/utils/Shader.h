// Shader.h

#ifndef SHADER_H
#define SHADER_H

#include <string>
#include <glad/glad.h>

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
    
    void use() const;
    GLuint getID() const { return ID; }
    
    // Uniform setters
    void setUniform(const std::string &name, float value) const;
    // Additional setters for vectors, matrices, etc.
    
private:
    GLuint ID;
    std::string loadSource(const char* filePath);
    GLuint compileShader(GLenum type, const char* source);
};

#endif // SHADER_H
