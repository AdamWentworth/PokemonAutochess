#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;

layout(push_constant) uniform WorldPushConstants {
    mat4 viewProjection;
    vec4 materialParams;
    vec4 shadingParams;
    vec4 pbrFactors;
    vec4 emissiveAndCamera;
} pushData;

layout(location = 0) out vec2 vertexUv;
layout(location = 1) out vec4 vertexColor;
layout(location = 2) out vec3 vertexNormal;
layout(location = 3) out vec4 vertexTangent;
layout(location = 4) out vec3 worldPosition;

void main() {
    vec4 clip = pushData.viewProjection * vec4(inPosition, 1.0);
    clip.y = -clip.y;
    clip.z = (clip.z + clip.w) * 0.5;
    clip.z -= max(pushData.materialParams.z, 0.0) * clip.w;
    gl_Position = clip;
    vertexUv = inUv;
    vertexColor = inColor;
    vertexNormal = inNormal;
    vertexTangent = inTangent;
    worldPosition = inPosition;
}
