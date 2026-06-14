#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec2 vUV;

void main()
{
    vec4 worldPosition = uModel * vec4(aPos, 1.0);
    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV;
    gl_Position = uProjection * uView * worldPosition;
}
