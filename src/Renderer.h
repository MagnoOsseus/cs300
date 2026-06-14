#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "Camera.h"
#include "Mesh.h"
#include "Scene.h"
#include "ShaderManager.h"

enum class MeshKind
{
    PLANE,
    CUBE,
    CONE,
    CYLINDER,
    SPHERE,
    OBJ
};

class Renderer
{
public:
    ~Renderer();

    bool Initialize(const Scene& scene, const std::string& shaderDirectory);
    void RebuildSlicedMeshes(int slices);
    void SetFaceNormalMode(bool faceNormals);
    void Render(const Scene& scene,
                const Camera& camera,
                bool textureMappingEnabled,
                bool wireframe,
                bool showNormals) const;
    int GetSlices() const { return currentSlices; }

private:
    struct RenderMesh
    {
        MeshKind kind = MeshKind::PLANE;
        std::string objPath;
        Mesh mesh;
        GLuint diffuseTexture = 0;
    };

    ShaderManager shaderManager;
    std::vector<RenderMesh> renderMeshes;

    GLuint mainProgram = 0;
    GLuint normalProgram = 0;
    GLuint defaultTexture = 0;

    GLint uModel = -1;
    GLint uView = -1;
    GLint uProjection = -1;
    GLint uNormalMatrix = -1;
    GLint uCameraPosition = -1;
    GLint uUseTexture = -1;
    GLint uDiffuseTexture = -1;
    GLint uShininess = -1;
    GLint uLightCount = -1;
    GLint uNormalMvp = -1;

    int currentSlices = 20;
    bool faceNormalsEnabled = false;

    void Destroy();
    void CacheUniformLocations();
    void BuildRenderMeshes(const Scene& scene);
    void UploadLights(const std::vector<Light>& lights) const;

    static MeshKind ResolveMeshKind(const std::string& meshId, std::string& objPath);
    static Mesh BuildMesh(MeshKind kind, const std::string& objPath, int slices);
    static glm::mat4 BuildModelMatrix(const SceneObject& object);
    static glm::vec3 NormalizeOrDefault(const glm::vec3& value, const glm::vec3& fallback);
    static GLuint CreateDefaultTexture();
    static GLuint LoadTexture(const std::string& path);
};
