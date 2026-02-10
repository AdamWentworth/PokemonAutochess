// assets/shaders/vfx/sprite_additive.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

uniform int u_UseFlipbook;
uniform sampler2D u_Flipbook;
uniform vec2  u_FlipbookGrid;
uniform float u_FrameCount;
uniform float u_Fps;

float hash1(float x) { return fract(sin(x * 12.9898) * 43758.5453); }

void main() {
    float age = clamp(vAge01, 0.0, 1.0);

    vec2 local = gl_PointCoord;
    // Growl cone atlas is authored left->right.
    // Rotate directly from this base orientation.
    vec2 localGrowl = local;
    float ang = (vSeed * 6.2831853) - 3.14159265; // [-pi, pi]
    float c = cos(ang);
    float s = sin(ang);
    vec2 centered = localGrowl - vec2(0.5);
    centered = vec2(
        centered.x * c - centered.y * s,
        centered.x * s + centered.y * c
    );
    localGrowl = centered + vec2(0.5);

    // Mild waviness to avoid rigid straight edges.
    float wave = sin(localGrowl.x * 18.0 + u_Time * 7.5 + vSeed * 23.0) * 0.012;
    localGrowl.y += wave * (1.0 - age);
    localGrowl += vec2(
        (hash1(vSeed * 31.0) - 0.5) * 0.010,
        (hash1(vSeed * 47.0) - 0.5) * 0.014
    );

    vec4 tex;
    if (u_UseFlipbook != 0) {
        float cols = max(1.0, u_FlipbookGrid.x);
        float rows = max(1.0, u_FlipbookGrid.y);
        float frameCount = max(1.0, u_FrameCount);
        float fps = max(0.0, u_Fps);

        float frame = 0.0;
        if (fps > 0.0) {
            float speed = mix(0.90, 1.10, hash1(vSeed * 13.7));
            float f = floor(u_Time * fps * speed + vSeed * frameCount);
            frame = mod(f, frameCount);
        }

        float col = mod(frame, cols);
        float rowFromTop = floor(frame / cols);
        float row = (rows - 1.0) - rowFromTop;
        vec2 clamped = clamp(localGrowl, vec2(0.0), vec2(1.0));
        vec2 cellUV = (vec2(col, row) + clamped) / vec2(cols, rows);
        tex = texture(u_Flipbook, cellUV);
    } else {
        tex = vec4(1.0);
    }

    // Feather sprite bounds so the effect blends with environment.
    const float feather = 0.12;
    vec2 edge = smoothstep(vec2(0.0), vec2(feather), localGrowl) *
                smoothstep(vec2(0.0), vec2(feather), vec2(1.0) - localGrowl);
    float edgeMask = edge.x * edge.y;

    // Longer sustain with a softer exit.
    float fadeIn = smoothstep(0.0, 0.10, age);
    float fadeOut = 1.0 - smoothstep(0.82, 1.0, age);
    float alpha = tex.a * edgeMask * fadeIn * fadeOut;

    if (alpha < 0.0015) discard;
    FragColor = vec4(tex.rgb * alpha, alpha);
}
