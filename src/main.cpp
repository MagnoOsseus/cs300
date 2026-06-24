#include <iostream>
#include <string>

#include "App.h"

// Entry point: parses the scene file argument and runs the application.
int main(int argc, char * argv[])
{
    std::string sceneFileStr = (argc > 1) ? argv[1] : "data/scenes/scene_A2.txt";
    // If no path separator is found, assume the file is in data/scenes/.
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
