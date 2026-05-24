// CS300 Assignment 0 – full 3-D graphics pipeline
//
// Controls
//   Camera movement : W/S (elevation) | A/D (azimuth) | Q/E (zoom)
//   Toggles         : N (normals) | F (face/averaged normals) | T (texture mode)
//                     M (wireframe) | +/Z (more slices) | -/X (fewer slices)
//   Quit            : Escape

#include <algorithm>
#include <cmath>
#include <cstring>
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

#include "OGLDebug.h"
#include "Camera.h"
#include "CS300Parser.h"
#include "Mesh.h"

// ============================================================
//  Window size
// ============================================================
static const GLsizei WIN_W = 1280;
static const GLsizei WIN_H = 720;

// ============================================================
//  Shader sources
// ============================================================
static const char* k_vertSrc = R"glsl(
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;

uniform mat4 uMVP;
out vec2 vUV;
out vec3 vColor;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vUV         = aUV;
    vColor      = vec3(clamp(aUV.x, 0.0, 1.0), clamp(aUV.y, 0.0, 1.0), 0.0);
}
)glsl";

static const char* k_fragSrc = R"glsl(
#version 430 core
in vec2 vUV;
in vec3 vColor;
uniform bool uUseTexture;

out vec4 fragColor;

vec3 UVDebugTexture(vec2 uv)
{
    vec2 tiled = fract(uv);
    vec2 gridUV = vec2(tiled.x, 1.0 - tiled.y);
    ivec2 cell = ivec2(clamp(floor(gridUV * 6.0), vec2(0.0), vec2(5.0)));
    int idx = (cell.x + cell.y) % 6;

    vec3 palette[6] = vec3[](
        vec3(0.0, 0.0, 1.0), // blue
        vec3(0.0, 1.0, 1.0), // cyan
        vec3(0.0, 1.0, 0.0), // green
        vec3(1.0, 1.0, 0.0), // yellow
        vec3(1.0, 0.0, 0.0), // red
        vec3(1.0, 0.0, 1.0)  // magenta
    );
    return palette[idx];
}

void main()
{
    fragColor = vec4(uUseTexture ? UVDebugTexture(vUV) : vColor, 1.0);
}
)glsl";

// Normal line shader
static const char* k_normVertSrc = R"glsl(
#version 430 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)glsl";

static const char* k_normFragSrc = R"glsl(
#version 430 core
out vec4 fragColor;
void main()
{
    fragColor = vec4(1.0, 1.0, 0.0, 1.0);
}
)glsl";

// ============================================================
//  Shader helper
// ============================================================
static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error:\n" << log << '\n';
    }
    return s;
}

static GLuint LinkProgram(const char* vSrc, const char* fSrc)
{
    GLuint vs   = CompileShader(GL_VERTEX_SHADER,   vSrc);
    GLuint fs   = CompileShader(GL_FRAGMENT_SHADER, fSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << '\n';
    }

    glDetachShader(prog, vs); glDeleteShader(vs);
    glDetachShader(prog, fs); glDeleteShader(fs);
    return prog;
}

// ============================================================
//  Scene object
// ============================================================
enum class MeshKind { PLANE, CUBE, CONE, CYLINDER, SPHERE, OBJ };

struct SceneObject
{
    std::string  name;
    MeshKind     kind    = MeshKind::PLANE;
    std::string  objPath;
    Mesh         mesh;

    glm::vec3    pos{ 0.0f }, rot{ 0.0f }, sca{ 1.0f };

    glm::mat4 ModelMatrix() const
    {
        // Rotation order: X then Y then Z
        glm::mat4 T  = glm::translate(glm::mat4(1.0f), pos);
        glm::mat4 Rx = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1,0,0));
        glm::mat4 Ry = glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0,1,0));
        glm::mat4 Rz = glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0,0,1));
        glm::mat4 S  = glm::scale(glm::mat4(1.0f), sca);
        return T * Rx * Ry * Rz * S;
    }
};

// ============================================================
//  Build a Mesh from kind + slices
// ============================================================
static Mesh BuildMesh(MeshKind kind, const std::string& objPath, int slices)
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

// ============================================================
//  Generate missing OBJ files so the scene can load them
// ============================================================
static void EnsureMeshFiles()
{
    namespace fs = std::filesystem;
    fs::path dir("data/meshes");
    if (!fs::exists(dir))
        fs::create_directories(dir);

    auto saveIfMissing = [&](const char* name, Mesh m) {
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

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[])
{
    const char* sceneFile = (argc > 1) ? argv[1] : "scene_A0.txt";

    // ---- SDL init ----
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);

    SDL_Window* window = SDL_CreateWindow("CS300", WIN_W, WIN_H, SDL_WINDOW_OPENGL);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx)
    {
        std::cerr << "SDL_GL_CreateContext: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

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
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, nullptr);
#endif

    std::cout << "GL_VENDOR   : " << glGetString(GL_VENDOR)   << '\n';
    std::cout << "GL_RENDERER : " << glGetString(GL_RENDERER) << '\n';
    std::cout << "GL_VERSION  : " << glGetString(GL_VERSION)  << '\n';

    // ---- Parse scene ----
    CS300Parser scene;
    scene.LoadDataFromFile(sceneFile);

    // ---- Generate missing OBJ files ----
    EnsureMeshFiles();

    // ---- Camera ----
    Camera camera;
    camera.fovY   = glm::radians(scene.fovy);
    camera.aspect = float(WIN_W) / float(WIN_H);
    camera.zNear  = scene.nearPlane;
    camera.zFar   = scene.farPlane;
    camera.InitFromLookAt(scene.camPos, scene.camTarget, scene.camUp);

    // ---- Shaders ----
    GLuint mainProg = LinkProgram(k_vertSrc,     k_fragSrc);
    GLuint normProg = LinkProgram(k_normVertSrc, k_normFragSrc);

    GLint uMVP        = glGetUniformLocation(mainProg, "uMVP");
    GLint uUseTexture = glGetUniformLocation(mainProg, "uUseTexture");
    GLint uNormMVP    = glGetUniformLocation(normProg,  "uMVP");

    // ---- App-state toggles ----
    bool showNormals  = false;
    bool faceNormals  = true;
    bool textureMode  = false;
    bool wireframe    = false;
    int  currentSlices = 4;

    // ---- Build scene objects ----
    std::vector<SceneObject> objects;
    objects.reserve(scene.objects.size());

    for (const auto& so : scene.objects)
    {
        SceneObject obj;
        obj.name = so.name;
        obj.pos  = so.pos;
        obj.rot  = so.rot;
        obj.sca  = so.sca;

        const std::string& ms = so.mesh;
        if      (ms == "PLANE")    obj.kind = MeshKind::PLANE;
        else if (ms == "CUBE")     obj.kind = MeshKind::CUBE;
        else if (ms == "CONE")     obj.kind = MeshKind::CONE;
        else if (ms == "CYLINDER") obj.kind = MeshKind::CYLINDER;
        else if (ms == "SPHERE")   obj.kind = MeshKind::SPHERE;
        else { obj.kind = MeshKind::OBJ; obj.objPath = ms; }

        obj.mesh = BuildMesh(obj.kind, obj.objPath, currentSlices);
        obj.mesh.Upload(faceNormals);
        objects.push_back(std::move(obj));
    }

    // ---- OpenGL state ----
    glEnable(GL_DEPTH_TEST);

    // ---- Timing ----
    Uint64 prevTick = SDL_GetTicks();

    // ---- Main loop ----
    SDL_Event ev;
    bool quit = false;

    while (!quit)
    {
        Uint64 nowTick = SDL_GetTicks();
        float  dt      = float(nowTick - prevTick) * 0.001f;
        prevTick       = nowTick;

        // Helper: rebuild all parametric meshes with the current slice count
        auto rebuildSlicedMeshes = [&]() {
            for (auto& o : objects)
            {
                if (o.kind == MeshKind::CONE ||
                    o.kind == MeshKind::CYLINDER ||
                    o.kind == MeshKind::SPHERE)
                {
                    o.mesh.Free();
                    o.mesh = BuildMesh(o.kind, o.objPath, currentSlices);
                    o.mesh.Upload(faceNormals);
                }
            }
            std::cout << "Slices: " << currentSlices << '\n';
        };

        // -- Events --
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                quit = true;

            if (ev.type == SDL_EVENT_KEY_DOWN)
            {
                switch (ev.key.scancode)
                {
                case SDL_SCANCODE_ESCAPE: quit = true; break;

                case SDL_SCANCODE_N:
                    showNormals = !showNormals;
                    std::cout << "Normals: " << (showNormals ? "ON" : "OFF") << '\n';
                    break;

                case SDL_SCANCODE_F:
                    faceNormals = !faceNormals;
                    for (auto& o : objects) o.mesh.SetNormalMode(faceNormals);
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

                // Increase slices: + or Z
                case SDL_SCANCODE_EQUALS:
                case SDL_SCANCODE_KP_PLUS:
                case SDL_SCANCODE_Z:
                    currentSlices += 2;
                    rebuildSlicedMeshes();
                    break;

                // Decrease slices: - or X
                case SDL_SCANCODE_MINUS:
                case SDL_SCANCODE_KP_MINUS:
                case SDL_SCANCODE_X:
                    currentSlices = std::max(4, currentSlices - 2);
                    rebuildSlicedMeshes();
                    break;

                default: break;
                }
            }
        }

        // -- Camera continuous input --
        camera.ProcessInput(SDL_GetKeyboardState(nullptr), dt);

        // -- Matrices --
        glm::mat4 V  = camera.GetView();
        glm::mat4 P  = camera.GetProjection();
        glm::mat4 VP = P * V;

        // -- Clear --
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        // -- Draw objects --
        glUseProgram(mainProg);

        for (const auto& obj : objects)
        {
            if (!obj.mesh.IsValid()) continue;

            glm::mat4 M   = obj.ModelMatrix();
            glm::mat4 MVP = VP * M;

            glUniformMatrix4fv(uMVP,       1, GL_FALSE, glm::value_ptr(MVP));
            glUniform1i(uUseTexture, textureMode ? 1 : 0);

            obj.mesh.Draw();
        }

        // -- Draw normal lines --
        if (showNormals)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glUseProgram(normProg);

            for (const auto& obj : objects)
            {
                if (!obj.mesh.HasNormals()) continue;

                glm::mat4 MVP = VP * obj.ModelMatrix();
                glUniformMatrix4fv(uNormMVP, 1, GL_FALSE, glm::value_ptr(MVP));
                obj.mesh.DrawNormals();
            }
        }

        glUseProgram(0);
        SDL_GL_SwapWindow(window);
    }

    // -- Cleanup --
    for (auto& o : objects) o.mesh.Free();
    glDeleteProgram(mainProg);
    glDeleteProgram(normProg);

    SDL_GL_DestroyContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
