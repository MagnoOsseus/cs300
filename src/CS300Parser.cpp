#include "CS300Parser.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
enum class LastAdded
{
    NONE,
    OBJECT,
    LIGHT
};
}

float CS300Parser::ReadFloat(std::ifstream& file)
{
    std::string token;
    file >> token;
    return std::strtof(token.c_str(), nullptr);
}

int CS300Parser::ReadInt(std::ifstream& file)
{
    std::string token;
    file >> token;
    return std::atoi(token.c_str());
}

glm::vec3 CS300Parser::ReadVec3(std::ifstream& file)
{
    return glm::vec3(ReadFloat(file), ReadFloat(file), ReadFloat(file));
}

Scene CS300Parser::LoadDataFromFile(const char* filename) const
{
    std::ifstream input(filename);
    if (!input.is_open())
    {
        std::cerr << "Could not open input file: " << filename << '\n';
        std::exit(1);
    }

    Scene scene;
    LastAdded lastAdded = LastAdded::NONE;
    std::string token;

    while (input >> token)
    {
        if (!token.empty() && token[0] == '#')
        {
            std::getline(input, token);
            continue;
        }

        if (token == "fovy")
        {
            scene.fovy = ReadFloat(input);
        }
        else if (token == "width")
        {
            scene.width = ReadFloat(input);
        }
        else if (token == "height")
        {
            scene.height = ReadFloat(input);
        }
        else if (token == "near")
        {
            scene.nearPlane = ReadFloat(input);
        }
        else if (token == "far")
        {
            scene.farPlane = ReadFloat(input);
        }
        else if (token == "camPosition")
        {
            scene.camPos = ReadVec3(input);
        }
        else if (token == "camTarget")
        {
            scene.camTarget = ReadVec3(input);
        }
        else if (token == "camUp")
        {
            scene.camUp = ReadVec3(input);
        }
        else if (token == "object")
        {
            SceneObject object;
            input >> object.name;
            scene.objects.push_back(object);
            lastAdded = LastAdded::OBJECT;
        }
        else if (token == "light")
        {
            scene.lights.emplace_back();
            lastAdded = LastAdded::LIGHT;
        }
        else if (token == "translate" || token == "translation")
        {
            const glm::vec3 position = ReadVec3(input);
            if (lastAdded == LastAdded::OBJECT && !scene.objects.empty())
            {
                scene.objects.back().originalPosition = position;
                scene.objects.back().currentPosition = position;
            }
            else if (lastAdded == LastAdded::LIGHT && !scene.lights.empty())
            {
                scene.lights.back().originalPosition = position;
                scene.lights.back().currentPosition = position;
            }
        }
        else if (token == "rotation")
        {
            if (!scene.objects.empty())
            {
                scene.objects.back().rotation = ReadVec3(input);
            }
        }
        else if (token == "scale")
        {
            if (!scene.objects.empty())
            {
                scene.objects.back().scale = ReadVec3(input);
            }
        }
        else if (token == "mesh")
        {
            if (!scene.objects.empty())
            {
                input >> scene.objects.back().mesh;
            }
        }
        else if (token == "shininess")
        {
            if (!scene.objects.empty())
            {
                scene.objects.back().material.shininess = ReadFloat(input);
            }
        }
        else if (token == "texture" || token == "diffuseTexture")
        {
            if (!scene.objects.empty())
            {
                input >> scene.objects.back().diffuseTexture;
            }
        }
        else if (token == "color")
        {
            if (!scene.lights.empty())
            {
                scene.lights.back().color = ReadVec3(input);
            }
        }
        else if (token == "ambient")
        {
            if (!scene.lights.empty())
            {
                scene.lights.back().ambientCoefficient = ReadFloat(input);
            }
        }
        else if (token == "lightType")
        {
            if (!scene.lights.empty())
            {
                std::string typeText;
                input >> typeText;
                TryParseLightType(typeText, scene.lights.back().type);
            }
        }
        else if (token == "attenuation")
        {
            if (!scene.lights.empty())
            {
                const glm::vec3 attenuation = ReadVec3(input);
                scene.lights.back().attenuation.constant = attenuation.x;
                scene.lights.back().attenuation.linear = attenuation.y;
                scene.lights.back().attenuation.quadratic = attenuation.z;
            }
        }
        else if (token == "direction")
        {
            if (!scene.lights.empty())
            {
                scene.lights.back().direction = ReadVec3(input);
            }
        }
        else if (token == "spotAttenuation")
        {
            if (!scene.lights.empty())
            {
                const glm::vec3 spotlight = ReadVec3(input);
                scene.lights.back().spotlight.innerAngle = spotlight.x;
                scene.lights.back().spotlight.outerAngle = spotlight.y;
                scene.lights.back().spotlight.falloff = spotlight.z;
            }
        }
        else if (token == "normalMap")
        {
            std::string ignored;
            input >> ignored;
        }
        else if (token == "reflector")
        {
            ReadFloat(input);
        }
        else if (token == "bias")
        {
            ReadFloat(input);
        }
        else if (token == "pcf")
        {
            ReadInt(input);
        }
        else if (token == "envMap")
        {
            for (int i = 0; i < 6; ++i)
            {
                std::string ignored;
                input >> ignored;
            }
        }
        else if (Animations::NameToUpdater.find(token) != Animations::NameToUpdater.end())
        {
            const glm::vec3 parameter = ReadVec3(input);
            if (lastAdded == LastAdded::OBJECT && !scene.objects.empty())
            {
                scene.objects.back().animations.emplace_back(Animations::NameToUpdater.at(token), parameter);
            }
            else if (lastAdded == LastAdded::LIGHT && !scene.lights.empty())
            {
                scene.lights.back().animations.emplace_back(Animations::NameToUpdater.at(token), parameter);
            }
        }
        else
        {
            std::string ignoredLine;
            std::getline(input, ignoredLine);
            std::cerr << "Ignoring unsupported token '" << token << "' in " << filename << '\n';
        }
    }

    scene.FinalizeParsedData();
    return scene;
}
