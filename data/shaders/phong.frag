#version 430 core

const int LIGHT_NUM_MAX = 8;
const int LIGHT_TYPE_POINT = 0;
const int LIGHT_TYPE_DIRECTIONAL = 1;
const int LIGHT_TYPE_SPOT = 2;
const float MIN_EPSILON = 1e-6;

struct Light
{
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float ambient;
    vec3 attenuation;
    float innerAngle;
    float outerAngle;
    float falloff;
};

in vec3 vViewPos;
in vec3 vViewNormal;
in vec3 vViewTangent;
in vec3 vViewBitangent;
in vec2 vUV;

uniform bool uUseTexture;
uniform sampler2D uDiffuseTexture;
uniform bool uUseNormalMap;
uniform sampler2D uNormalTexture;
uniform float uShininess;
uniform float uAmbientBoost;
uniform int uLightNum;
uniform Light uLight[LIGHT_NUM_MAX];

out vec4 fragColor;

vec3 UVColor(vec2 uv)
{
    return vec3(clamp(uv.x, 0.0, 1.0), clamp(uv.y, 0.0, 1.0), 0.0);
}

float ComputeSpotFactor(Light light, vec3 lightToFragment)
{
    if (light.type != LIGHT_TYPE_SPOT)
    {
        return 1.0;
    }

    vec3 dir = normalize(light.direction);
    float cosAlpha = dot(normalize(lightToFragment), dir);

    float cosInner = cos(radians(light.innerAngle));
    float cosOuter = cos(radians(light.outerAngle));

    if (cosAlpha <= cosOuter)
    {
        return 0.0;
    }
    if (cosAlpha >= cosInner)
    {
        return 1.0;
    }

    float denom = max(cosInner - cosOuter, MIN_EPSILON);
    float t = (cosAlpha - cosOuter) / denom;
    return clamp(pow(clamp(t, 0.0, 1.0), light.falloff), 0.0, 1.0);
}

float ComputeAttenuation(Light light, float distanceToLight)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
    {
        return 1.0;
    }

    float c1 = light.attenuation.x;
    float c2 = light.attenuation.y;
    float c3 = light.attenuation.z;
    float denom = c1 + c2 * distanceToLight + c3 * distanceToLight * distanceToLight;
    denom = max(denom, MIN_EPSILON);
    return min(1.0 / denom, 1.0);
}

void main()
{
    vec3 baseColor = uUseTexture ? texture(uDiffuseTexture, vUV).rgb : UVColor(vUV);
    if (uLightNum <= 0)
    {
        fragColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 NBase = normalize(vViewNormal);
    vec3 T = normalize(vViewTangent - dot(vViewTangent, NBase) * NBase);
    vec3 B = normalize(vViewBitangent - dot(vViewBitangent, NBase) * NBase);
    mat3 TBN = mat3(T, B, NBase);
    vec3 mapNormal = texture(uNormalTexture, vUV).rgb * 2.0 - 1.0;
    vec3 N = uUseNormalMap ? normalize(TBN * normalize(mapNormal)) : NBase;
    vec3 V = normalize(-vViewPos);
    vec3 finalColor = vec3(0.0);

    for (int i = 0; i < uLightNum; ++i)
    {
        Light light = uLight[i];

        vec3 L = vec3(0.0);
        float distanceToLight = 0.0;

        if (light.type == LIGHT_TYPE_DIRECTIONAL)
        {
            L = normalize(-light.direction);
        }
        else
        {
            vec3 toLight = light.position - vViewPos;
            distanceToLight = length(toLight);
            if (distanceToLight > 0.0)
            {
                L = toLight / distanceToLight;
            }
        }

        float NdotL = max(dot(N, L), 0.0);
        float ambientStrength = max(light.ambient, 0.0) + uAmbientBoost;
        vec3 ambientTerm = ambientStrength * baseColor * light.color;
        vec3 diffuseTerm = light.color * baseColor * NdotL;

        vec3 specularTerm = vec3(0.0);
        if (NdotL > 0.0)
        {
            vec3 R = normalize(2.0 * dot(N, L) * N - L);
            float spec = pow(max(dot(R, V), 0.0), max(uShininess, 1.0));
            specularTerm = light.color * vec3(1.0) * spec;
        }

        float attenuation = ComputeAttenuation(light, distanceToLight);
        vec3 lightToFragment = normalize(vViewPos - light.position);
        float spotFactor = ComputeSpotFactor(light, lightToFragment);

        float lightScale = (light.type == LIGHT_TYPE_SPOT) ? spotFactor : 1.0;
        vec3 contribution = attenuation * lightScale * (ambientTerm + diffuseTerm + specularTerm);
        finalColor += contribution;
    }

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
