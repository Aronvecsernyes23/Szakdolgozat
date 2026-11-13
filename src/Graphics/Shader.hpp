#pragma once
#include <glm/glm.hpp>
#include <string>

namespace plumbcad
{
    class Shader
    {
    public:
        Shader() = default;
        ~Shader();
        void loadFromFiles(const std::string& vertexPath, const std::string& fragmentPath);
        void use() const;
        void setBool(const char* name, bool value) const;
        void setMat4(const char* name, const glm::mat4& value) const;
        void setVec3(const char* name, const glm::vec3& value) const;
        unsigned int id() const { return m_program; }
    private:
        unsigned int m_program{0};
    };
}
