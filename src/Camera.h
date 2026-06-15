#pragma once
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// Orbital camera that always looks at the target.
class Camera
{
public:
    // Spherical coordinates: r=distance, alpha=polar, beta=azimuth.
    float r     = 10.0f;
    float alpha = glm::half_pi<float>();
    float beta  = -glm::half_pi<float>();

    // Point the camera orbits around.
    glm::vec3 target{ 0.0f };

    // Projection parameters.
    float fovY   = glm::radians(60.0f);
    float aspect = 16.0f / 9.0f;
    float zNear  = 0.1f;
    float zFar   = 1000.0f;

    // Input sensitivity.
    float rotateSensitivity = 0.05f;
    float zoomSensitivity   = 1.0f;

    // Angle and distance limits.
    static constexpr float kAlphaMin = 0.01f;
    static constexpr float kAlphaMax = glm::pi<float>() - 0.01f;
    static constexpr float kRMin     = 0.5f;

    // Sets up camera from a position and target.
    void InitFromLookAt(const glm::vec3& pos,
                        const glm::vec3& tgt,
                        const glm::vec3& )
    {
        target     = tgt;
        // Direction from target to camera.
        glm::vec3 d = pos - tgt;
        r = glm::length(d);
        if (r < kRMin) r = kRMin;

        // Convert to spherical angles.
        if (r > 1e-6f)
        {
            glm::vec3 dn = glm::normalize(d);
            alpha = std::acos(std::max(-1.0f, std::min(1.0f, dn.y)));
            beta  = std::atan2(dn.z, dn.x);
        }
    }

    // Handles W/S (elevation), A/D (orbit), Q/E (zoom).
    void ProcessInput(const bool* keys, float dt)
    {
        float step = dt * 60.0f;

        // Elevation.
        if (keys[SDL_SCANCODE_W]) alpha -= rotateSensitivity * step;
        if (keys[SDL_SCANCODE_S]) alpha += rotateSensitivity * step;

        // Orbit.
        if (keys[SDL_SCANCODE_A]) beta  -= rotateSensitivity * step;
        if (keys[SDL_SCANCODE_D]) beta  += rotateSensitivity * step;

        // Zoom.
        if (keys[SDL_SCANCODE_Q]) r -= zoomSensitivity * step;
        if (keys[SDL_SCANCODE_E]) r += zoomSensitivity * step;

        // Clamp to valid ranges.
        alpha = std::max(kAlphaMin, std::min(kAlphaMax, alpha));
        r     = std::max(kRMin, r);
    }

    // Returns camera world position from spherical coords.
    glm::vec3 GetPosition() const
    {
        return target + glm::vec3(
            r * std::sin(alpha) * std::cos(beta),
            r * std::cos(alpha),
            r * std::sin(alpha) * std::sin(beta));
    }

    // Returns the view matrix.
    glm::mat4 GetView() const
    {
        glm::vec3 pos = GetPosition();
        glm::vec3 up  = glm::vec3(0.0f, 1.0f, 0.0f);
        return glm::lookAt(pos, target, up);
    }

    // Returns the projection matrix.
    glm::mat4 GetProjection() const
    {
        return glm::perspective(fovY, aspect, zNear, zFar);
    }
};

