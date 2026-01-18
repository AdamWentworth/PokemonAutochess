// assets/shaders/vfx/fire/fire_palette.glsl
// (NO #version here)

// heat01: 0 = hottest, 1 = coolest
vec3 fireColor(float heat01) {
    vec3 hot  = vec3(1.42, 1.00, 0.52);
    vec3 mid  = vec3(1.10, 0.33, 0.10);
    vec3 edge = vec3(0.58, 0.07, 0.03);

    vec3 c = mix(hot, mid,  smoothstep(0.00, 0.38, heat01));
    c      = mix(c, edge, smoothstep(0.48, 1.00, heat01));

    float h = smoothstep(0.18, 0.00, heat01);
    float g = dot(c, vec3(0.3333));
    c = mix(c, vec3(g), h * 0.06);

    return c;
}
