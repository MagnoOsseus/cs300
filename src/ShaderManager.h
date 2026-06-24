#pragma once

#include <GL/glew.h>

#include <string>
#include <unordered_map>

// Load and store OpenGL programs.
class ShaderManager
{
public:
    // Build program from vertex and fragment files.
    bool LoadProgram(const std::string & programName,
                     const std::string & vertexPath,
                     const std::string & fragmentPath);

    // Return program id, or 0 if missing.
    GLuint GetProgram(const std::string & programName) const;

    // Delete all programs.
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
