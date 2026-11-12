#include "Graphics/GpuMesh.hpp"
#include <glad/gl.h>
#include <utility>

namespace plumbcad
{
    GpuMesh::~GpuMesh()
    {
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
    }

    GpuMesh::GpuMesh(GpuMesh&& other) noexcept { *this = std::move(other); }

    GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept
    {
        if (this == &other) return *this;
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
        m_ebo = std::exchange(other.m_ebo, 0);
        m_indexCount = std::exchange(other.m_indexCount, 0);
        m_mode = std::exchange(other.m_mode, 0);
        return *this;
    }

    void GpuMesh::upload(const MeshData& mesh, bool lines)
    {
        m_mode = lines ? GL_LINES : GL_TRIANGLES;
        m_indexCount = static_cast<int>(mesh.indices.size());
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(unsigned int), mesh.indices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
        glBindVertexArray(0);
    }

    void GpuMesh::draw() const
    {
        if (!m_vao) return;
        glBindVertexArray(m_vao);
        glDrawElements(m_mode, m_indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
}
