#pragma once
#include "Geometry/MeshData.hpp"
#include <glm/glm.hpp>

namespace plumbcad
{
    class MeshFactory
    {
    public:
        static MeshData makeBox(const glm::vec3& size);
        static MeshData makeCylinder(float radius, float length, int segments = 32);
        static MeshData makeArrow(
            float length = 1.0f,
            float shaftRadius = 0.025f,
            float headRadius = 0.075f,
            float headLength = 0.22f,
            int segments = 24);
        static MeshData makeEndpointRing(float radius, float thickness = 0.006f, int segments = 48);
        static MeshData makeElbow(float tubeRadius, float bendRadius, float angleRadians, int arcSegments = 24, int radialSegments = 24);
        static MeshData makeElbow90(float tubeRadius, float bendRadius, int arcSegments = 24, int radialSegments = 24);
        static MeshData makeElbow45(float tubeRadius, float bendRadius, int arcSegments = 18, int radialSegments = 24);
        static MeshData makeTee(float tubeRadius, float mainLength = 0.40f, float branchLength = 0.20f, int segments = 32);
        static MeshData makeReducer(float inletRadius, float outletRadius, float length = 0.25f, int segments = 32);
        static MeshData makeValve(float tubeRadius, float length = 0.24f, int segments = 32);
        static MeshData makeEndCap(float tubeRadius, float length = 0.10f, int segments = 32);
        static MeshData makeGrid(float halfSize = 10.0f, int lines = 20);
    };
}
