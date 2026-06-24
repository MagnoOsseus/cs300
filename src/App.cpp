#include "App.h"
#include "OGLDebug.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

// Window dimensions.
static const GLsizei WIN_W = 1280;
static const GLsizei WIN_H = 720;

static const float kMinLightDirectionLength = 1e-6f;
static const float kAmbientBoost            = 0.25f;
static const float kLightMarkerScale        = 1.2f;

// ----- SceneObject -------------------------------------------------------

glm::mat4 SceneObject::ModelMatrix() const
{
    glm::mat4 T  = glm::translate(glm::mat4(1.0f), currPos);
    glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0));
    glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0));
    glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
    glm::mat4 S  = glm::scale(glm::mat4(1.0f), sca);
    return T * Rx * Ry * Rz * S;
}

// ----- Helper free functions ---------------------------------------------

// Creates a mesh from its type.
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

// Creates OBJ files in data/meshes if they are missing.
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

// Creates a simple UV-palette fallback texture.
static GLuint CreateFallbackTexture()
{
    const int texW = 128;
    const int texH = 128;
    const int gridSize = 6;
    std::vector<unsigned char> pixels(static_cast<size_t>(texW * texH * 3));

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

            int cellX = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(gridSize))), 0, gridSize - 1);
            int cellY = std::clamp(static_cast<int>(std::floor((1.0f - v) * static_cast<float>(gridSize))), 0, gridSize - 1);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, texW, texH, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
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

// Creates a tiny default normal map texture (tangent-space flat normal).
static GLuint CreateDefaultNormalTexture()
{
    const unsigned char normal[3] = { 128, 128, 255 };
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, normal);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

// Resolves a texture path to an existing file, checking a remap directory.
static std::string ResolveTexturePath(const std::string& path)
{
    namespace fs = std::filesystem;
    static constexpr const char* kSceneTexturePrefix = "data/textures/";
    if (path.empty())
    {
        return std::string();
    }
    if (fs::exists(path))
    {
        return path;
    }
    if (path.rfind(kSceneTexturePrefix, 0) == 0)
    {
        fs::path remap = fs::path("data/normal_maps/textures") / path.substr(std::string(kSceneTexturePrefix).size());
        if (fs::exists(remap))
        {
            return remap.string();
        }
    }
    return path;
}

#ifdef _WIN32
static bool WicLoadRGBA(const std::string& path, std::vector<unsigned char>& pixels, int& width, int& height)
{
    using Microsoft::WRL::ComPtr;

    const int utf16Len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (utf16Len <= 0)
    {
        return false;
    }
    std::vector<wchar_t> widePath(static_cast<size_t>(utf16Len));
    if (MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, widePath.data(), utf16Len) <= 0)
    {
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
    {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(widePath.data(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
    {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)))
    {
        return false;
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        return false;
    }

    UINT w = 0;
    UINT h = 0;
    if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0)
    {
        return false;
    }

    width  = static_cast<int>(w);
    height = static_cast<int>(h);
    pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    const UINT stride   = w * 4u;
    const UINT byteSize = stride * h;
    return SUCCEEDED(converter->CopyPixels(nullptr, stride, byteSize, pixels.data()));
}
#endif

// Loads an image file into an OpenGL texture.
static GLuint LoadTexture2D(const std::string& sourcePath)
{
    const std::string path = ResolveTexturePath(sourcePath);
    int width  = 0;
    int height = 0;
    std::vector<unsigned char> pixels;

#ifdef _WIN32
    static bool comInitialized = false;
    if (!comInitialized)
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    if (!comInitialized || !WicLoadRGBA(path, pixels, width, height))
    {
        std::cerr << "Failed to load texture: " << path << '\n';
        return 0;
    }
#else
    std::cerr << "Texture loading is only implemented for Windows in this build: " << path << '\n';
    return 0;
#endif

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
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
        std::cerr << "Shader compile error:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// Builds a simple shader program used to draw white light markers.
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

// ----- App implementation ------------------------------------------------

bool App::Init(const char* sceneFile)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return false;
    }

    // OpenGL context attributes.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    m_window = SDL_CreateWindow("CS300", WIN_W, WIN_H, SDL_WINDOW_OPENGL);
    if (!m_window)
    {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return false;
    }

    m_glCtx = SDL_GL_CreateContext(m_window);
    if (!m_glCtx)
    {
        std::cerr << "SDL_GL_CreateContext: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "glewInit failed\n";
        SDL_GL_DestroyContext(m_glCtx);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

#if _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, nullptr);
#endif

    std::cout << "GL_VENDOR   : " << glGetString(GL_VENDOR)   << '\n';
    std::cout << "GL_RENDERER : " << glGetString(GL_RENDERER) << '\n';
    std::cout << "GL_VERSION  : " << glGetString(GL_VERSION)  << '\n';

    // Load scene data and set up all GPU resources.
    m_scene.LoadDataFromFile(sceneFile);
    EnsureMeshFiles();

    m_camera.fovY   = glm::radians(m_scene.fovy);
    m_camera.aspect = static_cast<float>(WIN_W) / static_cast<float>(WIN_H);
    m_camera.zNear  = m_scene.nearPlane;
    m_camera.zFar   = m_scene.farPlane;
    m_camera.InitFromLookAt(m_scene.camPos, m_scene.camTarget, m_scene.camUp);

    SetupShaders();
    LoadScene(sceneFile);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    return true;
}

// Loads all shader programs and queries uniform locations.
void App::SetupShaders()
{
    if (!m_shaderManager.LoadProgram("main",    "data/shaders/phong.vert",   "data/shaders/phong.frag") ||
        !m_shaderManager.LoadProgram("normals", "data/shaders/normals.vert", "data/shaders/normals.frag"))
    {
        std::cerr << "Failed to load shader programs.\n";
        return;
    }

    m_mainProg = m_shaderManager.GetProgram("main");
    m_normProg = m_shaderManager.GetProgram("normals");

    m_uModel        = glGetUniformLocation(m_mainProg, "uModel");
    m_uView         = glGetUniformLocation(m_mainProg, "uView");
    m_uProj         = glGetUniformLocation(m_mainProg, "uProj");
    m_uUseTexture   = glGetUniformLocation(m_mainProg, "uUseTexture");
    m_uDiffuseTex   = glGetUniformLocation(m_mainProg, "uDiffuseTexture");
    m_uUseNormalMap = glGetUniformLocation(m_mainProg, "uUseNormalMap");
    m_uNormalTex    = glGetUniformLocation(m_mainProg, "uNormalTexture");
    m_uShininess    = glGetUniformLocation(m_mainProg, "uShininess");
    m_uAmbientBoost = glGetUniformLocation(m_mainProg, "uAmbientBoost");
    m_uLightNum     = glGetUniformLocation(m_mainProg, "uLightNum");

    for (int i = 0; i < kMaxLights; ++i)
    {
        const std::string p = "uLight[" + std::to_string(i) + "]";
        m_lightUniforms[i].type          = glGetUniformLocation(m_mainProg, (p + ".type").c_str());
        m_lightUniforms[i].position      = glGetUniformLocation(m_mainProg, (p + ".position").c_str());
        m_lightUniforms[i].direction     = glGetUniformLocation(m_mainProg, (p + ".direction").c_str());
        m_lightUniforms[i].color         = glGetUniformLocation(m_mainProg, (p + ".color").c_str());
        m_lightUniforms[i].ambient       = glGetUniformLocation(m_mainProg, (p + ".ambient").c_str());
        m_lightUniforms[i].attenuation   = glGetUniformLocation(m_mainProg, (p + ".attenuation").c_str());
        m_lightUniforms[i].innerAngleCos = glGetUniformLocation(m_mainProg, (p + ".innerAngleCos").c_str());
        m_lightUniforms[i].outerAngleCos = glGetUniformLocation(m_mainProg, (p + ".outerAngleCos").c_str());
        m_lightUniforms[i].falloff       = glGetUniformLocation(m_mainProg, (p + ".falloff").c_str());
    }

    m_uNormMVP = glGetUniformLocation(m_normProg, "uMVP");
}

// Builds SceneObject list and uploads textures and meshes to the GPU.
void App::LoadScene(const char*)
{
    m_fallbackTex      = CreateFallbackTexture();
    m_whiteTex         = CreateWhiteTexture();
    m_defaultNormalTex = CreateDefaultNormalTexture();

    m_lightMarkerMesh = Mesh::MakeSphere(16, 8);
    m_lightMarkerMesh.Upload(true);

    m_objects.reserve(m_scene.objects.size());

    for (const auto & so : m_scene.objects)
    {
        SceneObject obj;
        obj.name                      = so.name;
        obj.pos                       = so.pos;
        obj.rot                       = so.rot;
        obj.sca                       = so.sca;
        obj.material.shininess        = so.ns;
        obj.material.diffuseTexturePath = so.diffuseTexture;
        obj.material.normalTexturePath  = so.normalTexture;
        obj.material.diffuseTexture   = m_fallbackTex;
        obj.material.normalTexture    = m_defaultNormalTex;
        obj.material.hasNormalMap     = false;

        const std::string & ms = so.mesh;
        if      (ms == "PLANE")    obj.kind = MeshKind::PLANE;
        else if (ms == "CUBE")     obj.kind = MeshKind::CUBE;
        else if (ms == "CONE")     obj.kind = MeshKind::CONE;
        else if (ms == "CYLINDER") obj.kind = MeshKind::CYLINDER;
        else if (ms == "SPHERE")   obj.kind = MeshKind::SPHERE;
        else { obj.kind = MeshKind::OBJ; obj.objPath = ms; }

        obj.mesh = BuildMesh(obj.kind, obj.objPath, m_currentSlices);
        obj.mesh.Upload(m_faceNormals);
        obj.anims  = so.anims;
        obj.currPos = obj.pos;

        // Load diffuse texture.
        if (!obj.material.diffuseTexturePath.empty())
        {
            const std::string diffusePath = ResolveTexturePath(obj.material.diffuseTexturePath);
            auto it = m_textureCache.find(diffusePath);
            if (it == m_textureCache.end())
            {
                GLuint tex = LoadTexture2D(diffusePath);
                if (tex != 0)
                {
                    m_textureCache[diffusePath]   = tex;
                    obj.material.diffuseTexture   = tex;
                }
            }
            else
            {
                obj.material.diffuseTexture = it->second;
            }
        }

        // Load normal map texture.
        if (!obj.material.normalTexturePath.empty())
        {
            const std::string normalPath = ResolveTexturePath(obj.material.normalTexturePath);
            auto it = m_textureCache.find(normalPath);
            if (it == m_textureCache.end())
            {
                GLuint tex = LoadTexture2D(normalPath);
                if (tex != 0)
                {
                    m_textureCache[normalPath]  = tex;
                    obj.material.normalTexture  = tex;
                    obj.material.hasNormalMap   = true;
                }
            }
            else
            {
                obj.material.normalTexture = it->second;
                obj.material.hasNormalMap  = true;
            }
        }

        m_objects.push_back(std::move(obj));
    }

    // Initial animated positions for lights.
    m_lightCurrPos.resize(m_scene.lights.size());
    for (size_t i = 0; i < m_scene.lights.size(); ++i)
    {
        m_lightCurrPos[i] = m_scene.lights[i].position;
    }
}

// Rebuilds meshes that depend on the current slice count.
void App::RebuildSlicedMeshes()
{
    for (auto & o : m_objects)
    {
        if (o.kind == MeshKind::CONE || o.kind == MeshKind::CYLINDER || o.kind == MeshKind::SPHERE)
        {
            o.mesh.Free();
            o.mesh = BuildMesh(o.kind, o.objPath, m_currentSlices);
            o.mesh.Upload(m_faceNormals);
        }
    }
    std::cout << "Slices: " << m_currentSlices << '\n';
}

// Updates object and light positions from their animations.
void App::UpdateAnimations(float elapsedTime)
{
    for (auto & obj : m_objects)
    {
        obj.currPos = obj.pos;
        for (const auto & anim : obj.anims)
        {
            obj.currPos = anim.Update(obj.currPos, elapsedTime);
        }
    }

    for (size_t i = 0; i < m_scene.lights.size(); ++i)
    {
        m_lightCurrPos[i] = m_scene.lights[i].position;
        for (const auto & anim : m_scene.lights[i].anims)
        {
            m_lightCurrPos[i] = anim.Update(m_lightCurrPos[i], elapsedTime);
        }
    }
}

// Processes SDL events for the current frame.
void App::HandleEvents(float /*dt*/, bool& quit)
{
    SDL_Event ev;
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
                m_showNormals = !m_showNormals;
                std::cout << "Normals: " << (m_showNormals ? "ON" : "OFF") << '\n';
                break;

            case SDL_SCANCODE_F:
                m_faceNormals = !m_faceNormals;
                for (auto & o : m_objects)
                {
                    o.mesh.SetNormalMode(m_faceNormals);
                }
                std::cout << "Normal mode: " << (m_faceNormals ? "FACE" : "AVERAGED") << '\n';
                break;

            case SDL_SCANCODE_M:
                m_wireframe = !m_wireframe;
                std::cout << "Wireframe: " << (m_wireframe ? "ON" : "OFF") << '\n';
                break;

            case SDL_SCANCODE_T:
                m_textureMode = !m_textureMode;
                std::cout << "Texture mode: " << (m_textureMode ? "ON" : "OFF") << '\n';
                break;

            case SDL_SCANCODE_EQUALS:
            case SDL_SCANCODE_KP_PLUS:
            case SDL_SCANCODE_Z:
                m_currentSlices += 2;
                RebuildSlicedMeshes();
                break;

            case SDL_SCANCODE_MINUS:
            case SDL_SCANCODE_KP_MINUS:
            case SDL_SCANCODE_X:
                m_currentSlices = std::max(4, m_currentSlices - 2);
                RebuildSlicedMeshes();
                break;

            default:
                break;
            }
        }
    }
}

// Draws scene objects, optional normals, and light markers.
void App::RenderFrame()
{
    const glm::mat4 V = m_camera.GetView();
    const glm::mat4 P = m_camera.GetProjection();
    const glm::mat3 viewRotation = glm::mat3(V);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, m_wireframe ? GL_LINE : GL_FILL);

    glUseProgram(m_mainProg);
    glUniformMatrix4fv(m_uView, 1, GL_FALSE, glm::value_ptr(V));
    glUniformMatrix4fv(m_uProj, 1, GL_FALSE, glm::value_ptr(P));

    const int activeLightCount = std::min<int>(static_cast<int>(m_scene.lights.size()), kMaxLights);
    glUniform1i(m_uLightNum, activeLightCount);
    glUniform1f(m_uAmbientBoost, kAmbientBoost);

    // Upload per-light uniforms, using precomputed cosine values for spot angles.
    for (int i = 0; i < activeLightCount; ++i)
    {
        const auto & light = m_scene.lights[static_cast<size_t>(i)];

        glm::vec3 lightDir = light.direction;
        if (glm::length(lightDir) < kMinLightDirectionLength)
        {
            lightDir = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        else
        {
            lightDir = glm::normalize(lightDir);
        }

        const glm::vec3 viewLightPos = glm::vec3(V * glm::vec4(m_lightCurrPos[static_cast<size_t>(i)], 1.0f));
        const glm::vec3 viewLightDir = glm::normalize(viewRotation * lightDir);

        glUniform1i(m_lightUniforms[i].type,           ToShaderLightType(light.type));
        glUniform3fv(m_lightUniforms[i].position,    1, glm::value_ptr(viewLightPos));
        glUniform3fv(m_lightUniforms[i].direction,   1, glm::value_ptr(viewLightDir));
        glUniform3fv(m_lightUniforms[i].color,       1, glm::value_ptr(light.color));
        glUniform1f(m_lightUniforms[i].ambient,        light.ambient);
        glUniform3fv(m_lightUniforms[i].attenuation, 1, glm::value_ptr(light.attenuation));
        glUniform1f(m_lightUniforms[i].innerAngleCos,  light.innerAngleCos);
        glUniform1f(m_lightUniforms[i].outerAngleCos,  light.outerAngleCos);
        glUniform1f(m_lightUniforms[i].falloff,        light.falloff);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fallbackTex);
    glUniform1i(m_uDiffuseTex, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_defaultNormalTex);
    glUniform1i(m_uNormalTex, 1);

    // Draw scene objects.
    for (const auto & obj : m_objects)
    {
        if (!obj.mesh.IsValid())
        {
            continue;
        }

        glm::mat4 M = obj.ModelMatrix();
        glUniformMatrix4fv(m_uModel, 1, GL_FALSE, glm::value_ptr(M));
        glUniform1i(m_uUseTexture,   m_textureMode ? 1 : 0);
        glUniform1i(m_uUseNormalMap, obj.material.hasNormalMap ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, obj.material.diffuseTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, obj.material.normalTexture);
        glUniform1f(m_uShininess, obj.material.shininess);
        obj.mesh.Draw();
    }

    // Draw normals pass if enabled.
    if (m_showNormals)
    {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glUseProgram(m_normProg);

        for (const auto & obj : m_objects)
        {
            if (!obj.mesh.HasNormals())
            {
                continue;
            }

            glm::mat4 MVP = P * V * obj.ModelMatrix();
            glUniformMatrix4fv(m_uNormMVP, 1, GL_FALSE, glm::value_ptr(MVP));
            obj.mesh.DrawNormals();
        }
    }

    // Draw small spheres at each light position.
    if (activeLightCount > 0 && m_lightMarkerMesh.IsValid())
    {
        glUseProgram(m_mainProg);
        glUniformMatrix4fv(m_uView, 1, GL_FALSE, glm::value_ptr(V));
        glUniformMatrix4fv(m_uProj, 1, GL_FALSE, glm::value_ptr(P));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_whiteTex);
        glUniform1i(m_uDiffuseTex, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_defaultNormalTex);
        glUniform1i(m_uNormalTex, 1);
        glUniform1i(m_uUseTexture,   1);
        glUniform1i(m_uUseNormalMap, 0);
        glUniform1f(m_uShininess,    64.0f);

        // Use a virtual bright point light at the camera so markers are always lit.
        glUniform1i(m_uLightNum,    1);
        glUniform1f(m_uAmbientBoost, 1.0f);
        glUniform1i(m_lightUniforms[0].type,          0);
        glUniform3fv(m_lightUniforms[0].position,   1, glm::value_ptr(glm::vec3(0.0f)));
        glUniform3fv(m_lightUniforms[0].direction,  1, glm::value_ptr(glm::vec3(0.0f, -1.0f, 0.0f)));
        glUniform3fv(m_lightUniforms[0].color,      1, glm::value_ptr(glm::vec3(1.0f)));
        glUniform1f(m_lightUniforms[0].ambient,       1.0f);
        glUniform3fv(m_lightUniforms[0].attenuation,1, glm::value_ptr(glm::vec3(1.0f, 0.0f, 0.0f)));
        glUniform1f(m_lightUniforms[0].innerAngleCos, 1.0f);  // cos(0 deg) = 1
        glUniform1f(m_lightUniforms[0].outerAngleCos,-1.0f);  // cos(180 deg) = -1
        glUniform1f(m_lightUniforms[0].falloff,       1.0f);

        for (int i = 0; i < activeLightCount; ++i)
        {
            const glm::vec3 markerPos = m_lightCurrPos[static_cast<size_t>(i)];
            glm::mat4 markerModel = glm::translate(glm::mat4(1.0f), markerPos);
            markerModel = glm::scale(markerModel, glm::vec3(kLightMarkerScale));
            glUniformMatrix4fv(m_uModel, 1, GL_FALSE, glm::value_ptr(markerModel));
            m_lightMarkerMesh.Draw();
        }

        // Restore scene lights.
        glUniform1i(m_uLightNum,     activeLightCount);
        glUniform1f(m_uAmbientBoost, kAmbientBoost);
    }

    glUseProgram(0);
    SDL_GL_SwapWindow(m_window);
}

// Main loop: processes input, updates state, renders frames.
void App::Run()
{
    Uint64 prevTick = SDL_GetTicks();
    bool   quit     = false;

    while (!quit)
    {
        const Uint64 nowTick = SDL_GetTicks();
        const float  dt      = static_cast<float>(nowTick - prevTick) * 0.001f;
        prevTick = nowTick;
        m_elapsedTime += dt;

        HandleEvents(dt, quit);
        m_camera.ProcessInput(SDL_GetKeyboardState(nullptr), dt);
        UpdateAnimations(m_elapsedTime);
        RenderFrame();
    }
}

// Frees all GPU resources and shuts down SDL.
void App::Shutdown()
{
    for (auto & o : m_objects)
    {
        o.mesh.Free();
    }

    glDeleteTextures(1, &m_fallbackTex);
    glDeleteTextures(1, &m_whiteTex);
    glDeleteTextures(1, &m_defaultNormalTex);

    for (const auto& [_, tex] : m_textureCache)
    {
        if (tex != 0 && tex != m_fallbackTex && tex != m_whiteTex && tex != m_defaultNormalTex)
        {
            glDeleteTextures(1, &tex);
        }
    }

    m_lightMarkerMesh.Free();

    SDL_GL_DestroyContext(m_glCtx);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}
