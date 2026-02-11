#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
// Model VAO stores COLOR_0 at location 4.
layout(location = 4) in vec4 aColor;

uniform mat4 uMVP;

out vec2 vUV;
out vec4 vColor;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV = aUV;
    vColor = aColor;
}
