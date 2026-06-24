#version 430 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vViewPos;
out vec3 vViewNormal;
out vec3 vViewTangent;
out vec3 vViewBitangent;
out vec2 vUV;

void main()
{
    mat4 MV = uView * uModel;
    // Normal matrix transforms direction vectors to camera space.
    mat3 normalMat = transpose(inverse(mat3(MV)));

    vViewPos       = vec3(MV * vec4(aPos, 1.0));
    vViewNormal    = normalize(normalMat * aNormal);
    vViewTangent   = normalize(normalMat * aTangent);
    vViewBitangent = normalize(normalMat * aBitangent);
    vUV            = aUV;

    gl_Position = uProj * vec4(vViewPos, 1.0);
}
