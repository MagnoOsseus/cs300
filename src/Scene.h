#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "animations.h"

enum class LightType
{
    POINT = 0,
    DIRECTIONAL = 1,
    SPOT = 2
};

struct AttenuationCoefficients
{
    float constant = 1.0f;
    float linear = 0.0f;
    float quadratic = 0.0f;
};

struct SpotlightParameters
{
    float innerAngle = 15.0f;
    float outerAngle = 30.0f;
    float falloff = 1.0f;
};

struct Material
{
    float shininess = 10.0f;
};

struct SceneObject
{
    std::string name;
    std::string mesh;
    std::string diffuseTexture;

    glm::vec3 originalPosition{ 0.0f };
    glm::vec3 currentPosition{ 0.0f };
    glm::vec3 rotation{ 0.0f };
    glm::vec3 scale{ 1.0f };

    Material material;
    std::vector<Animations::Anim> animations;
};

struct Light
{
    LightType type = LightType::POINT;

    glm::vec3 originalPosition{ 0.0f };
    glm::vec3 currentPosition{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 1.0f };

    float ambientCoefficient = 0.0f;

    AttenuationCoefficients attenuation;
    SpotlightParameters spotlight;
    std::vector<Animations::Anim> animations;
};

class Scene
{
public:
    float fovy = 60.0f;
    float width = 16.0f;
    float height = 9.0f;
    float nearPlane = 1.0f;
    float farPlane = 500.0f;

    glm::vec3 camPos{ 0.0f, 0.0f, 10.0f };
    glm::vec3 camTarget{ 0.0f };
    glm::vec3 camUp{ 0.0f, 1.0f, 0.0f };

    std::vector<SceneObject> objects;
    std::vector<Light> lights;

    void FinalizeParsedData();
    void UpdateAnimations(float elapsedTime);
};

bool TryParseLightType(const std::string& text, LightType& type);
