#version 430 core

// Vertex position input.
layout(location = 0) in vec3 aPos;

// Combined light view-projection * model matrix.
uniform mat4 uLightMVP;

void main()
{
    gl_Position = uLightMVP * vec4(aPos, 1.0);
}
