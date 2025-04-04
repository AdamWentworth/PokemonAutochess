// Camera3D.h

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera3D {
public:
    Camera3D(float fovDeg, float aspect, float nearPlane, float farPlane);

    void setPosition(const glm::vec3& pos);
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0, 1, 0));

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;

private:
    glm::vec3 position;
    glm::vec3 target;
    glm::vec3 upVector;

    float fov;
    float aspectRatio;
    float nearZ;
    float farZ;
};