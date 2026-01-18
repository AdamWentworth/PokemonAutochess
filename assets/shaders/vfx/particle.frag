// assets/shaders/vfx/particle.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

float smooth01(float a, float b, float x) {
    return smoothstep(a, b, x);
}

void main() {
    // point coord -> [-1..1]
    vec2 p = gl_PointCoord * 2.0 - 1.0;

    // vertical 0..1 (0=bottom, 1=top)
    float t = clamp((p.y + 1.0) * 0.5, 0.0, 1.0);

    // small side wobble (anime-style flame motion)
    float wobble = 0.10 * sin(u_Time * 10.0 + vSeed * 19.0 + t * 6.0);
    p.x += wobble * (1.0 - t);

    // ---- Anime teardrop profile ----
    // thin base -> expand quickly -> tighten tip
    float expand = smooth01(0.04, 0.28, t);                 // fast growth
    float tighten = smooth01(0.65, 1.00, t);                // tight tip

    // base radius starts tiny
    float rBase = 0.06;
    // belly gets big quickly
    float rBelly = mix(rBase, 0.78, expand);
    // tip tightens down
    float radius = mix(rBelly, 0.18, tighten);

    // extra pinch near very bottom (thin wick-like base)
    radius *= mix(0.55, 1.0, smooth01(0.00, 0.20, t));

    radius = max(radius, 0.03);

    // normalized horizontal distance
    float rx = abs(p.x) / radius;

    // hard-ish silhouette with a soft edge (cartoon-ish)
    float edge = 1.0 - smoothstep(0.88, 1.00, rx);

    // clamp top/bottom so it doesn't fill the entire square
    float yMask = smooth01(0.02, 0.08, t) * (1.0 - smooth01(0.98, 1.00, t));

    float shape = edge * yMask;

    float age = clamp(vAge01, 0.0, 1.0);

    // flicker
    float flicker = 0.85 + 0.15 * sin(u_Time * 14.0 + vSeed * 23.0);

    // core intensity (brighter center)
    float core = pow(clamp(edge, 0.0, 1.0), 1.6);

    // ---- Color: more orange/red, less pure yellow ----
    vec3 inner = vec3(1.00, 0.85, 0.35);  // warm yellow/orange
    vec3 mid   = vec3(1.00, 0.45, 0.12);  // orange
    vec3 outer = vec3(0.95, 0.14, 0.06);  // red

    // along height: bottom warmer/yellow, top redder
    vec3 heightCol = mix(inner, mid, smooth01(0.15, 0.55, t));
    heightCol = mix(heightCol, outer, smooth01(0.60, 1.00, t));

    // over lifetime: gets redder as it fades
    vec3 lifeCol = mix(heightCol, outer, pow(age, 0.8));

    // bring back a bright core (but not too yellow)
    vec3 col = mix(lifeCol, inner, core * 0.45);

    // alpha falloff
    float alpha = shape * (1.0 - age) * flicker;

    if (alpha < 0.01) discard;

    FragColor = vec4(col, alpha);
}
