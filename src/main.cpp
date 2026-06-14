#include <iostream>
#include <string>

#include <GL/glew.h>
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include "Camera.h"
#include "CS300Parser.h"
#include "OGLDebug.h"
#include "Renderer.h"

static const GLsizei WIN_W = 1280;
static const GLsizei WIN_H = 720;

int main(int argc, char* argv[])
{
    const char* sceneFile = (argc > 1) ? argv[1] : "scene_A1.txt";

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow("CS300 Assignment 1", WIN_W, WIN_H, SDL_WINDOW_OPENGL);
    if (!window)
    {
        std::cerr << "SDL_CreateWindow: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext)
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
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if _DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, nullptr);
#endif

    std::cout << "GL_VENDOR   : " << glGetString(GL_VENDOR) << '\n';
    std::cout << "GL_RENDERER : " << glGetString(GL_RENDERER) << '\n';
    std::cout << "GL_VERSION  : " << glGetString(GL_VERSION) << '\n';

    CS300Parser parser;
    Scene scene = parser.LoadDataFromFile(sceneFile);

    Camera camera;
    camera.fovY = glm::radians(scene.fovy);
    camera.aspect = float(WIN_W) / float(WIN_H);
    camera.zNear = scene.nearPlane;
    camera.zFar = scene.farPlane;
    camera.InitFromLookAt(scene.camPos, scene.camTarget, scene.camUp);

    Renderer renderer;
    if (!renderer.Initialize(scene, "data/shaders"))
    {
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);

    bool showNormals = false;
    bool faceNormalsEnabled = false;
    bool textureMappingEnabled = true;
    bool wireframe = false;

    Uint64 previousTick = SDL_GetTicks();
    SDL_Event event;
    bool quit = false;

    while (!quit)
    {
        const Uint64 currentTick = SDL_GetTicks();
        const float deltaTime = float(currentTick - previousTick) * 0.001f;
        const float elapsedTime = float(currentTick) * 0.001f;
        previousTick = currentTick;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                quit = true;
            }

            if (event.type == SDL_EVENT_KEY_DOWN)
            {
                switch (event.key.scancode)
                {
                case SDL_SCANCODE_ESCAPE:
                    quit = true;
                    break;
                case SDL_SCANCODE_N:
                    showNormals = !showNormals;
                    std::cout << "Normals: " << (showNormals ? "ON" : "OFF") << '\n';
                    break;
                case SDL_SCANCODE_T:
                    textureMappingEnabled = !textureMappingEnabled;
                    std::cout << "Texture mapping: " << (textureMappingEnabled ? "ON" : "OFF") << '\n';
                    break;
                case SDL_SCANCODE_F:
                    faceNormalsEnabled = !faceNormalsEnabled;
                    renderer.SetFaceNormalMode(faceNormalsEnabled);
                    std::cout << "Normal mode: " << (faceNormalsEnabled ? "FACE" : "AVERAGED") << '\n';
                    break;
                case SDL_SCANCODE_M:
                    wireframe = !wireframe;
                    std::cout << "Wireframe: " << (wireframe ? "ON" : "OFF") << '\n';
                    break;
                case SDL_SCANCODE_EQUALS:
                case SDL_SCANCODE_KP_PLUS:
                    renderer.RebuildSlicedMeshes(renderer.GetSlices() + 2);
                    std::cout << "Slices: " << renderer.GetSlices() << '\n';
                    break;
                case SDL_SCANCODE_MINUS:
                case SDL_SCANCODE_KP_MINUS:
                    renderer.RebuildSlicedMeshes(renderer.GetSlices() - 2);
                    std::cout << "Slices: " << renderer.GetSlices() << '\n';
                    break;
                default:
                    break;
                }
            }
        }

        camera.ProcessInput(SDL_GetKeyboardState(nullptr), deltaTime);
        scene.UpdateAnimations(elapsedTime);
        renderer.Render(scene, camera, textureMappingEnabled, wireframe, showNormals);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
