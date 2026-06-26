#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "Camera.h"
#include "CS300Parser.h"
#include "Mesh.h"
#include "ShaderManager.h"
#include "animations.h"

static const int kMaxLights = 8;

// Mesh type per scene object.
enum class MeshKind { PLANE, CUBE, CONE, CYLINDER, SPHERE, OBJ };

// Material data for shading.
struct Material
{
    float       shininess           = 10.0f;
    std::string diffuseTexturePath;
    std::string normalTexturePath;
    GLuint      diffuseTexture      = 0;
    GLuint      normalTexture       = 0;
    bool        hasNormalMap        = false;
};

// Scene object data.
struct SceneObject
{
    std::string  name;
    MeshKind     kind    = MeshKind::PLANE;
    std::string  objPath;
    Mesh         mesh;

    glm::vec3 pos{ 0.0f };     // Scene file position.
    glm::vec3 currPos{ 0.0f }; // Position for current frame.
    glm::vec3 rot{ 0.0f };
    glm::vec3 sca{ 1.0f };
    Material  material;

    std::vector<Animations::Anim> anims;

    // Build model matrix from current transform.
    glm::mat4 ModelMatrix() const;
};

// Uniform locations for one light.
struct LightUniformLoc
{
    GLint type          = -1;
    GLint position      = -1;
    GLint direction     = -1;
    GLint color         = -1;
    GLint ambient       = -1;
    GLint attenuation   = -1;
    GLint innerAngleCos = -1;
    GLint outerAngleCos = -1;
    GLint falloff       = -1;
};

// Main app class.
class App
{
public:
    bool Init(const char* sceneFile);
    void Run();
    void Shutdown();

private:
    void LoadScene();
    void SetupShaders();
    void RebuildSlicedMeshes();
    void UpdateAnimations(float elapsedTime);
    void HandleEvents(bool& quit);
    void RenderFrame();

    SDL_Window*   m_window = nullptr;
    SDL_GLContext m_glCtx  = nullptr;

    Camera        m_camera;
    ShaderManager m_shaderManager;
    CS300Parser   m_scene;

    std::vector<SceneObject>            m_objects;
    std::vector<glm::vec3>              m_lightCurrPos;
    std::unordered_map<std::string, GLuint> m_textureCache;

    GLuint m_fallbackTex      = 0;
    GLuint m_whiteTex         = 0;
    GLuint m_defaultNormalTex = 0;
    Mesh   m_lightMarkerMesh;

    GLuint m_mainProg = 0;
    GLuint m_normProg = 0;

    // Main shader uniforms.
    GLint m_uModel        = -1;
    GLint m_uView         = -1;
    GLint m_uProj         = -1;
    GLint m_uDiffuseTex   = -1;
    GLint m_uUseNormalMap = -1;
    GLint m_uNormalTex    = -1;
    GLint m_uRenderMode   = -1;
    GLint m_uShininess    = -1;
    GLint m_uAmbientBoost = -1;
    GLint m_uLightNum     = -1;
    // Normals shader uniform.
    GLint m_uNormMVP = -1;

    std::array<LightUniformLoc, kMaxLights> m_lightUniforms{};

    // Render toggles and state.
    bool  m_showNormals   = false;
    bool  m_faceNormals   = true;
    bool  m_wireframe     = false;
    int   m_renderMode    = 0;
    int   m_currentSlices = 4;
    float m_elapsedTime   = 0.0f;
};
