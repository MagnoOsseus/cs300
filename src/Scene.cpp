#include "Scene.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}
}

bool TryParseLightType(const std::string& text, LightType& type)
{
    const std::string upper = ToUpper(text);

    if (upper == "POINT")
    {
        type = LightType::POINT;
        return true;
    }

    if (upper == "DIR")
    {
        type = LightType::DIRECTIONAL;
        return true;
    }

    if (upper == "SPOT")
    {
        type = LightType::SPOT;
        return true;
    }

    return false;
}

void Scene::FinalizeParsedData()
{
    for (auto& object : objects)
    {
        object.currentPosition = object.originalPosition;
    }

    for (auto& light : lights)
    {
        light.currentPosition = light.originalPosition;
    }
}

void Scene::UpdateAnimations(float elapsedTime)
{
    for (auto& object : objects)
    {
        object.currentPosition = object.originalPosition;
        for (const auto& animation : object.animations)
        {
            object.currentPosition = animation.Update(object.currentPosition, elapsedTime);
        }
    }

    for (auto& light : lights)
    {
        light.currentPosition = light.originalPosition;
        for (const auto& animation : light.animations)
        {
            light.currentPosition = animation.Update(light.currentPosition, elapsedTime);
        }
    }
}
