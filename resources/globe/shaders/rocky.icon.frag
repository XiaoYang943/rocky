#version 450

// uniforms
layout(set = 0, binding = 2) uniform sampler2D u_iconTexture;

// inputs
layout(location = 0) in vec2 uv;

// outputs
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(u_iconTexture, uv);
}
