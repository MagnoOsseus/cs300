#version 430 core

const int LIGHT_NUM_MAX = 8;
const int LIGHT_TYPE_POINT = 0;
const int LIGHT_TYPE_DIRECTIONAL = 1;
const int LIGHT_TYPE_SPOT = 2;
const int RENDER_MODE_NORMAL = 1;
const int RENDER_MODE_TANGENT = 2;
const int RENDER_MODE_BITANGENT = 3;
const float MIN_EPSILON = 1e-6;

struct Light
{
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float ambient;
    vec3 attenuation;
    float innerAngleCos;
    float outerAngleCos;
    float falloff;
};

in vec3 vViewPos;
in vec3 vViewNormal;
in vec3 vViewTangent;
in vec3 vViewBitangent;
in vec2 vUV;
in vec3 vWorldPos; // World-space position for shadow test.

uniform sampler2D uDiffuseTexture;
// Normal mapping disabled for A3 (kept for code preservation).
uniform bool uUseNormalMap;
uniform sampler2D uNormalTexture;
uniform int uRenderMode;
uniform float uShininess;
uniform float uAmbientBoost;
// Only light[0] is used for A3; multiple-light loop kept but capped at 1.
uniform int uLightNum;
uniform Light uLight[LIGHT_NUM_MAX];

// Shadow map uniforms.
uniform sampler2D uShadowMap;    // Depth texture from light pass.
uniform mat4 uLightVP;           // LP * LV combined matrix.
uniform float uShadowBias;       // Bias read from scene (avoids acne).
uniform int uPcfRadius;          // PCF kernel half-size from scene.
uniform bool uShadowsEnabled;    // Toggled with T key.

out vec4 fragColor;

// Clamps a direction vector to [0,1]: negative components become black.
vec3 VectorToColor(vec3 basis)
{
    return max(normalize(basis), vec3(0.0));
}

float ComputeSpotFactor(Light light, vec3 lightToFragment)
{
    if (light.type != LIGHT_TYPE_SPOT)
    {
        return 1.0;
    }

    vec3 dir = normalize(light.direction);
    float cosAlpha = dot(normalize(lightToFragment), dir);

    // Cosine values are precomputed on the CPU at load time.
    float cosInner = light.innerAngleCos;
    float cosOuter = light.outerAngleCos;

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

// PCF shadow factor: 1.0 = fully lit, 0.0 = fully shadowed.
float ComputeShadow(vec3 worldPos)
{
    if (!uShadowsEnabled)
    {
        return 1.0;
    }

    // Transform world position into light clip space.
    vec4 lightClip = uLightVP * vec4(worldPos, 1.0);

    // Perspective divide to get NDC [-1,1].
    vec3 ndc = lightClip.xyz / lightClip.w;

    // Map NDC to shadow map texture coords [0,1].
    vec3 tc = ndc * 0.5 + 0.5;

    // Fragments outside the shadow frustum are fully lit.
    if (tc.x < 0.0 || tc.x > 1.0 || tc.y < 0.0 || tc.y > 1.0 || tc.z > 1.0)
    {
        return 1.0;
    }

    float fragDepth = tc.z - uShadowBias;

    // PCF: sample (2*uPcfRadius+1)^2 neighbours and average.
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    int count = 0;

    for (int x = -uPcfRadius; x <= uPcfRadius; ++x)
    {
        for (int y = -uPcfRadius; y <= uPcfRadius; ++y)
        {
            float storedDepth = texture(uShadowMap, tc.xy + vec2(x, y) * texelSize).r;
            // 1.0 if lit (fragment not behind stored depth), 0.0 if shadowed.
            shadow += (fragDepth <= storedDepth) ? 1.0 : 0.0;
            ++count;
        }
    }

    return shadow / float(count);
}

void main()
{
    if (uRenderMode == RENDER_MODE_NORMAL)
    {
        // Show the camera-space normal mapped into displayable color.
        fragColor = vec4(VectorToColor(vViewNormal), 1.0);
        return;
    }
    if (uRenderMode == RENDER_MODE_TANGENT)
    {
        // Show the camera-space tangent mapped into displayable color.
        fragColor = vec4(VectorToColor(vViewTangent), 1.0);
        return;
    }
    if (uRenderMode == RENDER_MODE_BITANGENT)
    {
        // Show the camera-space bitangent mapped into displayable color.
        fragColor = vec4(VectorToColor(vViewBitangent), 1.0);
        return;
    }

    vec3 baseColor = texture(uDiffuseTexture, vUV).rgb;
    if (uLightNum <= 0)
    {
        fragColor = vec4(baseColor, 1.0);
        return;
    }

    vec3 NBase = normalize(vViewNormal);
    vec3 T = normalize(vViewTangent);
    vec3 B = normalize(vViewBitangent);
    mat3 TBN = mat3(T, B, NBase);
    vec3 mapNormal = texture(uNormalTexture, vUV).rgb * 2.0 - 1.0;
    // Normal mapping disabled for A3; uUseNormalMap is always false.
    vec3 N = uUseNormalMap ? normalize(TBN * mapNormal) : NBase;
    vec3 V = normalize(-vViewPos);
    vec3 finalColor = vec3(0.0);

    // Compute shadow factor once for light[0] (A3 uses only one light).
    // Each additional light would need its own shadow map in a multi-light setup.
    float shadowFactor = ComputeShadow(vWorldPos);

    // Only light[0] is active for A3 (uLightNum capped to 1 on CPU).
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
        // Ambient coefficient is 1 per assignment; ambient unaffected by shadow.
        float ambientStrength = max(light.ambient, 0.0) + uAmbientBoost;
        vec3 ambientTerm = ambientStrength * baseColor * light.color;

        // Diffuse term.
        vec3 diffuseTerm = light.color * baseColor * NdotL;

        vec3 specularTerm = vec3(0.0);
        if (NdotL > 0.0)
        {
            vec3 R = normalize(2.0 * dot(N, L) * N - L);
            float spec = pow(max(dot(R, V), 0.0), max(uShininess, 1.0));
            // Specular color is white per assignment (specular = vec3(1.0)).
            specularTerm = light.color * vec3(1.0) * spec;
        }

        float attenuation = ComputeAttenuation(light, distanceToLight);
        vec3 lightToFragment = normalize(vViewPos - light.position);
        float spotFactor = ComputeSpotFactor(light, lightToFragment);

        float lightScale = (light.type == LIGHT_TYPE_SPOT) ? spotFactor : 1.0;

        // Shadow only modulates diffuse and specular; ambient is always present.
        vec3 contribution = attenuation * lightScale *
            (ambientTerm + shadowFactor * (diffuseTerm + specularTerm));
        finalColor += contribution;
    }

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}

