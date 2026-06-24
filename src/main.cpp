#include <iostream>
#include <string>

#include "App.h"

// Program entry point.
int main(int argc, char * argv[])
{
    std::string sceneFileStr = (argc > 1) ? argv[1] : "data/scenes/scene_A2.txt";
    // Use data/scenes/ when only a file name is passed.
    if (argc > 1 && sceneFileStr.find('/') == std::string::npos && sceneFileStr.find('\\') == std::string::npos)
    {
        sceneFileStr = "data/scenes/" + sceneFileStr;
    }

    App app;
    if (!app.Init(sceneFileStr.c_str()))
    {
        return 1;
    }
    app.Run();
    app.Shutdown();
    return 0;
}
