#pragma once

#include <glm/glm.hpp>

#include <fstream>
#include <string>
#include <vector>

#include "animations.h"

class CS300Parser
{
  public:
    enum class LightType
    {
        Point,
        Directional,
        Spot
    };

    void LoadDataFromFile(const char * filename);

    float     fovy      = 60.0f;
    float     width     = 16.0f;
    float     height    = 9.0f;
    float     nearPlane = 1.0f;
    float     farPlane  = 500.0f;
    glm::vec3 camPos;
    glm::vec3 camTarget;
    glm::vec3 camUp;

    struct Transform
    {
        std::string name;

        std::string mesh;
        std::string diffuseTexture;
        std::string normalTexture;

        glm::vec3 pos;
        glm::vec3 rot;
        glm::vec3 sca;
        float     ns = 10.0f;

        std::vector<Animations::Anim> anims;
    };

    std::vector<Transform> objects;

    struct Light
    {
        LightType  type         = LightType::Point;
        glm::vec3  position{ 0.0f };
        glm::vec3  direction{ 0.0f, -1.0f, 0.0f };
        glm::vec3  color{ 1.0f };
        float      ambient      = 0.0f;
        glm::vec3  attenuation{ 1.0f, 0.0f, 0.0f };
        float      innerAngle   = 0.0f;
        float      outerAngle   = 30.0f;
        float      falloff      = 1.0f;

        std::vector<Animations::Anim> anims;
    };
    std::vector<Light> lights;

  private:
    static float     ReadFloat(std::ifstream & f);
    static glm::vec3 ReadVec3(std::ifstream & f);
};