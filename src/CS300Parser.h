#pragma once

#include <glm/glm.hpp>

#include <fstream>

#include "Scene.h"

class CS300Parser
{
public:
    Scene LoadDataFromFile(const char* filename) const;

private:
    static float ReadFloat(std::ifstream& file);
    static int ReadInt(std::ifstream& file);
    static glm::vec3 ReadVec3(std::ifstream& file);
};
