#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 1) in float aAge01;
layout(location = 2) in float aSizePx;
layout(location = 3) in float aSeed;

uniform mat4  u_ViewProj;
uniform float u_PointScale;

out float vAge01;
out float vSeed;

void main() {
    vec4 clip = u_ViewProj * vec4(aPos, 1.0);
    gl_Position = clip;

    float invW = 1.0 / max(0.0001, clip.w);

    // Convert "aSizePx" (your world-size scalar) into screen pixels.
    float px = aSizePx * u_PointScale * invW;

    // OLD was 28px max; that makes the flipbook look tiny.
    gl_PointSize = clamp(px, 2.0, 128.0);

    vAge01 = aAge01;
    vSeed  = aSeed;
}
