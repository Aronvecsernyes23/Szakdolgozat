#include "Graphics/Renderer.hpp"
#include <glad/gl.h>

namespace plumbcad
{
    void Renderer::init()
    {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        m_shader.loadFromFiles(std::string(ASSET_DIR) + "/shaders/basic.vert",
                               std::string(ASSET_DIR) + "/shaders/basic.frag");
    }

    void Renderer::uploadObject(const SceneObject &object, bool lines)
    {
        GpuMesh mesh;
        mesh.upload(object.mesh, lines);
        m_meshes[object.id] = std::move(mesh);
    }

    void Renderer::removeObject(ObjectId id)
    {
        m_meshes.erase(id);
    }

    void Renderer::render(const std::vector<SceneObject> &objects, const Camera &camera, int width, int height,
                          ObjectId selectedId, bool wireframe)
    {
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_shader.use();
        float aspect = height == 0 ? 1.0f : static_cast<float>(width) / static_cast<float>(height);
        m_shader.setMat4("uView", camera.view());
        m_shader.setMat4("uProj", camera.projection(aspect));

        if (wireframe)
        {
            // Valódi OpenGL wireframe: a háromszögek élei rajzolódnak.
            // Culling nélkül a térbeli idomok hátsó élei is értelmezhetők.
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

            glDisable(GL_CULL_FACE);
        }
        else
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            glEnable(GL_CULL_FACE);
        }

        for (const auto &object : objects)
        {
            auto it = m_meshes.find(object.id);
            if (it == m_meshes.end()) continue;

            if (object.renderOnTop)
            {
                glDisable(GL_DEPTH_TEST);
            }

            m_shader.setMat4("uModel", object.transform.matrix());
            // A kijelölt elem minden objektumtípusnál egységesen sárga.
            // A tárolt alapszín ettől nem változik: kijelölés megszűnésekor
            // a cső visszakapja a kéket, az akadály a pirosat.
            const glm::vec3 renderColor = object.id == selectedId ? glm::vec3{1.0f, 0.92f, 0.05f} : object.color;
            m_shader.setVec3("uColor", renderColor);
            m_shader.setBool("uUnlit", object.unlit);
            it->second.draw();

            if (object.renderOnTop)
            {
                glEnable(GL_DEPTH_TEST);
            }
        }

        // Az ImGui és a következő frame mindig kitöltött poligonmódból induljon.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glEnable(GL_CULL_FACE);
    }
}
