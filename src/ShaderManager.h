#pragma once

#include <GL/glew.h>

#include <string>
#include <unordered_map>

// Loads and stores OpenGL shader programs.
class ShaderManager
{
public:
    // Builds a program from vertex and fragment shader files.
    bool LoadProgram(const std::string & programName,
                     const std::string & vertexPath,
                     const std::string & fragmentPath);

    // Returns a program id or 0 if not found.
    GLuint GetProgram(const std::string & programName) const;

    // Deletes all programs.
    void Clear();

    ~ShaderManager();

private:
    GLuint CompileShader(GLenum shaderType,
                         const std::string & source,
                         const std::string & filename) const;
    bool   ValidateProgram(GLuint program,
                           const std::string & vertexPath,
                           const std::string & fragmentPath) const;
    bool   ReadFile(const std::string & path, std::string & outContent) const;

    std::unordered_map<std::string, GLuint> programs_;
};
