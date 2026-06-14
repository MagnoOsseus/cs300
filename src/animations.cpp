#include "animations.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Animations
{

glm::vec3 Anim::Update(const glm::vec3& position, float time) const
{
    return updater(position, param, time);
}

glm::vec3 Sinusoidal(const glm::vec3& position, const glm::vec3& param, float time)
{
    const float phase = param.x;
    const float frequency = param.y;
    const float amplitude = param.z;
    const float offset = amplitude * glm::sin(frequency * time + phase);
    return position + glm::vec3(0.0f, offset, 0.0f);
}

glm::vec3 Orbit(const glm::vec3& position, const glm::vec3& center, float time)
{
    const glm::mat4 toCenter = glm::translate(glm::mat4(1.0f), center);
    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), time, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 fromCenter = glm::translate(glm::mat4(1.0f), -center);
    const glm::vec4 rotated = toCenter * rotation * fromCenter * glm::vec4(position, 1.0f);
    return glm::vec3(rotated);
}

} // namespace Animations
