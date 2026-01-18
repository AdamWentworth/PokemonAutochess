// assets/shaders/vfx/particle.frag
#version 330 core

in float vAge01;
in float vSeed;

out vec4 FragColor;

uniform float u_Time;

// Flipbook
uniform sampler2D u_Flipbook;
uniform vec2  u_FlipbookGrid;   // (cols, rows)
uniform float u_FrameCount;     // e.g. 40
uniform float u_Fps;            // e.g. 30

void main()
{
    float age = clamp(vAge01, 0.0, 1.0);

    float f = floor(u_Time * u_Fps + vSeed * u_FrameCount);
    float frame = mod(f, u_FrameCount);

    float cols = u_FlipbookGrid.x;
    float rows = u_FlipbookGrid.y;

    float col = mod(frame, cols);
    float rowFromTop = floor(frame / cols);

    // Frame 0 is assumed to be in the TOP row of the atlas.
    // OpenGL UV v=0 is bottom, so invert the row index.
    float row = (rows - 1.0) - rowFromTop;

    // FIX: flip the per-sprite Y so the flame in each cell is upright.
    vec2 local = gl_PointCoord;
    local.y = 1.0 - local.y;

    vec2 cellUV = (vec2(col, row) + local) / vec2(cols, rows);
    vec4 tex = texture(u_Flipbook, cellUV);

    tex.a *= (1.0 - age);
    if (tex.a < 0.01) discard;

    tex.rgb = pow(clamp(tex.rgb, 0.0, 1.0), vec3(1.15));

    FragColor = tex;
}
