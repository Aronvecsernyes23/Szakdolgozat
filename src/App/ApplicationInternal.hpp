#pragma once
#include "App/Application.hpp"
#include "Core/Log.hpp"
#include "Geometry/MeshFactory.hpp"
#include <imgui.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cfloat>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace plumbcad
{
    namespace
    {
        constexpr ObjectId EndpointMarkerBaseId = 1000000u;

        // CAD-szerűbb végpont-snap: a korábbi 18 cm túl nagy volt,
        // ezért az elem már messziről elugorhatott a közeli csővégre.
        constexpr float SnapDistanceMeters = 0.05f;

        const char* pipeTypeToText(PipeElementType type)
        {
            switch (type)
            {
                case PipeElementType::StraightPipe: return "StraightPipe";
                case PipeElementType::Elbow90: return "Elbow90";
                case PipeElementType::Elbow45: return "Elbow45";
                case PipeElementType::AlignmentElbow: return "AlignmentElbow";
                case PipeElementType::Tee: return "Tee";
                case PipeElementType::Reducer: return "Reducer";
                case PipeElementType::Valve: return "Valve";
                case PipeElementType::EndCap: return "EndCap";
            }
            return "StraightPipe";
        }

        PipeElementType pipeTypeFromText(const std::string& text)
        {
            if (text == "Elbow90") return PipeElementType::Elbow90;
            if (text == "Elbow45") return PipeElementType::Elbow45;
            if (text == "AlignmentElbow") return PipeElementType::AlignmentElbow;
            if (text == "Tee") return PipeElementType::Tee;
            if (text == "Reducer") return PipeElementType::Reducer;
            if (text == "Valve") return PipeElementType::Valve;
            if (text == "EndCap") return PipeElementType::EndCap;
            return PipeElementType::StraightPipe;
        }

        bool intersectLocalAabb(const MeshData& mesh, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float& outT)
        {
            if (mesh.vertices.empty()) return false;

            glm::vec3 minP{std::numeric_limits<float>::max()};
            glm::vec3 maxP{std::numeric_limits<float>::lowest()};
            for (const auto& v : mesh.vertices)
            {
                minP = glm::min(minP, v.position);
                maxP = glm::max(maxP, v.position);
            }

            // Kicsi ráhagyás, hogy vékony csöveket is könnyebb legyen eltalálni egérrel.
            const glm::vec3 padding{0.04f};
            minP -= padding;
            maxP += padding;

            float tMin = 0.0f;
            float tMax = std::numeric_limits<float>::max();

            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(rayDir[axis]) < 0.000001f)
                {
                    if (rayOrigin[axis] < minP[axis] || rayOrigin[axis] > maxP[axis]) return false;
                }
                else
                {
                    const float invD = 1.0f / rayDir[axis];
                    float t0 = (minP[axis] - rayOrigin[axis]) * invD;
                    float t1 = (maxP[axis] - rayOrigin[axis]) * invD;
                    if (t0 > t1) std::swap(t0, t1);
                    tMin = std::max(tMin, t0);
                    tMax = std::min(tMax, t1);
                    if (tMax < tMin) return false;
                }
            }

            outT = tMin;
            return true;
        }
    }
} // namespace plumbcad
