#include "ShaderManager.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

bool ShaderManager::ReadFile(const std::string & path, std::string & outContent) const
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Shader file open error [" << path << "]\n";
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    outContent = ss.str();
    return true;
}

GLuint ShaderManager::CompileShader(GLenum shaderType,
                                    const std::string & source,
                                    const std::string & filename) const
{
    GLuint shader = glCreateShader(shaderType);
    const char * src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return shader;
    }

    GLint logLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
    std::vector<char> log(static_cast<size_t>(std::max(1, logLen)));
    glGetShaderInfoLog(shader, logLen, nullptr, log.data());
    std::cerr << "Shader compile error [" << filename << "]\n" << log.data() << '\n';

    glDeleteShader(shader);
    return 0;
}

bool ShaderManager::ValidateProgram(GLuint program,
                                    const std::string & vertexPath,
                                    const std::string & fragmentPath) const
{
    glValidateProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_VALIDATE_STATUS, &ok);
    if (ok == GL_TRUE)
    {
        return true;
    }

    GLint logLen = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
    std::vector<char> log(static_cast<size_t>(std::max(1, logLen)));
    glGetProgramInfoLog(program, logLen, nullptr, log.data());
    std::cerr << "Program validation error [" << vertexPath << ", " << fragmentPath << "]\n"
              << log.data() << '\n';
    return false;
}

bool ShaderManager::LoadProgram(const std::string & programName,
                                const std::string & vertexPath,
                                const std::string & fragmentPath)
{
    std::string vertexSrc;
    std::string fragmentSrc;
    if (!ReadFile(vertexPath, vertexSrc) || !ReadFile(fragmentPath, fragmentSrc))
    {
        return false;
    }

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexSrc, vertexPath);
    if (vs == 0)
    {
        return false;
    }

    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentSrc, fragmentPath);
    if (fs == 0)
    {
        glDeleteShader(vs);
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(static_cast<size_t>(std::max(1, logLen)));
        glGetProgramInfoLog(program, logLen, nullptr, log.data());
        std::cerr << "Program link error [" << vertexPath << ", " << fragmentPath << "]\n"
                  << log.data() << '\n';

        glDeleteProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    if (!ValidateProgram(program, vertexPath, fragmentPath))
    {
        glDeleteProgram(program);
        return false;
    }

    auto found = programs_.find(programName);
    if (found != programs_.end())
    {
        glDeleteProgram(found->second);
    }
    programs_[programName] = program;
    return true;
}

GLuint ShaderManager::GetProgram(const std::string & programName) const
{
    auto it = programs_.find(programName);
    if (it == programs_.end())
    {
        return 0;
    }
    return it->second;
}

void ShaderManager::Clear()
{
    for (const auto & [_, program] : programs_)
    {
        glDeleteProgram(program);
    }
    programs_.clear();
}

ShaderManager::~ShaderManager()
{
    Clear();
}
