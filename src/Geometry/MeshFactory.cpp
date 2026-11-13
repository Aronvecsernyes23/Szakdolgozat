#include "Geometry/MeshFactory.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <glm/gtc/constants.hpp>
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

        struct Section
        {
            float axial;
            float radius;
        };

        MeshData lathe(std::initializer_list<Section> profile, int sides)
        {
            MeshData surface;
            for (auto first = profile.begin(); first + 1 != profile.end(); ++first)
            {
                const Section& next = *(first + 1);
                const float axialSpan = next.axial - first->axial;
                const float radialDrop = first->radius - next.radius;
                const auto base = static_cast<unsigned int>(surface.vertices.size());
                for (int i = 0; i <= sides; ++i)
                {
                    const float angle = glm::two_pi<float>() * static_cast<float>(i % sides) / static_cast<float>(sides);
                    const float c = std::cos(angle);
                    const float s = std::sin(angle);
                    const glm::vec3 normal = glm::normalize(glm::vec3{axialSpan * c, axialSpan * s, radialDrop});
                    surface.vertices.push_back({{first->radius * c, first->radius * s, first->axial}, normal});
                    surface.vertices.push_back({{next.radius * c, next.radius * s, next.axial}, normal});
                }
                for (int i = 0; i < sides; ++i)
                {
                    const unsigned int a = base + static_cast<unsigned int>(i * 2);
                    surface.indices.insert(surface.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
                }
            }
            return surface;
        }

        void appendRoutingRing(MeshData& mesh, const glm::vec3& center, const glm::vec3& radial,
                               float radius, int segments)
        {
            for (int i = 0; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = t * 2.0f * 3.14159265359f;
                mesh.routingBoundsVertices.push_back(
                    center + radius * (radial * std::cos(angle) + glm::vec3{0.0f, std::sin(angle), 0.0f}));
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

    MeshData MeshFactory::makeBeveledBox(const glm::vec3& size, float bevelRatio)
    {
        const glm::vec3 half = glm::max(size, glm::vec3{0.02f}) * 0.5f;
        const float bevel = std::min({half.x, half.y, half.z}) * bevelRatio;
        const glm::vec3 inner = half - glm::vec3{bevel};
        MeshData mesh;

        const auto face = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                              const glm::vec3& d, const glm::vec3& normal)
        {
            const auto base = static_cast<unsigned int>(mesh.vertices.size());
            for (const auto& point : {a, b, c, d}) mesh.vertices.push_back({point, normal});
            if (glm::dot(glm::cross(b - a, c - a), normal) > 0.0f)
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            else
                mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        };

        for (int axis = 0; axis < 3; ++axis)
        {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            for (float side : {-1.0f, 1.0f})
            {
                glm::vec3 center{0.0f}, du{0.0f}, dv{0.0f}, normal{0.0f};
                center[axis] = half[axis] * side;
                normal[axis] = side;
                du[u] = inner[u];
                dv[v] = inner[v];
                face(center - du - dv, center + du - dv, center + du + dv, center - du + dv, normal);
            }

            for (float sideU : {-1.0f, 1.0f})
            {
                for (float sideV : {-1.0f, 1.0f})
                {
                    glm::vec3 a{0.0f}, d{0.0f}, along{0.0f}, normal{0.0f};
                    a[axis] = d[axis] = -inner[axis];
                    a[u] = half[u] * sideU;
                    a[v] = inner[v] * sideV;
                    d[u] = inner[u] * sideU;
                    d[v] = half[v] * sideV;
                    along[axis] = 2.0f * inner[axis];
                    normal[u] = sideU;
                    normal[v] = sideV;
                    face(a, a + along, d + along, d, glm::normalize(normal));
                }
            }
        }

        for (float x : {-1.0f, 1.0f})
        {
            for (float y : {-1.0f, 1.0f})
            {
                for (float z : {-1.0f, 1.0f})
                {
                    const glm::vec3 sign{x, y, z};
                    mesh.routingBoundsVertices.push_back(half * sign);
                    const auto base = static_cast<unsigned int>(mesh.vertices.size());
                    const auto normal = glm::normalize(sign);
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        auto point = inner * sign;
                        point[axis] = half[axis] * sign[axis];
                        mesh.vertices.push_back({point, normal});
                    }
                    if (x * y * z > 0.0f) mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
                    else mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1});
                }
            }
        }
        return mesh;
    }

    MeshData MeshFactory::makePipe(float radius, float wallThickness, float length, int segments)
    {
        radius = std::max(radius, 0.001f);
        length = std::max(length, 0.0001f);
        const float half = length * 0.5f;
        const float bore = std::clamp(radius - wallThickness, radius * 0.15f, radius * 0.96f);
        const float bevel = std::min({0.24f * (radius - bore), length * 0.05f, 0.0015f});
        const float sleeve = std::min(radius * 2.0f, length * 0.45f);
        return lathe({
            {-half + sleeve, bore}, {-half, bore}, {-half, radius - bevel},
            {-half + bevel, radius}, {half - bevel, radius}, {half, radius - bevel},
            {half, bore}, {half - sleeve, bore}
        }, std::max(segments, 24));
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
            mesh.indices.insert(mesh.indices.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
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
        radius = std::max(radius, 0.005f);
        thickness = std::clamp(thickness, radius * 0.18f, radius * 0.45f);
        const float inner = radius - thickness;
        const float depth = std::clamp(thickness * 0.38f, 0.0009f, 0.003f);
        return lathe({{0.0f, inner}, {0.0f, radius}, {depth, radius}, {depth, inner}, {0.0f, inner}},
                     std::max(segments, 16));
    }

    MeshData MeshFactory::makeElbow(float tubeRadius, float bendRadius, float angleRadians, int arcSegments,
                                    int radialSegments)
    {
        MeshData mesh;
        bendRadius = std::max(bendRadius, tubeRadius * 1.5f);
        angleRadians = std::clamp(angleRadians, 0.001f, 3.14159265359f);
        arcSegments = std::max(arcSegments, 4);
        radialSegments = std::max(radialSegments, 8);

        // Keep the original sampled footprint for A* clearance.
        for (int i = 0; i <= arcSegments; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>(arcSegments) * angleRadians;
            const glm::vec3 radial{std::cos(angle), 0.0f, std::sin(angle)};
            appendRoutingRing(mesh, {bendRadius * radial.x, 0.0f, bendRadius * radial.z}, radial,
                              tubeRadius, radialSegments);
        }

        // Sweep a collar profile along the elbow without moving either connection plane.
        radialSegments = std::max(24, radialSegments + radialSegments % 2);
        const float collar = std::min(angleRadians * 0.23f, 0.78f * tubeRadius / bendRadius);
        const float port = 1.07f * tubeRadius;
        const float shoulder = 1.16f * tubeRadius;
        std::vector<Section> sections{
            {0.0f, port}, {0.22f * collar, shoulder}, {0.63f * collar, shoulder}, {collar, tubeRadius}
        };
        for (int i = 1; i < arcSegments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(arcSegments);
            sections.push_back({glm::mix(collar, angleRadians - collar, t), tubeRadius});
        }
        sections.insert(sections.end(), {
            {angleRadians - collar, tubeRadius}, {angleRadians - 0.63f * collar, shoulder},
            {angleRadians - 0.22f * collar, shoulder}, {angleRadians, port}
        });

        for (std::size_t i = 1; i < sections.size(); ++i)
        {
            const auto& first = sections[i - 1];
            const auto& next = sections[i];
            const float slope = (next.radius - first.radius) / (next.axial - first.axial);
            const auto base = static_cast<unsigned int>(mesh.vertices.size());
            for (int j = 0; j <= radialSegments; ++j)
            {
                const float angle = glm::two_pi<float>() * static_cast<float>(j % radialSegments) /
                                    static_cast<float>(radialSegments);
                const float c = std::cos(angle);
                const float s = std::sin(angle);
                for (const Section& section : {first, next})
                {
                    const glm::vec3 radial{std::cos(section.axial), 0.0f, std::sin(section.axial)};
                    const glm::vec3 tangent{-radial.z, 0.0f, radial.x};
                    const glm::vec3 outward = radial * c + glm::vec3{0.0f, s, 0.0f};
                    const glm::vec3 normal = glm::normalize(
                        (bendRadius + section.radius * c) * outward - slope * tangent);
                    mesh.vertices.push_back({bendRadius * radial + section.radius * outward, normal});
                }
            }
            for (int j = 0; j < radialSegments; ++j)
            {
                const auto a = base + static_cast<unsigned int>(2 * j);
                mesh.indices.insert(mesh.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
            }
        }

        const float bore = 0.82f * tubeRadius;
        const float inset = 0.40f * collar * bendRadius;
        const auto rim = lathe({{inset, bore}, {0.0f, bore}, {0.0f, port}}, radialSegments);
        appendMesh(mesh, rim, glm::translate(glm::mat4{1.0f}, glm::vec3{bendRadius, 0.0f, 0.0f}));
        const glm::vec3 end{bendRadius * std::cos(angleRadians), 0.0f, bendRadius * std::sin(angleRadians)};
        const auto endTransform = glm::rotate(glm::translate(glm::mat4{1.0f}, end),
                                              glm::pi<float>() - angleRadians, glm::vec3{0.0f, 1.0f, 0.0f});
        appendMesh(mesh, rim, endTransform);
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
        const float r = tubeRadius;
        const float half = 0.5f * std::max(mainLength, 4.0f * r);
        const float branch = std::max(branchLength, 3.0f * r);
        const float bore = 0.82f * r;
        const int sides = std::max(segments, 24);
        MeshData mesh = lathe({
            {-0.83f * half, bore}, {-half, bore}, {-half, 1.04f * r}, {-0.91f * half, 1.17f * r},
            {-0.74f * half, 1.17f * r}, {-0.60f * half, r}, {-0.32f * half, 1.12f * r},
            {0.32f * half, 1.12f * r}, {0.60f * half, r}, {0.74f * half, 1.17f * r},
            {0.91f * half, 1.17f * r}, {half, 1.04f * r}, {half, bore}, {0.83f * half, bore}
        }, sides);
        const MeshData outlet = lathe({
            {0.26f * r, 1.03f * r}, {0.90f * r, 1.16f * r}, {0.58f * branch, r},
            {0.75f * branch, 1.17f * r}, {0.92f * branch, 1.17f * r}, {branch, 1.04f * r},
            {branch, bore}, {0.82f * branch, bore}
        }, sides);
        const auto outward = glm::rotate(glm::mat4{1.0f}, glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
        appendMesh(mesh, outlet, outward);

        const glm::vec3 x{1.0f, 0.0f, 0.0f}, z{0.0f, 0.0f, 1.0f};
        appendRoutingRing(mesh, -half * z, x, r, segments);
        appendRoutingRing(mesh, half * z, x, r, segments);
        appendRoutingRing(mesh, {}, z, r, segments);
        appendRoutingRing(mesh, branch * x, z, r, segments);
        return mesh;
    }

    MeshData MeshFactory::makeReducer(float inletRadius, float outletRadius, float length, int segments)
    {
        const float inlet = std::max(inletRadius, 0.003f);
        const float outlet = std::max(outletRadius, 0.003f);
        const float half = 0.5f * std::max(length, 0.08f);
        segments = std::max(segments, 16);
        const float inletBore = 0.82f * inlet;
        const float outletBore = 0.82f * outlet;
        MeshData mesh = lathe({
            {-0.80f * half, inletBore}, {-half, inletBore}, {-half, 1.04f * inlet},
            {-0.90f * half, 1.15f * inlet}, {-0.69f * half, 1.15f * inlet}, {-0.55f * half, inlet},
            {-0.33f * half, glm::mix(inlet, outlet, 0.12f)},
            {0.33f * half, glm::mix(inlet, outlet, 0.88f)}, {0.55f * half, outlet},
            {0.69f * half, 1.15f * outlet}, {0.90f * half, 1.15f * outlet},
            {half, 1.04f * outlet}, {half, outletBore}, {0.80f * half, outletBore}
        }, std::max(segments, 24));
        appendRoutingRing(mesh, {0.0f, 0.0f, -half}, {1.0f, 0.0f, 0.0f}, inlet, segments);
        appendRoutingRing(mesh, {0.0f, 0.0f, half}, {1.0f, 0.0f, 0.0f}, outlet, segments);
        return mesh;
    }

    MeshData MeshFactory::makeValve(float tubeRadius, float length, int segments)
    {
        tubeRadius = std::max(tubeRadius, 0.003f);
        length = std::max(length, 0.10f);
        segments = std::max(segments, 24);

        // Chamfered collars and the cast body share one continuous surface.
        const float half = length * 0.5f;
        const float r = tubeRadius;
        const float boreRadius = 0.83f * r;
        const float socketDepth = std::min(0.55f * r, 0.13f * length);
        MeshData mesh = lathe({
            {-half + socketDepth, boreRadius}, {-half, boreRadius},
            {-half, r}, {-0.94f * half, r}, {-0.85f * half, 1.26f * r},
            {-0.72f * half, 1.26f * r}, {-0.64f * half, 1.08f * r},
            {-0.52f * half, 1.24f * r}, {-0.31f * half, 1.48f * r},
            {-0.14f * half, 1.58f * r}, {0.14f * half, 1.58f * r},
            {0.31f * half, 1.48f * r}, {0.52f * half, 1.24f * r},
            {0.64f * half, 1.08f * r}, {0.72f * half, 1.26f * r},
            {0.85f * half, 1.26f * r}, {0.94f * half, r}, {half, r},
            {half, boreRadius}, {half - socketDepth, boreRadius}
        }, segments);

        // A bonnet, exposed stem and capped hub sit above the flow axis.
        const float bonnetRadius = std::min(0.88f * r, 0.54f * half);
        const float wheelHeight = 2.82f * r;
        const float wheelRadius = std::min(1.62f * r, 0.84f * half);
        const float rimRadius = 0.12f * wheelRadius;
        const float hubRadius = 0.25f * wheelRadius;
        const float hubTop = wheelHeight + rimRadius * 1.15f;
        MeshData bonnet = lathe({
            {1.08f * r, 0.83f * bonnetRadius}, {1.45f * r, bonnetRadius},
            {1.58f * r, bonnetRadius}, {1.70f * r, 0.90f * bonnetRadius},
            {1.70f * r, 0.58f * bonnetRadius}, {1.98f * r, 0.58f * bonnetRadius},
            {2.10f * r, 0.44f * bonnetRadius}, {2.10f * r, 0.24f * bonnetRadius},
            {wheelHeight - rimRadius, 0.24f * bonnetRadius},
            {wheelHeight - rimRadius, hubRadius},
            {hubTop - 0.3f * rimRadius, hubRadius}, {hubTop, 0.78f * hubRadius}
        }, segments);
        appendDisc(bonnet, 0.78f * hubRadius, hubTop, 1.0f, segments);
        const auto upright = glm::rotate(glm::mat4{1.0f}, -glm::half_pi<float>(), glm::vec3{1.0f, 0.0f, 0.0f});
        appendMesh(mesh, bonnet, upright);

        constexpr int rimSides = 12;
        const auto rimBase = static_cast<unsigned int>(mesh.vertices.size());
        for (int i = 0; i < segments; ++i)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
            const glm::vec3 radial{std::cos(angle), 0.0f, std::sin(angle)};
            const glm::vec3 center = radial * wheelRadius + glm::vec3{0.0f, wheelHeight, 0.0f};
            for (int j = 0; j < rimSides; ++j)
            {
                const float round = glm::two_pi<float>() * static_cast<float>(j) / static_cast<float>(rimSides);
                const glm::vec3 normal = radial * std::cos(round) + glm::vec3{0.0f, std::sin(round), 0.0f};
                mesh.vertices.push_back({center + rimRadius * normal, normal});

                const auto a = rimBase + static_cast<unsigned int>(i * rimSides + j);
                const auto b = rimBase + static_cast<unsigned int>(i * rimSides + (j + 1) % rimSides);
                const auto c = rimBase + static_cast<unsigned int>(((i + 1) % segments) * rimSides + j);
                const auto d = rimBase + static_cast<unsigned int>(((i + 1) % segments) * rimSides + (j + 1) % rimSides);
                mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
            }
        }

        const MeshData spoke = lathe({
            {0.70f * hubRadius, 0.76f * rimRadius},
            {0.53f * wheelRadius, 0.56f * rimRadius},
            {wheelRadius, 0.70f * rimRadius}
        }, rimSides);
        const auto wheelCenter = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, wheelHeight, 0.0f});
        for (int i = 0; i < 3; ++i)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / 3.0f;
            appendMesh(mesh, spoke, glm::rotate(wheelCenter, angle, glm::vec3{0.0f, 1.0f, 0.0f}));
        }

        return mesh;
    }

    MeshData MeshFactory::makeEndCap(float tubeRadius, float length, int segments)
    {
        const float r = std::max(tubeRadius, 0.003f);
        length = std::max(length, 0.06f);
        segments = std::max(segments, 16);
        const float half = length * 0.5f;
        const float bore = 0.82f * r;
        const int sides = std::max(segments, 24);
        MeshData mesh = lathe({
            {-0.55f * half, bore}, {-half, bore}, {-half, 1.04f * r}, {-0.83f * half, 1.18f * r},
            {-0.50f * half, 1.18f * r}, {-0.32f * half, 1.08f * r}, {0.34f * half, 1.08f * r},
            {0.58f * half, 1.01f * r}, {0.79f * half, 0.86f * r}, {0.94f * half, 0.61f * r},
            {half, 0.32f * r}
        }, sides);
        appendDisc(mesh, 0.32f * r, half, 1.0f, sides);

        const float collarLength = std::min(length * 0.32f, std::max(r * 0.8f, 0.018f));
        const glm::vec3 x{1.0f, 0.0f, 0.0f};
        appendRoutingRing(mesh, {0.0f, 0.0f, -half}, x, 1.08f * r, segments);
        appendRoutingRing(mesh, {0.0f, 0.0f, half}, x, 1.08f * r, segments);
        appendRoutingRing(mesh, {0.0f, 0.0f, -half}, x, 1.18f * r, segments);
        appendRoutingRing(mesh, {0.0f, 0.0f, -half + collarLength}, x, 1.18f * r, segments);
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
