// text.frag

#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D u_Texture;
uniform vec3 u_TextColor;

void main()
{
    // Sample the texture. It is expected that the rendered text has proper alpha.
    vec4 sampled = texture(u_Texture, TexCoord);
    FragColor = vec4(u_TextColor, sampled.a);
}
