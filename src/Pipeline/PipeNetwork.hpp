#pragma once
#include "Pipeline/PipeTypes.hpp"
#include <unordered_map>
#include <vector>

namespace plumbcad
{
    class PipeNetwork
    {
    public:
        ObjectId addStraightPipe(float lengthM, float diameterMm);
        ObjectId addElbow90(float bendRadiusM, float diameterMm);
        ObjectId addElbow45(float bendRadiusM, float diameterMm);

        ObjectId addTee(float mainLengthM, float branchLengthM, float diameterMm);
        ObjectId addReducer(float lengthM, float inletDiameterMm, float outletDiameterMm);
        ObjectId addValve(float lengthM, float diameterMm);
        ObjectId addEndCap(float lengthM, float diameterMm);
        PipeElement* find(ObjectId id);
        const PipeElement* find(ObjectId id) const;
        const std::unordered_map<ObjectId, PipeElement>& elements() const { return m_elements; }
        void clear();
        void addLoadedElement(const PipeElement& element);

        const std::vector<PipeTypeDefinition>& catalog() const;
        bool setPipeType(ObjectId id, int typeIndex);
        bool setPipeSize(ObjectId id, int sizeIndex);
        bool setReducerSecondarySize(ObjectId id, int sizeIndex);

        void setPipeLength(ObjectId id, float lengthM);

        void connect(ObjectId a, int pointA, ObjectId b, int pointB);
        bool setIntentionalOpenEnd(ObjectId id, int pointIndex, bool intentional);
        void disconnect(ObjectId id, int pointIndex);
        void disconnectAll(ObjectId id);
        bool removeElement(ObjectId id);

    private:
        void applyCatalogSize(PipeElement& pipe);
        void applyReducerSecondaryCatalogSize(PipeElement& pipe);
        void updateStraightPipeConnectionPoints(PipeElement& pipe);
        void updateElbow90ConnectionPoints(PipeElement& pipe);
        void updateElbow45ConnectionPoints(PipeElement& pipe);
        void updateAlignmentElbowConnectionPoints(PipeElement& pipe);
        void updateTeeConnectionPoints(PipeElement& pipe);
        void updateReducerConnectionPoints(PipeElement& pipe);
        void updateValveConnectionPoints(PipeElement& pipe);
        void updateEndCapConnectionPoints(PipeElement& pipe);
        void updateElbowConnectionPoints(PipeElement& pipe, float angleRadians);

        ObjectId m_nextId{1};
        std::unordered_map<ObjectId, PipeElement> m_elements;
    };
}
