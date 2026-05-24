#pragma once
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

// -------------------------------------------------------------------------
// Spherical-coordinate camera that always looks at a fixed target.
//
//   position = target + r * (sin(alpha)*cos(beta),
//                             cos(alpha),
//                             sin(alpha)*sin(beta))
//
//   alpha : polar angle   (0 = top,  pi = bottom)
//   beta  : azimuthal     (0 = +X,  pi/2 = +Z, …)
//   r     : radial distance
// -------------------------------------------------------------------------
class Camera
{
public:
    // Camera position in spherical coordinates (relative to target)
    float r     = 10.0f;
    float alpha = glm::half_pi<float>();   // start at the equator
    float beta  = -glm::half_pi<float>(); // behind target on -Z

    glm::vec3 target{ 0.0f };

    // Projection parameters
    float fovY   = glm::radians(60.0f);
    float aspect = 16.0f / 9.0f;
    float zNear  = 0.1f;
    float zFar   = 1000.0f;

    // Sensitivity
    float rotateSensitivity = 0.05f;
    float zoomSensitivity   = 1.0f;

    // Clamp alpha away from poles to avoid gimbal issues
    static constexpr float kAlphaMin = 0.01f;
    static constexpr float kAlphaMax = glm::pi<float>() - 0.01f;
    static constexpr float kRMin     = 0.5f;

    // -----------------------------------------------------------------------
    // Initialise from a standard look-at specification
    // -----------------------------------------------------------------------
    void InitFromLookAt(const glm::vec3& pos,
                        const glm::vec3& tgt,
                        const glm::vec3& /*up*/)
    {
        target     = tgt;
        glm::vec3 d = pos - tgt;
        r = glm::length(d);
        if (r < kRMin) r = kRMin;

        if (r > 1e-6f)
        {
            glm::vec3 dn = glm::normalize(d);
            alpha = std::acos(std::max(-1.0f, std::min(1.0f, dn.y)));
            beta  = std::atan2(dn.z, dn.x);
        }
    }

    // -----------------------------------------------------------------------
    // Keyboard-driven update (call once per frame before rendering)
    // keys = result of SDL_GetKeyboardState(nullptr)
    // -----------------------------------------------------------------------
    void ProcessInput(const bool* keys, float dt)
    {
        // W / S  → elevation (alpha)
        if (keys[SDL_SCANCODE_W]) alpha -= rotateSensitivity * dt * 60.0f;
        if (keys[SDL_SCANCODE_S]) alpha += rotateSensitivity * dt * 60.0f;

        // A / D  → azimuth (beta)
        if (keys[SDL_SCANCODE_A]) beta  -= rotateSensitivity * dt * 60.0f;
        if (keys[SDL_SCANCODE_D]) beta  += rotateSensitivity * dt * 60.0f;

        // Q / E  → zoom (r)
        if (keys[SDL_SCANCODE_Q]) r -= zoomSensitivity * dt * 60.0f;
        if (keys[SDL_SCANCODE_E]) r += zoomSensitivity * dt * 60.0f;

        alpha = std::max(kAlphaMin, std::min(kAlphaMax, alpha));
        r     = std::max(kRMin, r);
    }

    // -----------------------------------------------------------------------
    // Cartesian position of the camera
    // -----------------------------------------------------------------------
    glm::vec3 GetPosition() const
    {
        return target + glm::vec3(
            r * std::sin(alpha) * std::cos(beta),
            r * std::cos(alpha),
            r * std::sin(alpha) * std::sin(beta));
    }

    // -----------------------------------------------------------------------
    // View matrix
    // -----------------------------------------------------------------------
    glm::mat4 GetView() const
    {
        glm::vec3 pos = GetPosition();
        glm::vec3 up  = glm::vec3(0.0f, 1.0f, 0.0f);
        return glm::lookAt(pos, target, up);
    }

    // -----------------------------------------------------------------------
    // Projection matrix
    // -----------------------------------------------------------------------
    glm::mat4 GetProjection() const
    {
        return glm::perspective(fovY, aspect, zNear, zFar);
    }
};
