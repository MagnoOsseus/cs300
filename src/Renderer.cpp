#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace
{
constexpr int kMaxLights = 8;
constexpr int kMinSlices = 4;
constexpr int kMinRings = 2;
constexpr float kLengthEpsilonSquared = 1.0e-6f;
}

Renderer::~Renderer()
{
    Destroy();
}

bool Renderer::Initialize(const Scene& scene, const std::string& shaderDirectory)
{
    Destroy();

    if (!shaderManager.LoadProgram("phong",
                                   shaderDirectory + "/phong.vert",
                                   shaderDirectory + "/phong.frag"))
    {
        return false;
    }

    if (!shaderManager.LoadProgram("normals",
                                   shaderDirectory + "/normals.vert",
                                   shaderDirectory + "/normals.frag"))
    {
        return false;
    }

    mainProgram = shaderManager.GetProgram("phong");
    normalProgram = shaderManager.GetProgram("normals");
    defaultTexture = CreateDefaultTexture();
    CacheUniformLocations();
    BuildRenderMeshes(scene);
    return mainProgram != 0 && normalProgram != 0 && defaultTexture != 0;
}

void Renderer::RebuildSlicedMeshes(int slices)
{
    currentSlices = std::max(kMinSlices, slices);

    for (auto& renderMesh : renderMeshes)
    {
        if (renderMesh.kind == MeshKind::CONE ||
            renderMesh.kind == MeshKind::CYLINDER ||
            renderMesh.kind == MeshKind::SPHERE)
        {
            renderMesh.mesh.Free();
            renderMesh.mesh = BuildMesh(renderMesh.kind, renderMesh.objPath, currentSlices);
            renderMesh.mesh.Upload(faceNormalsEnabled);
        }
    }
}

void Renderer::SetFaceNormalMode(bool faceNormals)
{
    faceNormalsEnabled = faceNormals;
    for (auto& renderMesh : renderMeshes)
    {
        renderMesh.mesh.SetNormalMode(faceNormalsEnabled);
    }
}

void Renderer::Render(const Scene& scene,
                      const Camera& camera,
                      bool textureMappingEnabled,
                      bool wireframe,
                      bool showNormals) const
{
    const glm::mat4 view = camera.GetView();
    const glm::mat4 projection = camera.GetProjection();
    const glm::mat4 viewProjection = projection * view;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

    glUseProgram(mainProgram);
    glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(uProjection, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(uCameraPosition, 1, glm::value_ptr(camera.GetPosition()));
    glUniform1i(uUseTexture, textureMappingEnabled ? 1 : 0);
    glUniform1i(uDiffuseTexture, 0);
    UploadLights(scene.lights);

    glActiveTexture(GL_TEXTURE0);

    const size_t renderCount = std::min(scene.objects.size(), renderMeshes.size());
    for (size_t i = 0; i < renderCount; ++i)
    {
        const SceneObject& object = scene.objects[i];
        const RenderMesh& renderMesh = renderMeshes[i];

        if (!renderMesh.mesh.IsValid())
        {
            continue;
        }

        const glm::mat4 model = BuildModelMatrix(object);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));

        glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix3fv(uNormalMatrix, 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform1f(uShininess, object.material.shininess);
        glBindTexture(GL_TEXTURE_2D, renderMesh.diffuseTexture != 0 ? renderMesh.diffuseTexture : defaultTexture);
        renderMesh.mesh.Draw();
    }

    if (showNormals)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUseProgram(normalProgram);

        for (size_t i = 0; i < renderCount; ++i)
        {
            const SceneObject& object = scene.objects[i];
            const RenderMesh& renderMesh = renderMeshes[i];

            if (!renderMesh.mesh.HasNormals())
            {
                continue;
            }

            const glm::mat4 mvp = viewProjection * BuildModelMatrix(object);
            glUniformMatrix4fv(uNormalMvp, 1, GL_FALSE, glm::value_ptr(mvp));
            renderMesh.mesh.DrawNormals();
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void Renderer::Destroy()
{
    for (auto& renderMesh : renderMeshes)
    {
        renderMesh.mesh.Free();
        if (renderMesh.diffuseTexture != 0 && renderMesh.diffuseTexture != defaultTexture)
        {
            glDeleteTextures(1, &renderMesh.diffuseTexture);
            renderMesh.diffuseTexture = 0;
        }
    }

    renderMeshes.clear();

    if (defaultTexture != 0)
    {
        glDeleteTextures(1, &defaultTexture);
        defaultTexture = 0;
    }

    shaderManager.Clear();
    mainProgram = 0;
    normalProgram = 0;
}

void Renderer::CacheUniformLocations()
{
    uModel = glGetUniformLocation(mainProgram, "uModel");
    uView = glGetUniformLocation(mainProgram, "uView");
    uProjection = glGetUniformLocation(mainProgram, "uProjection");
    uNormalMatrix = glGetUniformLocation(mainProgram, "uNormalMatrix");
    uCameraPosition = glGetUniformLocation(mainProgram, "uCameraPosition");
    uUseTexture = glGetUniformLocation(mainProgram, "uUseTexture");
    uDiffuseTexture = glGetUniformLocation(mainProgram, "uDiffuseTexture");
    uShininess = glGetUniformLocation(mainProgram, "uShininess");
    uLightCount = glGetUniformLocation(mainProgram, "uLightCount");
    uNormalMvp = glGetUniformLocation(normalProgram, "uMVP");
}

void Renderer::BuildRenderMeshes(const Scene& scene)
{
    renderMeshes.reserve(scene.objects.size());

    for (const auto& object : scene.objects)
    {
        RenderMesh renderMesh;
        renderMesh.kind = ResolveMeshKind(object.mesh, renderMesh.objPath);
        renderMesh.mesh = BuildMesh(renderMesh.kind, renderMesh.objPath, currentSlices);
        renderMesh.mesh.Upload(faceNormalsEnabled);
        renderMesh.diffuseTexture = object.diffuseTexture.empty() ? defaultTexture : LoadTexture(object.diffuseTexture);
        if (renderMesh.diffuseTexture == 0)
        {
            renderMesh.diffuseTexture = defaultTexture;
        }
        renderMeshes.push_back(std::move(renderMesh));
    }
}

void Renderer::UploadLights(const std::vector<Light>& lights) const
{
    const int lightCount = std::min<int>(static_cast<int>(lights.size()), kMaxLights);
    glUniform1i(uLightCount, lightCount);

    for (int i = 0; i < lightCount; ++i)
    {
        const Light& light = lights[static_cast<size_t>(i)];
        const std::string prefix = "uLights[" + std::to_string(i) + "]";

        glUniform1i(glGetUniformLocation(mainProgram, (prefix + ".type").c_str()), static_cast<int>(light.type));
        glUniform3fv(glGetUniformLocation(mainProgram, (prefix + ".position").c_str()), 1,
                     glm::value_ptr(light.currentPosition));

        const glm::vec3 direction = NormalizeOrDefault(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
        glUniform3fv(glGetUniformLocation(mainProgram, (prefix + ".direction").c_str()), 1,
                     glm::value_ptr(direction));
        glUniform3fv(glGetUniformLocation(mainProgram, (prefix + ".color").c_str()), 1,
                     glm::value_ptr(light.color));
        glUniform1f(glGetUniformLocation(mainProgram, (prefix + ".ambient").c_str()),
                    light.ambientCoefficient);

        const glm::vec3 attenuation(light.attenuation.constant,
                                    light.attenuation.linear,
                                    light.attenuation.quadratic);
        glUniform3fv(glGetUniformLocation(mainProgram, (prefix + ".attenuation").c_str()), 1,
                     glm::value_ptr(attenuation));

        const float innerCos = std::cos(glm::radians(light.spotlight.innerAngle));
        const float outerCos = std::cos(glm::radians(light.spotlight.outerAngle));
        glUniform1f(glGetUniformLocation(mainProgram, (prefix + ".innerCos").c_str()), innerCos);
        glUniform1f(glGetUniformLocation(mainProgram, (prefix + ".outerCos").c_str()), outerCos);
        glUniform1f(glGetUniformLocation(mainProgram, (prefix + ".falloff").c_str()),
                    light.spotlight.falloff);
    }
}

MeshKind Renderer::ResolveMeshKind(const std::string& meshId, std::string& objPath)
{
    if (meshId == "PLANE")
    {
        return MeshKind::PLANE;
    }

    if (meshId == "CUBE")
    {
        return MeshKind::CUBE;
    }

    if (meshId == "CONE")
    {
        return MeshKind::CONE;
    }

    if (meshId == "CYLINDER")
    {
        return MeshKind::CYLINDER;
    }

    if (meshId == "SPHERE")
    {
        return MeshKind::SPHERE;
    }

    objPath = meshId;
    return MeshKind::OBJ;
}

Mesh Renderer::BuildMesh(MeshKind kind, const std::string& objPath, int slices)
{
    const int rings = std::max(kMinRings, slices / 2);

    switch (kind)
    {
    case MeshKind::PLANE:
        return Mesh::MakePlane();
    case MeshKind::CUBE:
        return Mesh::MakeCube();
    case MeshKind::CONE:
        return Mesh::MakeCone(slices);
    case MeshKind::CYLINDER:
        return Mesh::MakeCylinder(slices);
    case MeshKind::SPHERE:
        return Mesh::MakeSphere(slices, rings);
    case MeshKind::OBJ:
        return Mesh::LoadOBJ(objPath);
    }

    return Mesh::MakePlane();
}

glm::mat4 Renderer::BuildModelMatrix(const SceneObject& object)
{
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), object.currentPosition);
    const glm::mat4 rotationX = glm::rotate(glm::mat4(1.0f), glm::radians(object.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(object.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 rotationZ = glm::rotate(glm::mat4(1.0f), glm::radians(object.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), object.scale);
    return translation * rotationX * rotationY * rotationZ * scale;
}

glm::vec3 Renderer::NormalizeOrDefault(const glm::vec3& value, const glm::vec3& fallback)
{
    const float lengthSquared = glm::dot(value, value);
    if (lengthSquared <= kLengthEpsilonSquared)
    {
        return fallback;
    }

    return glm::normalize(value);
}

GLuint Renderer::CreateDefaultTexture()
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    const unsigned char whitePixel[] = { 255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 1,
                 1,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 whitePixel);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

GLuint Renderer::LoadTexture(const std::string& path)
{
    SDL_Surface* surface = SDL_LoadBMP(path.c_str());
    if (surface == nullptr)
    {
        std::cerr << "Failed to load texture '" << path << "': " << SDL_GetError() << '\n';
        return 0;
    }

    SDL_Surface* convertedSurface = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);

    if (convertedSurface == nullptr)
    {
        std::cerr << "Failed to convert texture '" << path << "': " << SDL_GetError() << '\n';
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 convertedSurface->w,
                 convertedSurface->h,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 convertedSurface->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    SDL_DestroySurface(convertedSurface);
    return texture;
}
