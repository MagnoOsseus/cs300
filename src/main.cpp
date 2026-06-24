#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GL/gl.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// stb_image for texture loading (header-only).
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "OGLDebug.h"
#include "Camera.h"
#include "CS300Parser.h"
#include "Mesh.h"
#include "ShaderManager.h"
#include "animations.h"

// Window size.
static const GLsizei WIN_W = 1280;
static const GLsizei WIN_H = 720;
static const int kMaxLights = 8;
static const float kMinLightDirectionLength = 1e-6f;
static const float kAmbientBoost = 0.25f;
static const float kLightMarkerScale = 1.2f;

// Mesh type each scene object can use.
enum class MeshKind { PLANE, CUBE, CONE, CYLINDER, SPHERE, OBJ };

// Material data for shading.
struct Material
{
    float shininess = 10.0f;
    std::string diffuseTexture; // path to diffuse texture (empty = use fallback)
    std::string normalTexture;  // path to normal map (empty = no normal map)
};

// Scene object with transform and mesh.
struct SceneObject
{
    std::string  name;
    MeshKind     kind = MeshKind::PLANE;
    std::string  objPath;
    Mesh         mesh;

    glm::vec3    pos{ 0.0f };     // original position from scene file (never modified)
    glm::vec3    currPos{ 0.0f }; // animated position for this frame
    glm::vec3    rot{ 0.0f };
    glm::vec3    sca{ 1.0f };
    Material     material;

    GLuint       diffuseTex = 0;  // GPU diffuse texture (0 = use fallback)
    GLuint       normalTex  = 0;  // GPU normal map texture (0 = flat fallback)

    std::vector<Animations::Anim> anims; // animations attached to this object

    // Builds the model matrix using the animated position.
    glm::mat4 ModelMatrix() const
    {
        glm::mat4 T  = glm::translate(glm::mat4(1.0f), currPos);
        glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0));
        glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0));
        glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
        glm::mat4 S  = glm::scale(glm::mat4(1.0f), sca);
        return T * Rx * Ry * Rz * S;
    }
};

// Uniform locations for one light in the shader.
struct LightUniformLoc
{
    GLint type        = -1;
    GLint position    = -1;
    GLint direction   = -1;
    GLint color       = -1;
    GLint ambient     = -1;
    GLint attenuation = -1;
    GLint innerAngle  = -1;
    GLint outerAngle  = -1;
    GLint falloff     = -1;
};



// Creates a mesh from type and parameters.
static Mesh BuildMesh(MeshKind kind, const std::string & objPath, int slices)
{
    int rings = slices / 2;
    switch (kind)
    {
    case MeshKind::PLANE:    return Mesh::MakePlane();
    case MeshKind::CUBE:     return Mesh::MakeCube();
    case MeshKind::CONE:     return Mesh::MakeCone(slices);
    case MeshKind::CYLINDER: return Mesh::MakeCylinder(slices);
    case MeshKind::SPHERE:   return Mesh::MakeSphere(slices, rings);
    case MeshKind::OBJ:      return Mesh::LoadOBJ(objPath);
    }
    return Mesh::MakePlane();
}

// Loads a texture from a file; returns 0 on failure.
static GLuint LoadTextureFromFile(const std::string& path)
{
    int w, h, channels;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 0);
    if (!data)
    {
        std::cerr << "LoadTextureFromFile: cannot load '" << path << "'\n";
        return 0;
    }

    GLenum internalFmt = (channels == 4) ? GL_RGBA8 : GL_RGB8;
    GLenum fmt         = (channels == 4) ? GL_RGBA   : GL_RGB;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return tex;
}

// Creates a 1x1 flat normal map: (0,0,1) encoded as (127,127,255).
static GLuint CreateFlatNormalMap()
{
    const unsigned char flat[3] = { 127, 127, 255 };
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, flat);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Creates base OBJ files if missing in data/meshes.
static void EnsureMeshFiles()
{
    namespace fs = std::filesystem;
    fs::path dir("data/meshes");
    if (!fs::exists(dir))
    {
        fs::create_directories(dir);
    }

    auto saveIfMissing = [&](const char * name, Mesh m) {
        fs::path p = dir / name;
        if (!fs::exists(p))
        {
            std::cout << "Generating " << p.string() << " ...\n";
            m.SaveOBJ(p.string());
        }
    };

    saveIfMissing("plane.obj",            Mesh::MakePlane());
    saveIfMissing("cube_face.obj",        Mesh::MakeCube());
    saveIfMissing("cone_20_face.obj",     Mesh::MakeCone(20));
    saveIfMissing("cylinder_20_face.obj", Mesh::MakeCylinder(20));
    saveIfMissing("sphere_20_face.obj",   Mesh::MakeSphere(40, 20));
    saveIfMissing("suzanne.obj",          Mesh::MakeSphere(32, 16));
}

// Converts parser light type to shader integer.
static int ToShaderLightType(CS300Parser::LightType type)
{
    switch (type)
    {
    case CS300Parser::LightType::Directional: return 1;
    case CS300Parser::LightType::Spot:        return 2;
    case CS300Parser::LightType::Point:
    default:                                  return 0;
    }
}

// Creates a simple fallback texture.
static GLuint CreateFallbackTexture()
{
    const int texW = 128;
    const int texH = 128;
    const int gridSize = 6;
    std::vector<unsigned char> pixels(static_cast<size_t>(texW * texH * 3));

    // UV color palette for the fallback texture.
    const glm::vec3 palette[gridSize] = {
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 1.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 0.0f, 1.0f)
    };

    for (int y = 0; y < texH; ++y)
    {
        for (int x = 0; x < texW; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(texW - 1);
            float v = static_cast<float>(y) / static_cast<float>(texH - 1);
            size_t idx = static_cast<size_t>((y * texW + x) * 3);

            float normalizedU = u;
            float flippedV = 1.0f - v;
            int cellX = std::clamp(static_cast<int>(std::floor(normalizedU * static_cast<float>(gridSize))), 0, gridSize - 1);
            int cellY = std::clamp(static_cast<int>(std::floor(flippedV * static_cast<float>(gridSize))), 0, gridSize - 1);
            const glm::vec3 color = palette[(cellX + cellY) % gridSize];

            pixels[idx + 0] = static_cast<unsigned char>(color.r * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(color.g * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(color.b * 255.0f);
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGB8,
                 texW,
                 texH,
                 0,
                 GL_RGB,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Creates a tiny solid-white texture.
static GLuint CreateWhiteTexture()
{
    const unsigned char white[3] = { 255, 255, 255 };
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Compiles a shader from source text.
static GLuint CompileShaderFromSource(GLenum type, const char * source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE)
    {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(1, logLen)));
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        std::cerr << "Light marker shader compile error:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// Builds a tiny shader program for white point markers.
static GLuint CreateLightMarkerProgram()
{
    static const char * kMarkerVert = R"glsl(
#version 430 core
layout(location = 0) in vec3 aPos;
uniform mat4 uView;
uniform mat4 uProj;
void main()
{
    gl_Position = uProj * uView * vec4(aPos, 1.0);
    gl_PointSize = 10.0;
}
)glsl";

    static const char * kMarkerFrag = R"glsl(
#version 430 core
out vec4 fragColor;
void main()
{
    fragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
)glsl";

    GLuint vs = CompileShaderFromSource(GL_VERTEX_SHADER, kMarkerVert);
    if (vs == 0)
    {
        return 0;
    }

    GLuint fs = CompileShaderFromSource(GL_FRAGMENT_SHADER, kMarkerFrag);
    if (fs == 0)
    {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(1, logLen)));
        glGetProgramInfoLog(prog, logLen, nullptr, log.data());
        std::cerr << "Light marker program link error:\n" << log.data() << '\n';
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

// Entry point: initializes, loads scene, renders, and cleans up.
int main(int argc, char * argv[])
{
    // Scene file from argv, defaults to scene_A1.txt.
    const char * sceneFile = (argc > 1) ? argv[1] : "scene_A1.txt";

    // SDL initialization.
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }

    // OpenGL context setup.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // SDL window creation.
    SDL_Window * window = SDL_CreateWindow("CS300", WIN_W, WIN_H, SDL_WINDOW_OPENGL);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    // OpenGL context creation.
    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx)
    {
        std::cerr << "SDL_GL_CreateContext: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // GLEW initialization.
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "glewInit failed\n";
        SDL_GL_DestroyContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if _DEBUG
    // Routes OpenGL debug messages to callback.
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, nullptr);
#endif

    // Print basic GPU information.
    std::cout << "GL_VENDOR   : " << glGetString(GL_VENDOR) << '\n';
    std::cout << "GL_RENDERER : " << glGetString(GL_RENDERER) << '\n';
    std::cout << "GL_VERSION  : " << glGetString(GL_VERSION) << '\n';

    // Loads scene data from file.
    CS300Parser scene;
    scene.LoadDataFromFile(sceneFile);

    // Ensures mesh files exist.
    EnsureMeshFiles();

    // Configures the camera from scene parameters.
    Camera camera;
    camera.fovY = glm::radians(scene.fovy);
    camera.aspect = static_cast<float>(WIN_W) / static_cast<float>(WIN_H);
    camera.zNear = scene.nearPlane;
    camera.zFar = scene.farPlane;
    camera.InitFromLookAt(scene.camPos, scene.camTarget, scene.camUp);

    // Loads shader programs from files.
    ShaderManager shaderManager;
    if (!shaderManager.LoadProgram("main", "data/shaders/phong.vert", "data/shaders/phong.frag") ||
        !shaderManager.LoadProgram("normals", "data/shaders/normals.vert", "data/shaders/normals.frag"))
    {
        SDL_GL_DestroyContext(glCtx);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    GLuint mainProg = shaderManager.GetProgram("main");
    GLuint normProg = shaderManager.GetProgram("normals");

    // Gets uniform locations for shaders.
    GLint uModel = glGetUniformLocation(mainProg, "uModel");
    GLint uView = glGetUniformLocation(mainProg, "uView");
    GLint uProj = glGetUniformLocation(mainProg, "uProj");
    GLint uUseTexture = glGetUniformLocation(mainProg, "uUseTexture");
    GLint uDiffuseTexture = glGetUniformLocation(mainProg, "uDiffuseTexture");
    GLint uNormalMap = glGetUniformLocation(mainProg, "uNormalMap");
    GLint uHasNormalMap = glGetUniformLocation(mainProg, "uHasNormalMap");
    GLint uShininess = glGetUniformLocation(mainProg, "uShininess");
    GLint uAmbientBoost = glGetUniformLocation(mainProg, "uAmbientBoost");
    GLint uLightNum = glGetUniformLocation(mainProg, "uLightNum");

    std::array<LightUniformLoc, kMaxLights> lightUniforms{};
    for (int i = 0; i < kMaxLights; ++i)
    {
        const std::string prefix = "uLight[" + std::to_string(i) + "]";
        lightUniforms[i].type = glGetUniformLocation(mainProg, (prefix + ".type").c_str());
        lightUniforms[i].position = glGetUniformLocation(mainProg, (prefix + ".position").c_str());
        lightUniforms[i].direction = glGetUniformLocation(mainProg, (prefix + ".direction").c_str());
        lightUniforms[i].color = glGetUniformLocation(mainProg, (prefix + ".color").c_str());
        lightUniforms[i].ambient = glGetUniformLocation(mainProg, (prefix + ".ambient").c_str());
        lightUniforms[i].attenuation = glGetUniformLocation(mainProg, (prefix + ".attenuation").c_str());
        lightUniforms[i].innerAngle = glGetUniformLocation(mainProg, (prefix + ".innerAngle").c_str());
        lightUniforms[i].outerAngle = glGetUniformLocation(mainProg, (prefix + ".outerAngle").c_str());
        lightUniforms[i].falloff = glGetUniformLocation(mainProg, (prefix + ".falloff").c_str());
    }

    GLint uNormMVP = glGetUniformLocation(normProg, "uMVP");

    // Fallback and white textures.
    GLuint fallbackTex = CreateFallbackTexture();
    GLuint whiteTex = CreateWhiteTexture();
    // Flat normal map used for objects without a normal map texture.
    GLuint flatNormalMap = CreateFlatNormalMap();

    // Small sphere to mark light positions.
    Mesh lightMarkerMesh = Mesh::MakeSphere(16, 8);
    lightMarkerMesh.Upload(true);

    // Initial rendering toggles.
    bool showNormals = false;
    bool faceNormals = true;
    bool textureMode = false;
    bool wireframe = false;
    int currentSlices = 4;

    // Builds scene objects from loaded data.
    std::vector<SceneObject> objects;
    objects.reserve(scene.objects.size());

    for (const auto & so : scene.objects)
    {
        SceneObject obj;
        obj.name = so.name;
        obj.pos = so.pos;
        obj.rot = so.rot;
        obj.sca = so.sca;
        obj.material.shininess = so.ns;

        const std::string & ms = so.mesh;
        if (ms == "PLANE") obj.kind = MeshKind::PLANE;
        else if (ms == "CUBE") obj.kind = MeshKind::CUBE;
        else if (ms == "CONE") obj.kind = MeshKind::CONE;
        else if (ms == "CYLINDER") obj.kind = MeshKind::CYLINDER;
        else if (ms == "SPHERE") obj.kind = MeshKind::SPHERE;
        else { obj.kind = MeshKind::OBJ; obj.objPath = ms; }

        obj.mesh = BuildMesh(obj.kind, obj.objPath, currentSlices);
        obj.mesh.Upload(faceNormals);
        obj.anims = so.anims;
        obj.currPos = obj.pos; // start animated position at original

        // Load per-object textures (optional).
        if (!so.diffuseTexture.empty())
        {
            obj.diffuseTex = LoadTextureFromFile(so.diffuseTexture);
        }
        if (!so.normalTexture.empty())
        {
            obj.normalTex = LoadTextureFromFile(so.normalTexture);
        }

        objects.push_back(std::move(obj));
    }

    // Enables depth testing.
    glEnable(GL_DEPTH_TEST);

    // Previous tick for delta time.
    Uint64 prevTick = SDL_GetTicks();

    // Total time elapsed for animations.
    float elapsedTime = 0.0f;

    // Animated light positions (originals stay in scene.lights).
    std::vector<glm::vec3> lightCurrPos(scene.lights.size());
    for (size_t i = 0; i < scene.lights.size(); ++i)
    {
        lightCurrPos[i] = scene.lights[i].position;
    }

    // Main render loop.
    SDL_Event ev;
    bool quit = false;

    while (!quit)
    {
        // Delta time in seconds.
        Uint64 nowTick = SDL_GetTicks();
        float dt = static_cast<float>(nowTick - prevTick) * 0.001f;
        prevTick = nowTick;
        elapsedTime += dt;

        // Rebuilds meshes if the number of cuts changes.
        auto rebuildSlicedMeshes = [&]() {
            for (auto & o : objects)
            {
                if (o.kind == MeshKind::CONE || o.kind == MeshKind::CYLINDER || o.kind == MeshKind::SPHERE)
                {
                    o.mesh.Free();
                    o.mesh = BuildMesh(o.kind, o.objPath, currentSlices);
                    o.mesh.Upload(faceNormals);
                }
            }
            std::cout << "Slices: " << currentSlices << '\n';
        };

        // Processes SDL events.
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
            {
                quit = true;
            }

            if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                switch (ev.key.scancode)
                {
                case SDL_SCANCODE_ESCAPE:
                    quit = true;
                    break;

                case SDL_SCANCODE_N:
                    showNormals = !showNormals;
                    std::cout << "Normals: " << (showNormals ? "ON" : "OFF") << '\n';
                    break;

                case SDL_SCANCODE_F:
                    faceNormals = !faceNormals;
                    for (auto & o : objects)
                    {
                        o.mesh.SetNormalMode(faceNormals);
                    }
                    std::cout << "Normal mode: " << (faceNormals ? "FACE" : "AVERAGED") << '\n';
                    break;

                case SDL_SCANCODE_M:
                    wireframe = !wireframe;
                    std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << '\n';
                    break;

                case SDL_SCANCODE_T:
                    textureMode = !textureMode;
                    std::cout << "Texture mode: " << (textureMode ? "ON" : "OFF") << '\n';
                    break;

                case SDL_SCANCODE_EQUALS:
                case SDL_SCANCODE_KP_PLUS:
                case SDL_SCANCODE_Z:
                    currentSlices += 2;
                    rebuildSlicedMeshes();
                    break;

                case SDL_SCANCODE_MINUS:
                case SDL_SCANCODE_KP_MINUS:
                case SDL_SCANCODE_X:
                    currentSlices = std::max(4, currentSlices - 2);
                    rebuildSlicedMeshes();
                    break;

                default:
                    break;
                }
            }
        }

        // Updates camera.
        camera.ProcessInput(SDL_GetKeyboardState(nullptr), dt);

        // Update object animations from original position.
        for (auto & obj : objects)
        {
            obj.currPos = obj.pos;
            for (const auto & anim : obj.anims)
            {
                obj.currPos = anim.Update(obj.currPos, elapsedTime);
            }
        }

        // Update light animations from original position.
        for (size_t i = 0; i < scene.lights.size(); ++i)
        {
            lightCurrPos[i] = scene.lights[i].position;
            for (const auto & anim : scene.lights[i].anims)
            {
                lightCurrPos[i] = anim.Update(lightCurrPos[i], elapsedTime);
            }
        }

        // View and projection matrices.
        glm::mat4 V = camera.GetView();
        glm::mat4 P = camera.GetProjection();

        // Clears the screen and sets polygon mode.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        // Draws scene objects.
        glUseProgram(mainProg);
        glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(V));
        glUniformMatrix4fv(uProj, 1, GL_FALSE, glm::value_ptr(P));

        const int activeLightCount = std::min<int>(static_cast<int>(scene.lights.size()), kMaxLights);
        glUniform1i(uLightNum, activeLightCount);
        glUniform1f(uAmbientBoost, kAmbientBoost);

        // Upload lights transformed to camera/view space.
        glm::mat3 V3(V);
        for (int i = 0; i < activeLightCount; ++i)
        {
            const auto & light = scene.lights[static_cast<size_t>(i)];
            glm::vec3 lightDir = light.direction;
            if (glm::length(lightDir) < kMinLightDirectionLength)
            {
                lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
            }
            else
            {
                lightDir = glm::normalize(lightDir);
            }

            // Transform position and direction to camera space.
            glm::vec3 lightPosView = glm::vec3(V * glm::vec4(lightCurrPos[static_cast<size_t>(i)], 1.0f));
            glm::vec3 lightDirView = glm::normalize(V3 * lightDir);

            glUniform1i(lightUniforms[i].type, ToShaderLightType(light.type));
            glUniform3fv(lightUniforms[i].position, 1, glm::value_ptr(lightPosView));
            glUniform3fv(lightUniforms[i].direction, 1, glm::value_ptr(lightDirView));
            glUniform3fv(lightUniforms[i].color, 1, glm::value_ptr(light.color));
            glUniform1f(lightUniforms[i].ambient, light.ambient);
            glUniform3fv(lightUniforms[i].attenuation, 1, glm::value_ptr(light.attenuation));
            glUniform1f(lightUniforms[i].innerAngle, light.innerAngle);
            glUniform1f(lightUniforms[i].outerAngle, light.outerAngle);
            glUniform1f(lightUniforms[i].falloff, light.falloff);
        }

        // Set texture sampler units.
        glUniform1i(uDiffuseTexture, 0);
        glUniform1i(uNormalMap, 1);

        for (const auto & obj : objects)
        {
            if (!obj.mesh.IsValid())
            {
                continue;
            }

            glm::mat4 M = obj.ModelMatrix();
            glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(M));
            glUniform1i(uUseTexture, textureMode ? 1 : 0);
            glUniform1f(uShininess, obj.material.shininess);

            // Bind diffuse texture (object-specific or global fallback).
            glActiveTexture(GL_TEXTURE0);
            if (textureMode && obj.diffuseTex != 0)
                glBindTexture(GL_TEXTURE_2D, obj.diffuseTex);
            else
                glBindTexture(GL_TEXTURE_2D, fallbackTex);

            // Bind normal map (object-specific or flat fallback).
            glActiveTexture(GL_TEXTURE1);
            if (obj.normalTex != 0)
            {
                glBindTexture(GL_TEXTURE_2D, obj.normalTex);
                glUniform1i(uHasNormalMap, 1);
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, flatNormalMap);
                glUniform1i(uHasNormalMap, 0);
            }

            obj.mesh.Draw();
        }

        // Draws normals if enabled.
        if (showNormals)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUseProgram(normProg);

            for (const auto & obj : objects)
            {
                if (!obj.mesh.HasNormals())
                {
                    continue;
                }

                glm::mat4 MVP = P * V * obj.ModelMatrix();
                glUniformMatrix4fv(uNormMVP, 1, GL_FALSE, glm::value_ptr(MVP));
                obj.mesh.DrawNormals();
            }
        }

        // Draws small white spheres at light positions.
        if (activeLightCount > 0 && lightMarkerMesh.IsValid())
        {
            glUseProgram(mainProg);
            glUniformMatrix4fv(uView, 1, GL_FALSE, glm::value_ptr(V));
            glUniformMatrix4fv(uProj, 1, GL_FALSE, glm::value_ptr(P));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, whiteTex);
            glUniform1i(uDiffuseTexture, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, flatNormalMap);
            glUniform1i(uNormalMap, 1);
            glUniform1i(uHasNormalMap, 0);
            glUniform1i(uUseTexture, 1);
            glUniform1f(uShininess, 64.0f);

            // Bright point at camera origin in view space so markers are always visible.
            glUniform1i(uLightNum, 1);
            glUniform1f(uAmbientBoost, 1.0f);
            glUniform1i(lightUniforms[0].type, 0);
            glUniform3fv(lightUniforms[0].position, 1, glm::value_ptr(glm::vec3(0.0f)));
            glUniform3fv(lightUniforms[0].direction, 1, glm::value_ptr(glm::vec3(0.0f, -1.0f, 0.0f)));
            glUniform3fv(lightUniforms[0].color, 1, glm::value_ptr(glm::vec3(1.0f)));
            glUniform1f(lightUniforms[0].ambient, 1.0f);
            glUniform3fv(lightUniforms[0].attenuation, 1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f)));
            glUniform1f(lightUniforms[0].innerAngle, 0.0f);
            glUniform1f(lightUniforms[0].outerAngle, 180.0f);
            glUniform1f(lightUniforms[0].falloff, 1.0f);

            for (int i = 0; i < activeLightCount; ++i)
            {
                const glm::vec3 markerPos = lightCurrPos[static_cast<size_t>(i)];
                glm::mat4 markerModel = glm::translate(glm::mat4(1.0f), markerPos);
                markerModel = glm::scale(markerModel, glm::vec3(kLightMarkerScale));
                glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(markerModel));
                lightMarkerMesh.Draw();
            }

            // Restore scene lights.
            glUniform1i(uLightNum, activeLightCount);
            glUniform1f(uAmbientBoost, kAmbientBoost);
        }

        // Presents the rendered frame.
        glUseProgram(0);
        SDL_GL_SwapWindow(window);
    }

    // Releases resources and shuts down SDL.
    for (auto & o : objects)
    {
        o.mesh.Free();
        if (o.diffuseTex != 0) glDeleteTextures(1, &o.diffuseTex);
        if (o.normalTex  != 0) glDeleteTextures(1, &o.normalTex);
    }
    glDeleteTextures(1, &fallbackTex);
    glDeleteTextures(1, &whiteTex);
    glDeleteTextures(1, &flatNormalMap);
    lightMarkerMesh.Free();

    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
