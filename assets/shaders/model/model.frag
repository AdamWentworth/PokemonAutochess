// model.frag
#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

// Minimal glTF material support: baseColor + emissive + alpha modes.
uniform sampler2D u_BaseColorTex;
uniform sampler2D u_EmissiveTex;
uniform vec3  u_EmissiveFactor; // includes any emissiveStrength multiplier

// 0=OPAQUE, 1=MASK, 2=BLEND (glTF)
uniform int   u_AlphaMode;
uniform float u_AlphaCutoff;

void main()
{
    vec4 base = texture(u_BaseColorTex, TexCoord);
    vec3 emissive = texture(u_EmissiveTex, TexCoord).rgb * u_EmissiveFactor;
    vec4 outC = vec4(base.rgb + emissive, base.a);

    if (u_AlphaMode == 0) {
        outC.a = 1.0;
    } else if (u_AlphaMode == 1) {
        if (outC.a < u_AlphaCutoff) discard;
        outC.a = 1.0;
    }

    FragColor = outC;
}


