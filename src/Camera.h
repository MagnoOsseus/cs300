#pragma once
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// Simple orbital camera that always looks at the target.
class Camera
{
public:
    // Spherical-coordinate state.
    // r: distance to target, alpha: polar angle, beta: azimuth angle.
    float r     = 10.0f;
    float alpha = glm::half_pi<float>();
    float beta  = -glm::half_pi<float>();

    // Look-at target point.
    glm::vec3 target{ 0.0f };

    // Projection parameters.
    // fovY is in radians.
    float fovY   = glm::radians(60.0f);
    float aspect = 16.0f / 9.0f;
    float zNear  = 0.1f;
    float zFar   = 1000.0f;

    // Control sensitivity.
    // Values are scaled by delta time.
    float rotateSensitivity = 0.05f;
    float zoomSensitivity   = 1.0f;

    // Limits to avoid invalid angles and distances.
    // Keep alpha away from poles and radius above zero.
    static constexpr float kAlphaMin = 0.01f;
    static constexpr float kAlphaMax = glm::pi<float>() - 0.01f;
    static constexpr float kRMin     = 0.5f;

    // Initializes the camera from lookAt-style data.
    // pos: camera position, tgt: look-at target.
    void InitFromLookAt(const glm::vec3& pos,
                        const glm::vec3& tgt,
                        const glm::vec3& )
    {
        // Store the target used by view matrix.
        target     = tgt;
        // Direction from target to camera.
        glm::vec3 d = pos - tgt;
        r = glm::length(d);
        if (r < kRMin) r = kRMin;

        // Convert direction vector to spherical angles.
        if (r > 1e-6f)
        {
            // Normalize before angle extraction.
            glm::vec3 dn = glm::normalize(d);
            // Polar angle from +Y.
            alpha = std::acos(std::max(-1.0f, std::min(1.0f, dn.y)));
            // Azimuth angle on XZ plane.
            beta  = std::atan2(dn.z, dn.x);
        }
    }

    // Processes W/S/A/D/Q/E controls to move the camera.
    // W/S: elevation, A/D: orbit around target, Q/E: zoom.
    void ProcessInput(const bool* keys, float dt)
    {
        // Scale movement to look consistent across frame rates.
        float step = dt * 60.0f;

        // W/S: changes elevation.
        if (keys[SDL_SCANCODE_W]) alpha -= rotateSensitivity * step;
        if (keys[SDL_SCANCODE_S]) alpha += rotateSensitivity * step;

        // A/D: rotates around the target.
        if (keys[SDL_SCANCODE_A]) beta  -= rotateSensitivity * step;
        if (keys[SDL_SCANCODE_D]) beta  += rotateSensitivity * step;

        // Q/E: zoom.
        if (keys[SDL_SCANCODE_Q]) r -= zoomSensitivity * step;
        if (keys[SDL_SCANCODE_E]) r += zoomSensitivity * step;

        // Clamp to valid movement ranges.
        alpha = std::max(kAlphaMin, std::min(kAlphaMax, alpha));
        r     = std::max(kRMin, r);
    }

    // Computes camera Cartesian position.
    // Converts (r, alpha, beta) into (x, y, z) around target.
    glm::vec3 GetPosition() const
    {
        // Convert spherical coordinates into world position around target.
        return target + glm::vec3(
            r * std::sin(alpha) * std::cos(beta),
            r * std::cos(alpha),
            r * std::sin(alpha) * std::sin(beta));
    }

    // Returns the view matrix.
    glm::mat4 GetView() const
    {
        // Current camera position from spherical coordinates.
        glm::vec3 pos = GetPosition();
        // Fixed world-up vector.
        glm::vec3 up  = glm::vec3(0.0f, 1.0f, 0.0f);
        return glm::lookAt(pos, target, up);
    }

    // Returns the projection matrix.
    glm::mat4 GetProjection() const
    {
        // Perspective projection using configured camera parameters.
        return glm::perspective(fovY, aspect, zNear, zFar);
    }
};

