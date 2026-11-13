#include "Graphics/Shader.hpp"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace plumbcad
{
    static std::string readText(const std::string& path)
    {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Shader file not found: " + path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    static unsigned int compile(unsigned int type, const std::string& source)
    {
        unsigned int shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        int ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetShaderInfoLog(shader, 1024, nullptr, log);
            throw std::runtime_error(log);
        }
        return shader;
    }

    Shader::~Shader()
    {
        if (m_program) glDeleteProgram(m_program);
    }

    void Shader::loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath)
    {
        auto vs = compile(GL_VERTEX_SHADER, readText(vertexPath));
        auto fs = compile(GL_FRAGMENT_SHADER, readText(fragmentPath));
        m_program = glCreateProgram();
        glAttachShader(m_program, vs);
        glAttachShader(m_program, fs);
        glLinkProgram(m_program);
        glDeleteShader(vs);
        glDeleteShader(fs);

        int ok = 0;
        glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetProgramInfoLog(m_program, 1024, nullptr, log);
            throw std::runtime_error(log);
        }
    }

    void Shader::use() const { glUseProgram(m_program); }
    void Shader::setBool(const char* name, bool value) const { glUniform1i(glGetUniformLocation(m_program, name), value); }
    void Shader::setMat4(const char* name, const glm::mat4& value) const { glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, glm::value_ptr(value)); }
    void Shader::setVec3(const char* name, const glm::vec3& value) const { glUniform3fv(glGetUniformLocation(m_program, name), 1, glm::value_ptr(value)); }
}
