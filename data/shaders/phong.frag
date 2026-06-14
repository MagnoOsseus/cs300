#version 430 core

struct Light
{
    int type;
    vec3 position;
    vec3 direction;
    vec3 color;
    float ambient;
    vec3 attenuation;
    float innerCos;
    float outerCos;
    float falloff;
};

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUV;

uniform sampler2D uDiffuseTexture;
uniform bool uUseTexture;
uniform vec3 uCameraPosition;
uniform float uShininess;
uniform int uLightCount;
uniform Light uLights[8];

out vec4 fragColor;

vec3 GetBaseColor()
{
    if (uUseTexture)
    {
        return texture(uDiffuseTexture, vUV).rgb;
    }

    return vec3(fract(vUV.x), fract(vUV.y), 0.0);
}

float ComputeAttenuation(Light light, float distanceToLight)
{
    float denominator = light.attenuation.x +
                        light.attenuation.y * distanceToLight +
                        light.attenuation.z * distanceToLight * distanceToLight;

    if (denominator <= 0.0)
    {
        return 1.0;
    }

    return min(1.0 / denominator, 1.0);
}

float ComputeSpotFactor(Light light, vec3 lightVector)
{
    vec3 spotlightDirection = normalize(light.direction);
    float cosAlpha = dot(normalize(-lightVector), spotlightDirection);

    if (cosAlpha <= light.outerCos)
    {
        return 0.0;
    }

    if (cosAlpha >= light.innerCos)
    {
        return 1.0;
    }

    float denominator = max(light.innerCos - light.outerCos, 0.0001);
    float blend = clamp((cosAlpha - light.outerCos) / denominator, 0.0, 1.0);
    return pow(blend, light.falloff);
}

void main()
{
    vec3 baseColor = GetBaseColor();
    vec3 normal = normalize(vWorldNormal);
    vec3 viewVector = normalize(uCameraPosition - vWorldPosition);
    vec3 totalLight = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i)
    {
        Light light = uLights[i];
        vec3 lightVector = vec3(0.0);
        float attenuation = 1.0;
        float spotFactor = 1.0;

        if (light.type == 1)
        {
            lightVector = normalize(-light.direction);
        }
        else
        {
            vec3 toLight = light.position - vWorldPosition;
            float distanceToLight = length(toLight);

            if (distanceToLight <= 0.0001)
            {
                continue;
            }

            lightVector = normalize(toLight);
            attenuation = ComputeAttenuation(light, distanceToLight);

            if (light.type == 2)
            {
                spotFactor = ComputeSpotFactor(light, lightVector);
                if (spotFactor <= 0.0)
                {
                    continue;
                }
            }
        }

        float diffuseFactor = max(dot(normal, lightVector), 0.0);
        vec3 ambientTerm = light.ambient * baseColor;
        vec3 diffuseTerm = light.color * baseColor * diffuseFactor;
        vec3 specularTerm = vec3(0.0);

        if (diffuseFactor > 0.0)
        {
            vec3 reflectionVector = reflect(-lightVector, normal);
            float specularFactor = pow(max(dot(reflectionVector, viewVector), 0.0), uShininess);
            specularTerm = light.color * vec3(1.0) * specularFactor;
        }

        totalLight += ambientTerm + attenuation * spotFactor * (diffuseTerm + specularTerm);
    }

    fragColor = vec4(totalLight, 1.0);
}
