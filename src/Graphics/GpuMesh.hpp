#pragma once
#include "Geometry/MeshData.hpp"

namespace plumbcad
{
    class GpuMesh
    {
    public:
        GpuMesh() = default;
        ~GpuMesh();
        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;
        GpuMesh(GpuMesh&& other) noexcept;
        GpuMesh& operator=(GpuMesh&& other) noexcept;

        void upload(const MeshData& mesh, bool lines = false);
        void draw() const;
        bool valid() const { return m_vao != 0; }

    private:
        unsigned int m_vao{0};
        unsigned int m_vbo{0};
        unsigned int m_ebo{0};
        int m_indexCount{0};
        unsigned int m_mode{0};
    };
}
