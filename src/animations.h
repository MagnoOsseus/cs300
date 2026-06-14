#pragma once

#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <string>

namespace Animations
{
using AnimUpdater = std::function<glm::vec3(const glm::vec3&, const glm::vec3&, float)>;

class Anim
{
public:
    Anim(const AnimUpdater& updater, const glm::vec3& param) :
        updater(updater),
        param(param)
    {
    }

    glm::vec3 Update(const glm::vec3& position, float time) const;

private:
    AnimUpdater updater;
    glm::vec3 param;
};

glm::vec3 Sinusoidal(const glm::vec3& position, const glm::vec3& param, float time);
glm::vec3 Orbit(const glm::vec3& position, const glm::vec3& center, float time);

const std::map<std::string, AnimUpdater> NameToUpdater = {
    { "sinusoidal", Sinusoidal },
    { "orbit", Orbit }
};
} // namespace Animations
