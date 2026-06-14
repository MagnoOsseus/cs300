#include "ShaderManager.h"

#include <fstream>
#include <iostream>
#include <sstream>

ShaderManager::~ShaderManager()
{
    Clear();
}

bool ShaderManager::LoadProgram(const std::string& name,
                                const std::string& vertexPath,
                                const std::string& fragmentPath)
{
    const std::string vertexSource = ReadTextFile(vertexPath);
    const std::string fragmentSource = ReadTextFile(fragmentPath);

    if (vertexSource.empty() || fragmentSource.empty())
    {
        return false;
    }

    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource, vertexPath);
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource, fragmentPath);

    if (vertexShader == 0 || fragmentShader == 0)
    {
        if (vertexShader != 0)
        {
            glDeleteShader(vertexShader);
        }
        if (fragmentShader != 0)
        {
            glDeleteShader(fragmentShader);
        }
        return false;
    }

    const GLuint program = LinkProgram(vertexShader, fragmentShader, vertexPath, fragmentPath);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (program == 0)
    {
        return false;
    }

    auto existing = programs.find(name);
    if (existing != programs.end())
    {
        glDeleteProgram(existing->second.program);
    }

    programs[name] = ProgramRecord{ program, vertexPath, fragmentPath };
    return true;
}

GLuint ShaderManager::GetProgram(const std::string& name) const
{
    const auto found = programs.find(name);
    return found == programs.end() ? 0 : found->second.program;
}

void ShaderManager::Clear()
{
    for (auto& [name, record] : programs)
    {
        if (record.program != 0)
        {
            glDeleteProgram(record.program);
            record.program = 0;
        }
    }

    programs.clear();
}

std::string ShaderManager::ReadTextFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " << path << '\n';
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

GLuint ShaderManager::CompileShader(GLenum type,
                                    const std::string& source,
                                    const std::string& filename)
{
    const GLuint shader = glCreateShader(type);
    const char* sourcePtr = source.c_str();
    glShaderSource(shader, 1, &sourcePtr, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_TRUE)
    {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    std::cerr << "Shader compile error in " << filename << ":\n" << log << '\n';
    glDeleteShader(shader);
    return 0;
}

GLuint ShaderManager::LinkProgram(GLuint vertexShader,
                                  GLuint fragmentShader,
                                  const std::string& vertexFilename,
                                  const std::string& fragmentFilename)
{
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_TRUE)
    {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log(static_cast<size_t>(std::max(logLength, 1)), '\0');
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    std::cerr << "Program link error for " << vertexFilename << " + " << fragmentFilename << ":\n"
              << log << '\n';
    glDeleteProgram(program);
    return 0;
}
