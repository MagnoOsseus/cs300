#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec2 vUV;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    mat3 normalMat = transpose(inverse(mat3(uModel)));

    vWorldPos = worldPos.xyz;
    vWorldNormal = normalize(normalMat * aNormal);
    vUV = aUV;

    gl_Position = uProj * uView * worldPos;
}
