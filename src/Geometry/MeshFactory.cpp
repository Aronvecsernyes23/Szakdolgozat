#include "Geometry/MeshFactory.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace plumbcad
{
    namespace
    {
        void appendMesh(MeshData &target, const MeshData &source, const glm::mat4 &transform = glm::mat4{1.0f})
        {
            const unsigned int base = static_cast<unsigned int>(target.vertices.size());

            const glm::mat3 normalTransform = glm::transpose(glm::inverse(glm::mat3(transform)));

            for (const Vertex &vertex : source.vertices)
            {
                Vertex transformed;
                transformed.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));

                transformed.normal = glm::normalize(normalTransform * vertex.normal);

                target.vertices.push_back(transformed);
            }

            for (unsigned int index : source.indices)
            {
                target.indices.push_back(base + index);
            }
        }

        void appendDisc(MeshData &mesh, float radius, float z, float normalZ, int segments)
        {
            radius = std::max(radius, 0.001f);

            segments = std::max(segments, 12);

            const unsigned int centerIndex = static_cast<unsigned int>(mesh.vertices.size());

            mesh.vertices.push_back({glm::vec3{0.0f, 0.0f, z}, glm::vec3{0.0f, 0.0f, normalZ}});

            const unsigned int ringStart = static_cast<unsigned int>(mesh.vertices.size());

            for (int i = 0; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);

                const float angle = t * 2.0f * 3.14159265359f;

                mesh.vertices.push_back({glm::vec3{radius * std::cos(angle), radius * std::sin(angle), z},
                                         glm::vec3{0.0f, 0.0f, normalZ}});
            }

            for (int i = 0; i < segments; ++i)
            {
                const unsigned int a = ringStart + static_cast<unsigned int>(i);

                const unsigned int b = a + 1;

                if (normalZ >= 0.0f)
                {
                    mesh.indices.insert(mesh.indices.end(), {centerIndex, a, b});
                }
                else
                {
                    mesh.indices.insert(mesh.indices.end(), {centerIndex, b, a});
                }
            }
        }
    }

    MeshData MeshFactory::makeBox(const glm::vec3 &requestedSize)
    {
        const glm::vec3 size{std::max(0.02f, requestedSize.x), std::max(0.02f, requestedSize.y),
                             std::max(0.02f, requestedSize.z)};
        const glm::vec3 h = size * 0.5f;
        MeshData mesh;
        auto addFace = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                           const glm::vec3 &normal) {
            const unsigned int base = static_cast<unsigned int>(mesh.vertices.size());
            mesh.vertices.push_back({a, normal});
            mesh.vertices.push_back({b, normal});
            mesh.vertices.push_back({c, normal});
            mesh.vertices.push_back({d, normal});
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        };
        addFace({-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z}, {0, 0, 1});
        addFace({h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z}, {0, 0, -1});
        addFace({h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z}, {1, 0, 0});
        addFace({-h.x, -h.y, -h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z}, {-1, 0, 0});
        addFace({-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}, {0, 1, 0});
        addFace({-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z}, {0, -1, 0});
        return mesh;
    }

    MeshData MeshFactory::makeCylinder(float radius, float length, int segments)
    {
        MeshData mesh;
        const float half = length * 0.5f;
        for (int i = 0; i <= segments; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(segments);
            float a = t * 2.0f * 3.14159265359f;
            glm::vec3 n{std::cos(a), std::sin(a), 0.0f};
            mesh.vertices.push_back({{radius * n.x, radius * n.y, -half}, n});
            mesh.vertices.push_back({{radius * n.x, radius * n.y, half}, n});
        }
        for (int i = 0; i < segments; ++i)
        {
            unsigned int base = static_cast<unsigned int>(i * 2);
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }
        return mesh;
    }

    MeshData MeshFactory::makeArrow(float length, float shaftRadius, float headRadius, float headLength,
                                    int segments)
    {
        MeshData mesh;

        length = std::max(length, 0.10f);

        segments = std::max(segments, 12);

        headLength = std::clamp(headLength, length * 0.10f, length * 0.45f);

        shaftRadius = std::clamp(shaftRadius, 0.003f, length * 0.12f);

        headRadius = std::clamp(headRadius, shaftRadius * 1.5f, length * 0.22f);

        const float shaftLength = length - headLength;

        // A henger alapból a saját közepe körül, Z tengely mentén készül.
        // +Z irányba toljuk, hogy a teljes nyíl pontosan az origóból induljon.
        MeshData shaft = makeCylinder(shaftRadius, shaftLength, segments);

        appendMesh(mesh, shaft, glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.0f, shaftLength * 0.5f}));

        // A nyíl töve és a nyílhegy alja zárt felületet kap.
        appendDisc(mesh, shaftRadius, 0.0f, -1.0f, segments);

        appendDisc(mesh, headRadius, shaftLength, -1.0f, segments);

        // Kúp alakú nyílhegy.
        const unsigned int ringStart = static_cast<unsigned int>(mesh.vertices.size());

        const float slopeNormalZ = headRadius / std::max(headLength, 0.001f);

        for (int i = 0; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);

            const float angle = t * 2.0f * 3.14159265359f;

            const float c = std::cos(angle);

            const float s = std::sin(angle);

            const glm::vec3 normal = glm::normalize(glm::vec3{c, s, slopeNormalZ});

            mesh.vertices.push_back({glm::vec3{headRadius * c, headRadius * s, shaftLength}, normal});
        }

        const unsigned int tipIndex = static_cast<unsigned int>(mesh.vertices.size());

        mesh.vertices.push_back({glm::vec3{0.0f, 0.0f, length}, glm::vec3{0.0f, 0.0f, 1.0f}});

        for (int i = 0; i < segments; ++i)
        {
            const unsigned int a = ringStart + static_cast<unsigned int>(i);

            const unsigned int b = a + 1;

            mesh.indices.insert(mesh.indices.end(), {a, b, tipIndex});
        }

        return mesh;
    }

    MeshData MeshFactory::makeEndpointRing(float radius, float thickness, int segments)
    {
        MeshData mesh;
        segments = std::max(segments, 16);
        radius = std::max(radius, 0.005f);
        thickness = std::clamp(thickness, radius * 0.18f, radius * 0.45f);

        // A jelölő külső kerülete pontosan a cső külső sugarán fut.
        // A vastagsága befelé terjed, ezért a gyűrű nem lóg túl a csővégen.
        const float outerRadius = radius;
        const float innerRadius = std::max(0.001f, radius - thickness);

        // A gyűrű lokálisan az XY síkban van, a normálja +Z.
        // Az Application a csővég irányába forgatja, így a jelölés
        // közvetlenül a cső keresztmetszeti körvonalára ül.
        for (int i = 0; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float a = t * 2.0f * 3.14159265359f;
            const float c = std::cos(a);
            const float s = std::sin(a);
            mesh.vertices.push_back({{outerRadius * c, outerRadius * s, 0.0f}, {0.0f, 0.0f, 1.0f}});
            mesh.vertices.push_back({{innerRadius * c, innerRadius * s, 0.0f}, {0.0f, 0.0f, 1.0f}});
        }

        for (int i = 0; i < segments; ++i)
        {
            const unsigned int base = static_cast<unsigned int>(i * 2);
            const unsigned int next = base + 2;

            // Elülső oldal
            mesh.indices.insert(mesh.indices.end(), {base, next, base + 1, base + 1, next, next + 1});
            // Hátoldal is, hogy a gyűrű mindkét nézetből látszódjon bekapcsolt backface culling mellett is.
            mesh.indices.insert(mesh.indices.end(), {base + 1, next, base, next + 1, next, base + 1});
        }

        return mesh;
    }

    MeshData MeshFactory::makeElbow(float tubeRadius, float bendRadius, float angleRadians, int arcSegments,
                                    int radialSegments)
    {
        MeshData mesh;
        bendRadius = std::max(bendRadius, tubeRadius * 1.5f);
        angleRadians = std::clamp(angleRadians, 0.001f, 3.14159265359f);
        arcSegments = std::max(arcSegments, 4);
        radialSegments = std::max(radialSegments, 8);

        // Általános könyökidom az XZ síkban.
        // 0. végpont: (R,0,0), kifelé mutató csőtengely lokálisan -Z.
        // 1. végpont: (R*cos(angle),0,R*sin(angle)), kifelé mutató csőtengely lokálisan
        // (-sin(angle),0,cos(angle)). 90° és 45° könyök is ebből készül.
        for (int i = 0; i <= arcSegments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(arcSegments);
            const float a = t * angleRadians;
            const glm::vec3 center{bendRadius * std::cos(a), 0.0f, bendRadius * std::sin(a)};
            const glm::vec3 radial{std::cos(a), 0.0f, std::sin(a)};
            const glm::vec3 up{0.0f, 1.0f, 0.0f};

            for (int j = 0; j <= radialSegments; ++j)
            {
                const float u = static_cast<float>(j) / static_cast<float>(radialSegments);
                const float b = u * 2.0f * 3.14159265359f;
                const glm::vec3 normal = glm::normalize(radial * std::cos(b) + up * std::sin(b));
                mesh.vertices.push_back({center + normal * tubeRadius, normal});
            }
        }

        const int ring = radialSegments + 1;
        for (int i = 0; i < arcSegments; ++i)
        {
            for (int j = 0; j < radialSegments; ++j)
            {
                const unsigned int a = static_cast<unsigned int>(i * ring + j);
                const unsigned int b = static_cast<unsigned int>((i + 1) * ring + j);
                const unsigned int c = static_cast<unsigned int>((i + 1) * ring + j + 1);
                const unsigned int d = static_cast<unsigned int>(i * ring + j + 1);
                mesh.indices.insert(mesh.indices.end(), {a, b, d, b, c, d});
            }
        }
        return mesh;
    }

    MeshData MeshFactory::makeElbow90(float tubeRadius, float bendRadius, int arcSegments, int radialSegments)
    {
        return makeElbow(tubeRadius, bendRadius, 3.14159265359f * 0.5f, arcSegments, radialSegments);
    }

    MeshData MeshFactory::makeElbow45(float tubeRadius, float bendRadius, int arcSegments, int radialSegments)
    {
        return makeElbow(tubeRadius, bendRadius, 3.14159265359f * 0.25f, arcSegments, radialSegments);
    }

    MeshData MeshFactory::makeTee(float tubeRadius, float mainLength, float branchLength, int segments)
    {
        MeshData mesh = makeCylinder(tubeRadius, std::max(mainLength, tubeRadius * 4.0f), segments);

        MeshData branch = makeCylinder(tubeRadius, std::max(branchLength, tubeRadius * 3.0f), segments);
        const unsigned int baseIndex = static_cast<unsigned int>(mesh.vertices.size());
        const float halfBranch = std::max(branchLength, tubeRadius * 3.0f) * 0.5f;

        // A henger alapból Z tengely mentén áll. A leágazást X tengelyre fordítjuk,
        // majd úgy toljuk el, hogy a T-idom közepe a fő cső tengelyén legyen.
        for (auto v : branch.vertices)
        {
            const glm::vec3 p{v.position.z + halfBranch, v.position.y, v.position.x};
            const glm::vec3 n{v.normal.z, v.normal.y, v.normal.x};
            mesh.vertices.push_back({p, glm::normalize(n)});
        }
        for (unsigned int idx : branch.indices)
        {
            mesh.indices.push_back(baseIndex + idx);
        }
        return mesh;
    }

    MeshData MeshFactory::makeReducer(float inletRadius, float outletRadius, float length, int segments)
    {
        MeshData mesh;

        inletRadius = std::max(inletRadius, 0.003f);

        outletRadius = std::max(outletRadius, 0.003f);

        length = std::max(length, 0.08f);

        segments = std::max(segments, 16);

        const float half = length * 0.5f;

        const float slopeZ = (inletRadius - outletRadius) / length;

        for (int i = 0; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);

            const float angle = t * 2.0f * 3.14159265359f;

            const float c = std::cos(angle);

            const float s = std::sin(angle);

            const glm::vec3 normal = glm::normalize(glm::vec3{c, s, slopeZ});

            mesh.vertices.push_back({glm::vec3{inletRadius * c, inletRadius * s, -half}, normal});

            mesh.vertices.push_back({glm::vec3{outletRadius * c, outletRadius * s, half}, normal});
        }

        for (int i = 0; i < segments; ++i)
        {
            const unsigned int base = static_cast<unsigned int>(i * 2);

            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }

        // A redukció két vége szándékosan nyitott:
        // itt csatlakoznak a szomszédos csőelemek.
        return mesh;
    }

    MeshData MeshFactory::makeValve(float tubeRadius, float length, int segments)
    {
        tubeRadius = std::max(tubeRadius, 0.003f);

        length = std::max(length, 0.10f);

        segments = std::max(segments, 16);

        MeshData mesh = makeCylinder(tubeRadius, length, segments);

        // Szelep test: a középső szakasz nagyobb átmérőjű.
        const float bodyRadius = tubeRadius * 1.55f;

        const float bodyLength = std::min(length * 0.55f, std::max(tubeRadius * 3.2f, 0.07f));

        appendMesh(mesh, makeCylinder(bodyRadius, bodyLength, segments));

        // Szeleporsó a +Y irányban.
        const float stemRadius = std::max(tubeRadius * 0.28f, 0.004f);

        const float stemLength = std::max(bodyRadius * 1.9f, 0.06f);

        glm::mat4 stemTransform{1.0f};
        stemTransform = glm::translate(stemTransform, glm::vec3{0.0f, bodyRadius + stemLength * 0.42f, 0.0f});

        stemTransform = glm::rotate(stemTransform, -1.57079632679f, glm::vec3{1.0f, 0.0f, 0.0f});

        appendMesh(mesh, makeCylinder(stemRadius, stemLength, 16), stemTransform);

        // Egyszerű kézikerék: lapos, széles henger Y tengelyen.
        const float wheelRadius = std::max(bodyRadius * 0.90f, 0.025f);

        const float wheelThickness = std::max(tubeRadius * 0.30f, 0.008f);

        glm::mat4 wheelTransform{1.0f};
        wheelTransform = glm::translate(wheelTransform, glm::vec3{0.0f, bodyRadius + stemLength, 0.0f});

        wheelTransform = glm::rotate(wheelTransform, -1.57079632679f, glm::vec3{1.0f, 0.0f, 0.0f});

        appendMesh(mesh, makeCylinder(wheelRadius, wheelThickness, 24), wheelTransform);

        return mesh;
    }

    MeshData MeshFactory::makeEndCap(float tubeRadius, float length, int segments)
    {
        tubeRadius = std::max(tubeRadius, 0.003f);

        length = std::max(length, 0.06f);

        segments = std::max(segments, 16);

        MeshData mesh = makeCylinder(tubeRadius * 1.08f, length, segments);

        // A -Z oldal a csatlakozó oldal, ezért nyitott marad.
        // A +Z oldal valódi zárt kupakot kap.
        appendDisc(mesh, tubeRadius * 1.08f, length * 0.5f, 1.0f, segments);

        // Rövid váll a csatlakozó oldalon, hogy vizuálisan is idomnak látszódjon.
        const float collarLength = std::min(length * 0.32f, std::max(tubeRadius * 0.8f, 0.018f));

        glm::mat4 collarTransform{1.0f};
        collarTransform =
            glm::translate(collarTransform, glm::vec3{0.0f, 0.0f, -length * 0.5f + collarLength * 0.5f});

        appendMesh(mesh, makeCylinder(tubeRadius * 1.18f, collarLength, segments), collarTransform);

        return mesh;
    }

    MeshData MeshFactory::makeGrid(float halfSize, int lines)
    {
        MeshData mesh;
        float step = (halfSize * 2.0f) / static_cast<float>(lines);
        for (int i = 0; i <= lines; ++i)
        {
            float p = -halfSize + step * static_cast<float>(i);
            unsigned int start = static_cast<unsigned int>(mesh.vertices.size());
            mesh.vertices.push_back({{-halfSize, 0, p}, {0, 1, 0}});
            mesh.vertices.push_back({{halfSize, 0, p}, {0, 1, 0}});
            mesh.vertices.push_back({{p, 0, -halfSize}, {0, 1, 0}});
            mesh.vertices.push_back({{p, 0, halfSize}, {0, 1, 0}});
            mesh.indices.insert(mesh.indices.end(), {start, start + 1, start + 2, start + 3});
        }
        return mesh;
    }

}
