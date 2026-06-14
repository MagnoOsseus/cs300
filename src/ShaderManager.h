#pragma once

#include <GL/glew.h>

#include <string>
#include <unordered_map>

class ShaderManager
{
public:
    ~ShaderManager();

    bool LoadProgram(const std::string& name,
                     const std::string& vertexPath,
                     const std::string& fragmentPath);

    GLuint GetProgram(const std::string& name) const;
    void Clear();

private:
    struct ProgramRecord
    {
        GLuint program = 0;
        std::string vertexPath;
        std::string fragmentPath;
    };

    std::unordered_map<std::string, ProgramRecord> programs;

    static std::string ReadTextFile(const std::string& path);
    static GLuint CompileShader(GLenum type,
                                const std::string& source,
                                const std::string& filename);
    static GLuint LinkProgram(GLuint vertexShader,
                              GLuint fragmentShader,
                              const std::string& vertexFilename,
                              const std::string& fragmentFilename);
};
