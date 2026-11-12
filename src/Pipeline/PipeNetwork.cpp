#include "Pipeline/PipeNetwork.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace plumbcad
{
    namespace
    {
        float minimumElbowCenterlineRadiusM(float diameterMm)
        {
            return std::max(0.02f, (diameterMm / 1000.0f) * 0.75f);
        }

        float minimumTeeMainLengthM(float diameterMm)
        {
            return std::max(0.06f, (diameterMm / 1000.0f) * 2.0f);
        }

        float minimumTeeBranchLengthM(float diameterMm)
        {
            return std::max(0.04f, (diameterMm / 1000.0f) * 1.5f);
        }

        const std::vector<PipeTypeDefinition> &defaultPipeCatalog()
        {
            static const std::vector<PipeTypeDefinition> catalog = {
                {"PPR PN20 ivóvízcső",
                 "PPR",
                 {{20.0f, 3.4f},
                  {25.0f, 4.2f},
                  {32.0f, 5.4f},
                  {40.0f, 6.7f},
                  {50.0f, 8.3f},
                  {63.0f, 10.5f},
                  {75.0f, 12.5f},
                  {90.0f, 15.0f}}},
                {"PVC-U foul-drain | Table 6", "PVC-U", {{75.0f, 1.9f}, {110.0f, 3.2f}, {160.0f, 4.0f}}},
                {"Rézcső",
                 "Réz",
                 {{15.0f, 1.0f},
                  {18.0f, 1.0f},
                  {22.0f, 1.0f},
                  {28.0f, 1.0f},
                  {35.0f, 1.5f},
                  {42.0f, 1.5f},
                  {54.0f, 2.0f}}}};
            return catalog;
        }
    }

    void PipeNetwork::clear()
    {
        m_elements.clear();
        m_nextId = 1;
    }

    void PipeNetwork::addLoadedElement(const PipeElement &element)
    {
        if (element.id == InvalidObjectId) return;
        PipeElement copy = element;
        std::vector<ConnectionPoint> savedConnections = copy.connections;

        if (copy.type == PipeElementType::StraightPipe)
            updateStraightPipeConnectionPoints(copy);
        else if (copy.type == PipeElementType::Elbow90)
            updateElbow90ConnectionPoints(copy);
        else if (copy.type == PipeElementType::Elbow45)
            updateElbow45ConnectionPoints(copy);
        else if (copy.type == PipeElementType::AlignmentElbow)
            updateAlignmentElbowConnectionPoints(copy);
        else if (copy.type == PipeElementType::Tee)
            updateTeeConnectionPoints(copy);
        else if (copy.type == PipeElementType::Reducer)
        {
            applyReducerSecondaryCatalogSize(copy);
            updateReducerConnectionPoints(copy);
        }
        else if (copy.type == PipeElementType::Valve)
            updateValveConnectionPoints(copy);
        else if (copy.type == PipeElementType::EndCap)
            updateEndCapConnectionPoints(copy);

        const std::size_t restoreCount = std::min(copy.connections.size(), savedConnections.size());
        for (std::size_t i = 0; i < restoreCount; ++i)
        {
            copy.connections[i].connectedElementId = savedConnections[i].connectedElementId;
            copy.connections[i].connectedPointIndex = savedConnections[i].connectedPointIndex;
            copy.connections[i].intentionalOpenEnd = savedConnections[i].intentionalOpenEnd;
        }

        m_elements[copy.id] = copy;
        m_nextId = std::max(m_nextId, copy.id + 1);
    }

    const std::vector<PipeTypeDefinition> &PipeNetwork::catalog() const
    {
        return defaultPipeCatalog();
    }

    void PipeNetwork::applyCatalogSize(PipeElement &pipe)
    {
        const auto &cats = catalog();
        if (cats.empty()) return;
        pipe.pipeTypeIndex = std::clamp(pipe.pipeTypeIndex, 0, static_cast<int>(cats.size()) - 1);
        const auto &type = cats[pipe.pipeTypeIndex];
        if (type.sizes.empty()) return;
        pipe.pipeSizeIndex = std::clamp(pipe.pipeSizeIndex, 0, static_cast<int>(type.sizes.size()) - 1);
        pipe.diameterMm = type.sizes[pipe.pipeSizeIndex].diameterMm;
        pipe.wallThicknessMm = type.sizes[pipe.pipeSizeIndex].wallThicknessMm;
        pipe.material = type.material;
    }

    void PipeNetwork::applyReducerSecondaryCatalogSize(PipeElement &pipe)
    {
        if (pipe.type != PipeElementType::Reducer) return;

        const auto &cats = catalog();

        if (cats.empty()) return;

        pipe.pipeTypeIndex = std::clamp(pipe.pipeTypeIndex, 0, static_cast<int>(cats.size()) - 1);

        const auto &type = cats[pipe.pipeTypeIndex];

        if (type.sizes.empty()) return;

        const int maxIndex = static_cast<int>(type.sizes.size()) - 1;

        pipe.pipeSizeIndex = std::clamp(pipe.pipeSizeIndex, 0, maxIndex);

        if (pipe.secondaryPipeSizeIndex < 0)
        {
            if (maxIndex <= 0)
            {
                pipe.secondaryPipeSizeIndex = pipe.pipeSizeIndex;
            }
            else if (pipe.pipeSizeIndex > 0)
            {
                pipe.secondaryPipeSizeIndex = pipe.pipeSizeIndex - 1;
            }
            else
            {
                pipe.secondaryPipeSizeIndex = 1;
            }
        }

        pipe.secondaryPipeSizeIndex = std::clamp(pipe.secondaryPipeSizeIndex, 0, maxIndex);

        // Ha van választási lehetőség, a szűkítő két oldala ne legyen
        // ugyanaz a katalógusméret.
        if (maxIndex > 0 && pipe.secondaryPipeSizeIndex == pipe.pipeSizeIndex)
        {
            pipe.secondaryPipeSizeIndex = pipe.pipeSizeIndex > 0 ? pipe.pipeSizeIndex - 1 : 1;
        }

        const PipeCatalogSize &secondary = type.sizes[pipe.secondaryPipeSizeIndex];

        pipe.secondaryDiameterMm = secondary.diameterMm;

        pipe.secondaryWallThicknessMm = secondary.wallThicknessMm;
    }

    void PipeNetwork::updateStraightPipeConnectionPoints(PipeElement &pipe)
    {
        if (pipe.connections.size() < 2)
        {
            pipe.connections.resize(2);
        }
        pipe.connections[0].localPosition = {0.0f, 0.0f, -pipe.lengthM * 0.5f};
        pipe.connections[1].localPosition = {0.0f, 0.0f, pipe.lengthM * 0.5f};
    }

    void PipeNetwork::updateElbowConnectionPoints(PipeElement &pipe, float angleRadians)
    {
        if (pipe.connections.size() < 2)
        {
            pipe.connections.resize(2);
        }
        const float r = std::max(pipe.bendRadiusM, minimumElbowCenterlineRadiusM(pipe.diameterMm));

        pipe.connections[0].localPosition = {r, 0.0f, 0.0f};
        pipe.connections[1].localPosition = {r * std::cos(angleRadians), 0.0f, r * std::sin(angleRadians)};
    }

    void PipeNetwork::updateElbow90ConnectionPoints(PipeElement &pipe)
    {
        updateElbowConnectionPoints(pipe, 1.57079632679f);
    }

    void PipeNetwork::updateElbow45ConnectionPoints(PipeElement &pipe)
    {
        updateElbowConnectionPoints(pipe, 0.78539816339f);
    }

    void PipeNetwork::updateAlignmentElbowConnectionPoints(PipeElement &pipe)
    {
        const float radius = std::max(pipe.bendRadiusM, 0.1f);

        const float angle = std::clamp(pipe.lengthM / radius, 0.001f, 3.12413936107f);

        updateElbowConnectionPoints(pipe, angle);
    }

    void PipeNetwork::updateTeeConnectionPoints(PipeElement &pipe)
    {
        if (pipe.connections.size() < 3)
        {
            pipe.connections.resize(3);
        }
        const float mainLength = std::max(pipe.lengthM, minimumTeeMainLengthM(pipe.diameterMm));

        const float branchLength = std::max(pipe.bendRadiusM, minimumTeeBranchLengthM(pipe.diameterMm));
        pipe.connections[0].localPosition = {0.0f, 0.0f, -mainLength * 0.5f};
        pipe.connections[1].localPosition = {0.0f, 0.0f, mainLength * 0.5f};
        pipe.connections[2].localPosition = {branchLength, 0.0f, 0.0f};
    }

    void PipeNetwork::updateReducerConnectionPoints(PipeElement &pipe)
    {
        if (pipe.connections.size() < 2) pipe.connections.resize(2);

        const float length = std::max(pipe.lengthM, 0.08f);

        pipe.connections[0].localPosition = glm::vec3{0.0f, 0.0f, -length * 0.5f};

        pipe.connections[1].localPosition = glm::vec3{0.0f, 0.0f, length * 0.5f};
    }

    void PipeNetwork::updateValveConnectionPoints(PipeElement &pipe)
    {
        if (pipe.connections.size() < 2) pipe.connections.resize(2);

        const float length = std::max(pipe.lengthM, 0.10f);

        pipe.connections[0].localPosition = glm::vec3{0.0f, 0.0f, -length * 0.5f};

        pipe.connections[1].localPosition = glm::vec3{0.0f, 0.0f, length * 0.5f};
    }

    void PipeNetwork::updateEndCapConnectionPoints(PipeElement &pipe)
    {
        // A végzárónak pontosan egy fizikai csatlakozása van.
        pipe.connections.resize(1);

        const float length = std::max(pipe.lengthM, 0.06f);

        // A nyitott csatlakozás a -Z oldalon van; a +Z oldal zárt.
        pipe.connections[0].localPosition = glm::vec3{0.0f, 0.0f, -length * 0.5f};
    }

    ObjectId PipeNetwork::addElbow90(float bendRadiusM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::Elbow90;
        p.drainageCheckEnabled = false;
        p.bendRadiusM = std::max(bendRadiusM, 0.02f);

        // A megadott átmérőhöz legközelebbi katalógusméretet választjuk ki.
        float bestDiff = 999999.0f;
        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];
            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);

        p.bendRadiusM = std::max(p.bendRadiusM, minimumElbowCenterlineRadiusM(p.diameterMm));

        p.lengthM = p.bendRadiusM * 1.57079632679f;

        updateElbow90ConnectionPoints(p);
        p.connections[0].connectedElementId = InvalidObjectId;
        p.connections[0].connectedPointIndex = -1;
        p.connections[1].connectedElementId = InvalidObjectId;
        p.connections[1].connectedPointIndex = -1;
        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addElbow45(float bendRadiusM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::Elbow45;
        p.drainageCheckEnabled = false;
        p.bendRadiusM = std::max(bendRadiusM, 0.02f);

        float bestDiff = 999999.0f;
        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];
            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);

        p.bendRadiusM = std::max(p.bendRadiusM, minimumElbowCenterlineRadiusM(p.diameterMm));

        p.lengthM = p.bendRadiusM * 0.78539816339f;

        updateElbow45ConnectionPoints(p);
        p.connections[0].connectedElementId = InvalidObjectId;
        p.connections[0].connectedPointIndex = -1;
        p.connections[1].connectedElementId = InvalidObjectId;
        p.connections[1].connectedPointIndex = -1;
        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addTee(float mainLengthM, float branchLengthM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::Tee;
        p.drainageCheckEnabled = false;
        p.lengthM = std::max(mainLengthM, 0.06f);
        p.bendRadiusM = std::max(branchLengthM, 0.04f);

        float bestDiff = 999999.0f;
        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];
            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);

        p.lengthM = std::max(p.lengthM, minimumTeeMainLengthM(p.diameterMm));

        p.bendRadiusM = std::max(p.bendRadiusM, minimumTeeBranchLengthM(p.diameterMm));

        updateTeeConnectionPoints(p);
        for (auto &connection : p.connections)
        {
            connection.connectedElementId = InvalidObjectId;
            connection.connectedPointIndex = -1;
        }
        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addReducer(float lengthM, float inletDiameterMm, float outletDiameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::Reducer;
        p.drainageCheckEnabled = false;
        p.lengthM = std::max(lengthM, 0.08f);

        float bestPrimaryDiff = std::numeric_limits<float>::max();

        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];

            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - inletDiameterMm);

                if (diff < bestPrimaryDiff)
                {
                    bestPrimaryDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);

        const auto &type = catalog()[p.pipeTypeIndex];

        float bestSecondaryDiff = std::numeric_limits<float>::max();

        for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
        {
            const float diff = std::abs(type.sizes[sizeIndex].diameterMm - outletDiameterMm);

            if (diff < bestSecondaryDiff)
            {
                bestSecondaryDiff = diff;
                p.secondaryPipeSizeIndex = sizeIndex;
            }
        }

        applyReducerSecondaryCatalogSize(p);
        updateReducerConnectionPoints(p);

        for (ConnectionPoint &point : p.connections)
        {
            point.connectedElementId = InvalidObjectId;
            point.connectedPointIndex = -1;
            point.intentionalOpenEnd = false;
        }

        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addValve(float lengthM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::Valve;
        p.drainageCheckEnabled = false;
        p.lengthM = std::max(lengthM, 0.10f);

        float bestDiff = std::numeric_limits<float>::max();

        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];

            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);

                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);
        updateValveConnectionPoints(p);

        for (ConnectionPoint &point : p.connections)
        {
            point.connectedElementId = InvalidObjectId;
            point.connectedPointIndex = -1;
            point.intentionalOpenEnd = false;
        }

        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addEndCap(float lengthM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::EndCap;
        p.drainageCheckEnabled = false;
        p.lengthM = std::max(lengthM, 0.06f);

        float bestDiff = std::numeric_limits<float>::max();

        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];

            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);

                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);
        updateEndCapConnectionPoints(p);

        p.connections[0].connectedElementId = InvalidObjectId;
        p.connections[0].connectedPointIndex = -1;
        p.connections[0].intentionalOpenEnd = false;

        m_elements[p.id] = p;
        return p.id;
    }

    ObjectId PipeNetwork::addStraightPipe(float lengthM, float diameterMm)
    {
        PipeElement p;
        p.id = m_nextId++;
        p.type = PipeElementType::StraightPipe;
        p.lengthM = std::clamp(lengthM, MinStraightPipeLengthM, MaxStraightPipeLengthM);

        // A megadott átmérőhöz legközelebbi katalógusméretet választjuk ki.
        float bestDiff = 999999.0f;
        for (int typeIndex = 0; typeIndex < static_cast<int>(catalog().size()); ++typeIndex)
        {
            const auto &type = catalog()[typeIndex];
            for (int sizeIndex = 0; sizeIndex < static_cast<int>(type.sizes.size()); ++sizeIndex)
            {
                const float diff = std::abs(type.sizes[sizeIndex].diameterMm - diameterMm);
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    p.pipeTypeIndex = typeIndex;
                    p.pipeSizeIndex = sizeIndex;
                }
            }
        }

        applyCatalogSize(p);
        updateStraightPipeConnectionPoints(p);
        p.connections[0].connectedElementId = InvalidObjectId;
        p.connections[0].connectedPointIndex = -1;
        p.connections[1].connectedElementId = InvalidObjectId;
        p.connections[1].connectedPointIndex = -1;
        m_elements[p.id] = p;
        return p.id;
    }

    PipeElement *PipeNetwork::find(ObjectId id)
    {
        auto it = m_elements.find(id);
        return it == m_elements.end() ? nullptr : &it->second;
    }

    const PipeElement *PipeNetwork::find(ObjectId id) const
    {
        auto it = m_elements.find(id);
        return it == m_elements.end() ? nullptr : &it->second;
    }

    bool PipeNetwork::setPipeType(ObjectId id, int typeIndex)
    {
        PipeElement *pipe = find(id);

        if (!pipe) return false;

        const auto &cats = catalog();

        if (cats.empty()) return false;

        const int newTypeIndex = std::clamp(typeIndex, 0, static_cast<int>(cats.size()) - 1);

        const PipeTypeDefinition &newType = cats[newTypeIndex];

        if (newType.sizes.empty()) return false;

        const float oldPrimaryDiameter = pipe->diameterMm;

        const float oldSecondaryDiameter = pipe->secondaryDiameterMm;

        auto closestSizeIndex = [&](float diameterMm) {
            int bestIndex = 0;

            float bestDistance = std::numeric_limits<float>::max();

            for (int i = 0; i < static_cast<int>(newType.sizes.size()); ++i)
            {
                const float distance = std::abs(newType.sizes[i].diameterMm - diameterMm);

                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestIndex = i;
                }
            }

            return bestIndex;
        };

        pipe->pipeTypeIndex = newTypeIndex;

        pipe->pipeSizeIndex = closestSizeIndex(oldPrimaryDiameter);

        applyCatalogSize(*pipe);

        if (pipe->type == PipeElementType::Reducer)
        {
            pipe->secondaryPipeSizeIndex =
                closestSizeIndex(oldSecondaryDiameter > 0.0f ? oldSecondaryDiameter : oldPrimaryDiameter);

            if (newType.sizes.size() > 1 && pipe->secondaryPipeSizeIndex == pipe->pipeSizeIndex)
            {
                pipe->secondaryPipeSizeIndex = pipe->pipeSizeIndex > 0 ? pipe->pipeSizeIndex - 1 : 1;
            }

            applyReducerSecondaryCatalogSize(*pipe);
        }

        return true;
    }

    bool PipeNetwork::setPipeSize(ObjectId id, int sizeIndex)
    {
        PipeElement *pipe = find(id);
        if (!pipe) return false;
        const auto &cats = catalog();
        if (cats.empty()) return false;
        pipe->pipeTypeIndex = std::clamp(pipe->pipeTypeIndex, 0, static_cast<int>(cats.size()) - 1);
        pipe->pipeSizeIndex =
            std::clamp(sizeIndex, 0, static_cast<int>(cats[pipe->pipeTypeIndex].sizes.size()) - 1);
        applyCatalogSize(*pipe);

        if (pipe->type == PipeElementType::Reducer)
        {
            applyReducerSecondaryCatalogSize(*pipe);
        }

        return true;
    }

    bool PipeNetwork::setReducerSecondarySize(ObjectId id, int sizeIndex)
    {
        PipeElement *pipe = find(id);

        if (!pipe || pipe->type != PipeElementType::Reducer)
        {
            return false;
        }

        const auto &cats = catalog();

        if (pipe->pipeTypeIndex < 0 || pipe->pipeTypeIndex >= static_cast<int>(cats.size()))
        {
            return false;
        }

        const auto &type = cats[pipe->pipeTypeIndex];

        if (type.sizes.empty()) return false;

        pipe->secondaryPipeSizeIndex = std::clamp(sizeIndex, 0, static_cast<int>(type.sizes.size()) - 1);

        applyReducerSecondaryCatalogSize(*pipe);

        return true;
    }

    void PipeNetwork::setPipeLength(ObjectId id, float lengthM)
    {
        PipeElement *pipe = find(id);
        if (!pipe) return;
        if (pipe->type == PipeElementType::StraightPipe)
        {
            pipe->lengthM = std::clamp(lengthM, MinStraightPipeLengthM, MaxStraightPipeLengthM);
        }
        else
        {
            pipe->lengthM = std::max(lengthM, 0.1f);
        }

        if (pipe->type == PipeElementType::StraightPipe)
            updateStraightPipeConnectionPoints(*pipe);
        else if (pipe->type == PipeElementType::Elbow90)
        {
            pipe->bendRadiusM = pipe->lengthM / 1.57079632679f;
            updateElbow90ConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::Elbow45)
        {
            pipe->bendRadiusM = pipe->lengthM / 0.78539816339f;
            updateElbow45ConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::AlignmentElbow)
        {
            updateAlignmentElbowConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::Tee)
        {
            updateTeeConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::Reducer)
        {
            updateReducerConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::Valve)
        {
            updateValveConnectionPoints(*pipe);
        }
        else if (pipe->type == PipeElementType::EndCap)
        {
            updateEndCapConnectionPoints(*pipe);
        }
    }

    void PipeNetwork::connect(ObjectId a, int pointA, ObjectId b, int pointB)
    {
        auto *ea = find(a);
        auto *eb = find(b);
        if (!ea || !eb) return;
        if (pointA < 0 || pointA >= static_cast<int>(ea->connections.size())) return;
        if (pointB < 0 || pointB >= static_cast<int>(eb->connections.size())) return;
        ea->connections[pointA].connectedElementId = b;
        ea->connections[pointA].connectedPointIndex = pointB;
        ea->connections[pointA].intentionalOpenEnd = false;

        eb->connections[pointB].connectedElementId = a;
        eb->connections[pointB].connectedPointIndex = pointA;
        eb->connections[pointB].intentionalOpenEnd = false;
    }

    bool PipeNetwork::setIntentionalOpenEnd(ObjectId id, int pointIndex, bool intentional)
    {
        PipeElement *element = find(id);
        if (!element) return false;

        if (pointIndex < 0 || pointIndex >= static_cast<int>(element->connections.size()))
        {
            return false;
        }

        ConnectionPoint &point = element->connections[pointIndex];

        // Kapcsolt pont nem jelölhető szándékosan nyitottnak.
        if (!point.isOpen())
        {
            point.intentionalOpenEnd = false;
            return !intentional;
        }

        point.intentionalOpenEnd = intentional;

        return true;
    }

    void PipeNetwork::disconnect(ObjectId id, int pointIndex)
    {
        PipeElement *element = find(id);
        if (!element) return;
        if (pointIndex < 0 || pointIndex >= static_cast<int>(element->connections.size())) return;

        ConnectionPoint &point = element->connections[pointIndex];
        const ObjectId otherId = point.connectedElementId;
        const int otherPointIndex = point.connectedPointIndex;

        point.connectedElementId = InvalidObjectId;
        point.connectedPointIndex = -1;
        point.intentionalOpenEnd = false;

        PipeElement *other = find(otherId);
        if (!other) return;
        if (otherPointIndex < 0 || otherPointIndex >= static_cast<int>(other->connections.size())) return;

        ConnectionPoint &otherPoint = other->connections[otherPointIndex];
        if (otherPoint.connectedElementId == id && otherPoint.connectedPointIndex == pointIndex)
        {
            otherPoint.connectedElementId = InvalidObjectId;
            otherPoint.connectedPointIndex = -1;
            otherPoint.intentionalOpenEnd = false;
        }
    }

    void PipeNetwork::disconnectAll(ObjectId id)
    {
        PipeElement *element = find(id);
        if (!element) return;

        const int count = static_cast<int>(element->connections.size());
        for (int i = 0; i < count; ++i)
        {
            disconnect(id, i);
        }
    }

    bool PipeNetwork::removeElement(ObjectId id)
    {
        auto it = m_elements.find(id);

        if (it == m_elements.end()) return false;

        // Először minden szabályos, kölcsönös kapcsolatot bontunk.
        // A szomszédos elemek megmaradnak, az érintett végpontjuk nyitott lesz.
        disconnectAll(id);

        // Ezután az elemet eltávolítjuk a hálózatból.
        m_elements.erase(id);

        // Sérült / régi projektből maradt egyoldalú hivatkozás se mutathasson
        // többé a törölt elemre.
        for (auto &[otherId, element] : m_elements)
        {
            (void)otherId;

            for (ConnectionPoint &point : element.connections)
            {
                if (point.connectedElementId != id) continue;

                point.connectedElementId = InvalidObjectId;
                point.connectedPointIndex = -1;
                point.intentionalOpenEnd = false;
            }
        }

        return true;
    }

}
