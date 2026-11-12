#pragma once
#include "Geometry/SceneObject.hpp"
#include "Graphics/Camera.hpp"
#include "Graphics/GpuMesh.hpp"
#include "Graphics/Shader.hpp"
#include <unordered_map>

namespace plumbcad
{
    class Renderer
    {
      public:
        void init();
        void uploadObject(const SceneObject &object, bool lines = false);
        void removeObject(ObjectId id);
        void render(const std::vector<SceneObject> &objects, const Camera &camera, int width, int height,
                    ObjectId selectedId = InvalidObjectId, bool wireframe = false);

      private:
        Shader m_shader;
        std::unordered_map<ObjectId, GpuMesh> m_meshes;
    };
}
