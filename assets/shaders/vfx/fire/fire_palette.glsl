// assets/shaders/vfx/fire/fire_palette.glsl
// (NO #version here)

// Small helper to keep fire saturated
vec3 adjustSaturation(vec3 c, float sat) {
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(l), c, sat);
}

// heat01: 0 = hottest, 1 = coolest
vec3 fireColor(float heat01) {
    // "hot" is orange, NOT white-yellow
    vec3 hot  = vec3(1.05, 0.42, 0.18);
    vec3 mid  = vec3(0.92, 0.26, 0.11);
    vec3 edge = vec3(0.60, 0.08, 0.04);

    vec3 c = mix(hot, mid,  smoothstep(0.00, 0.55, heat01));
    c      = mix(c, edge, smoothstep(0.55, 1.00, heat01));

    // Trim brightness + keep saturation
    c *= 0.82;
    c = adjustSaturation(c, 1.05);
    return c;
}
