#version 460

// Texture arena (fixed size)
//layout(set = 0, binding = 3) uniform sampler samp;
layout(set = 0, binding = 4) uniform sampler2D u_textures[1];

// input varyings
layout(location = 0) in vec2 uv;
layout(location = 1) flat in int textureIndex;

// outputs
layout(location = 0) out vec4 outColor;

void main()
{
    const vec4 ERROR_COLOR = vec4(1,0,0,1);

    if (textureIndex < 0)
    {
        outColor = ERROR_COLOR;
    }
    else
    {
        outColor = texture(u_textures[textureIndex], uv);
    }

    if (outColor.a < 0.15)
        discard;
}
