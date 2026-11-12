#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    bool Application::saveProject(const std::string &path) const
    {
        std::ofstream out(path);
        if (!out) return false;

        out << "PLUMBCAD_PROJECT 11\n";
        out << "PRESSURE_TEST_SETTINGS " << m_pressureDesignPressureBar << ' ' << m_pressureTestPressureBar << ' '
            << m_pressureHoldTimeMin << ' ' << m_pressureInitialMeasuredPressureBar << ' '
            << m_pressureFinalMeasuredPressureBar << ' ' << m_pressureAllowedDropBar << "\n";

        out << "VALIDATION_PATH " << m_validationStartId << ' ' << m_validationStartEndpoint << ' '
            << m_validationEndId << ' ' << m_validationEndEndpoint << "\n";

        // A második/harmadik érték legacy v8-v11 kompatibilitási mező.
        // A jelenlegi vízsebesség-validáció fix nyílt 1.5 / 3.0 m/s profilt használ.
        out << "FLOW_SETTINGS " << m_flowRateLps << ' ' << m_flowMinVelocityMps << ' ' << m_flowMaxVelocityMps
            << "\n";

        out << "HYDRAULIC_SETTINGS " << m_hydraulicFlowRateLps << ' ' << m_hydraulicDensityKgM3 << ' '
            << m_hydraulicKinematicViscosityM2s << ' ' << m_hydraulicMaxTotalPressureLossBar << "\n";
        out << "ELEMENTS " << m_network.elements().size() << "\n";
        for (const auto &[id, pipe] : m_network.elements())
        {
            const SceneObject *object = findSceneObject(id);
            out << "ELEMENT " << pipe.id << ' ' << pipeTypeToText(pipe.type) << '\n';
            out << "PIPE " << pipe.pipeTypeIndex << ' ' << pipe.pipeSizeIndex << ' ' << pipe.diameterMm << ' '
                << pipe.wallThicknessMm << ' ' << pipe.lengthM << ' ' << pipe.bendRadiusM << ' '
                << pipe.designPressureBar << ' ' << std::quoted(pipe.material) << "\n";
            // A min/max szögek legacy projektfájlmezők; a jelenlegi
            // Table 6 validáció a Qpeak + katalógusméret alapján választ követelményt.
            out << "DRAINAGE_CHECK " << pipe.drainageCheckEnabled << ' ' << pipe.drainageMinAngleDeg << ' '
                << pipe.drainageMaxAngleDeg << "\n";

            out << "HYDRAULIC_DATA " << pipe.hydraulicRoughnessMm << "\n";

            out << "FITTING_DATA " << pipe.secondaryPipeSizeIndex << ' ' << pipe.secondaryDiameterMm << ' '
                << pipe.secondaryWallThicknessMm << "\n";

            const Transform transform = object ? object->transform : Transform{};
            out << "TRANSFORM " << transform.position.x << ' ' << transform.position.y << ' '
                << transform.position.z << ' ' << transform.rotationEuler.x << ' ' << transform.rotationEuler.y
                << ' ' << transform.rotationEuler.z << ' ' << transform.scale.x << ' ' << transform.scale.y << ' '
                << transform.scale.z << "\n";

            out << "CONNECTIONS " << pipe.connections.size() << "\n";
            for (int i = 0; i < static_cast<int>(pipe.connections.size()); ++i)
            {
                const auto &cp = pipe.connections[i];
                out << "C " << i << ' ' << cp.connectedElementId << ' ' << cp.connectedPointIndex << ' '
                    << cp.intentionalOpenEnd << "\n";
            }
            out << "END_ELEMENT\n";
        }

        out << "ARCHITECTURE " << m_architecturalObstacles.size() << "\n";
        for (const ArchitecturalObstacle &obstacle : m_architecturalObstacles)
        {
            const SceneObject *object = findSceneObject(obstacle.id);
            const Transform transform = object ? object->transform : Transform{};
            out << "ARCH_OBSTACLE " << obstacle.id << ' ' << architecturalObstacleTypeToken(obstacle.type) << ' '
                << obstacle.sizeM.x << ' ' << obstacle.sizeM.y << ' ' << obstacle.sizeM.z << ' '
                << obstacle.blocksRouting << ' ' << transform.position.x << ' ' << transform.position.y << ' '
                << transform.position.z << ' ' << transform.rotationEuler.x << ' ' << transform.rotationEuler.y
                << ' ' << transform.rotationEuler.z << "\n";
        }
        return true;
    }

    bool Application::loadProject(const std::string &path)
    {
        std::ifstream in(path);
        if (!in) return false;

        std::string header;
        int version = 0;
        in >> header >> version;
        if (header != "PLUMBCAD_PROJECT" || (version < 1 || version > 11)) return false;

        std::string token;

        if (version >= 8)
        {
            in >> token;
            if (token != "PRESSURE_TEST_SETTINGS") return false;

            in >> m_pressureDesignPressureBar >> m_pressureTestPressureBar >> m_pressureHoldTimeMin >>
                m_pressureInitialMeasuredPressureBar >> m_pressureFinalMeasuredPressureBar >>
                m_pressureAllowedDropBar;
        }
        else if (version >= 6)
        {
            // v6-v7: a nyomáspróba külön hálózati referenciaelemet mentett.
            // v8-tól a közös Validáció START elem adja a hálózatrészt.
            ObjectId ignoredPressureNetworkRootId = InvalidObjectId;

            in >> token;
            if (token != "PRESSURE_TEST_NETWORK") return false;

            in >> ignoredPressureNetworkRootId >> m_pressureDesignPressureBar >> m_pressureTestPressureBar >>
                m_pressureHoldTimeMin >> m_pressureInitialMeasuredPressureBar >>
                m_pressureFinalMeasuredPressureBar >> m_pressureAllowedDropBar;
        }
        else
        {
            m_pressureDesignPressureBar = 6.0f;
            m_pressureTestPressureBar = 9.0f;
            m_pressureHoldTimeMin = 30.0f;
            m_pressureInitialMeasuredPressureBar = 9.0f;
            m_pressureFinalMeasuredPressureBar = 9.0f;
            m_pressureAllowedDropBar = 0.20f;
        }

        if (version >= 8)
        {
            in >> token;
            if (token != "VALIDATION_PATH") return false;
            in >> m_validationStartId >> m_validationStartEndpoint >> m_validationEndId >> m_validationEndEndpoint;

            in >> token;
            if (token != "FLOW_SETTINGS") return false;
            in >> m_flowRateLps >> m_flowMinVelocityMps >> m_flowMaxVelocityMps;

            in >> token;
            if (token != "HYDRAULIC_SETTINGS") return false;
            in >> m_hydraulicFlowRateLps >> m_hydraulicDensityKgM3 >> m_hydraulicKinematicViscosityM2s >>
                m_hydraulicMaxTotalPressureLossBar;
        }
        else
        {
            ObjectId oldFlowStartId = InvalidObjectId;
            int oldFlowStartEndpoint = -1;
            ObjectId oldFlowEndId = InvalidObjectId;
            int oldFlowEndEndpoint = -1;

            ObjectId oldDrainageStartId = InvalidObjectId;
            int oldDrainageStartEndpoint = -1;
            ObjectId oldDrainageEndId = InvalidObjectId;
            int oldDrainageEndEndpoint = -1;

            ObjectId oldHydraulicStartId = InvalidObjectId;
            int oldHydraulicStartEndpoint = -1;
            ObjectId oldHydraulicEndId = InvalidObjectId;
            int oldHydraulicEndEndpoint = -1;

            m_flowRateLps = 0.20f;
            m_flowMinVelocityMps = 0.10f;
            m_flowMaxVelocityMps = 2.00f;

            m_hydraulicFlowRateLps = 0.20f;
            m_hydraulicDensityKgM3 = 998.2f;
            m_hydraulicKinematicViscosityM2s = 0.000001004f;
            m_hydraulicMaxTotalPressureLossBar = 0.50f;

            if (version >= 5)
            {
                in >> token;
                if (token != "FLOW_PATH") return false;
                in >> oldFlowStartId >> oldFlowStartEndpoint >> oldFlowEndId >> oldFlowEndEndpoint >>
                    m_flowRateLps >> m_flowMinVelocityMps >> m_flowMaxVelocityMps;
            }

            if (version >= 4)
            {
                in >> token;
                if (token != "DRAINAGE_PATH") return false;
                in >> oldDrainageStartId >> oldDrainageStartEndpoint >> oldDrainageEndId >> oldDrainageEndEndpoint;

                in >> token;
                if (token != "HYDRAULIC_PATH") return false;
                in >> oldHydraulicStartId >> oldHydraulicStartEndpoint >> oldHydraulicEndId >>
                    oldHydraulicEndEndpoint >> m_hydraulicFlowRateLps >> m_hydraulicDensityKgM3 >>
                    m_hydraulicKinematicViscosityM2s >> m_hydraulicMaxTotalPressureLossBar;
            }

            auto hasCompleteLegacyPath = [](ObjectId startId, int startEndpoint, ObjectId endId, int endEndpoint) {
                return startId != InvalidObjectId && startEndpoint >= 0 && endId != InvalidObjectId &&
                       endEndpoint >= 0;
            };

            auto hasAnyLegacyPath = [](ObjectId startId, int startEndpoint, ObjectId endId, int endEndpoint) {
                return startId != InvalidObjectId || startEndpoint >= 0 || endId != InvalidObjectId ||
                       endEndpoint >= 0;
            };

            auto assignLegacyPath = [&](ObjectId startId, int startEndpoint, ObjectId endId, int endEndpoint) {
                m_validationStartId = startId;
                m_validationStartEndpoint = startEndpoint;
                m_validationEndId = endId;
                m_validationEndEndpoint = endEndpoint;
            };

            m_validationStartId = InvalidObjectId;
            m_validationStartEndpoint = -1;
            m_validationEndId = InvalidObjectId;
            m_validationEndEndpoint = -1;

            // v4-v7 migráció: először a teljes, használható útvonalat keressük.
            // Prioritás: EN 806 áramlás -> EN 1610 lejtés -> Darcy.
            if (hasCompleteLegacyPath(oldFlowStartId, oldFlowStartEndpoint, oldFlowEndId, oldFlowEndEndpoint))
            {
                assignLegacyPath(oldFlowStartId, oldFlowStartEndpoint, oldFlowEndId, oldFlowEndEndpoint);
            }
            else if (hasCompleteLegacyPath(oldDrainageStartId, oldDrainageStartEndpoint, oldDrainageEndId,
                                           oldDrainageEndEndpoint))
            {
                assignLegacyPath(oldDrainageStartId, oldDrainageStartEndpoint, oldDrainageEndId,
                                 oldDrainageEndEndpoint);
            }
            else if (hasCompleteLegacyPath(oldHydraulicStartId, oldHydraulicStartEndpoint, oldHydraulicEndId,
                                           oldHydraulicEndEndpoint))
            {
                assignLegacyPath(oldHydraulicStartId, oldHydraulicStartEndpoint, oldHydraulicEndId,
                                 oldHydraulicEndEndpoint);
            }
            else if (hasAnyLegacyPath(oldFlowStartId, oldFlowStartEndpoint, oldFlowEndId, oldFlowEndEndpoint))
            {
                assignLegacyPath(oldFlowStartId, oldFlowStartEndpoint, oldFlowEndId, oldFlowEndEndpoint);
            }
            else if (hasAnyLegacyPath(oldDrainageStartId, oldDrainageStartEndpoint, oldDrainageEndId,
                                      oldDrainageEndEndpoint))
            {
                assignLegacyPath(oldDrainageStartId, oldDrainageStartEndpoint, oldDrainageEndId,
                                 oldDrainageEndEndpoint);
            }
            else if (hasAnyLegacyPath(oldHydraulicStartId, oldHydraulicStartEndpoint, oldHydraulicEndId,
                                      oldHydraulicEndEndpoint))
            {
                assignLegacyPath(oldHydraulicStartId, oldHydraulicStartEndpoint, oldHydraulicEndId,
                                 oldHydraulicEndEndpoint);
            }
        }

        std::size_t count = 0;
        in >> token >> count;
        if (token != "ELEMENTS") return false;

        struct LoadedScene
        {
            PipeElement pipe;
            Transform transform;
        };
        std::vector<LoadedScene> loaded;
        loaded.reserve(count);

        bool legacyPressureDefaultsCaptured = false;

        for (std::size_t elementIndex = 0; elementIndex < count; ++elementIndex)
        {
            LoadedScene item;
            std::string typeText;
            in >> token >> item.pipe.id >> typeText;
            if (token != "ELEMENT") return false;
            item.pipe.type = pipeTypeFromText(typeText);

            in >> token >> item.pipe.pipeTypeIndex >> item.pipe.pipeSizeIndex >> item.pipe.diameterMm >>
                item.pipe.wallThicknessMm >> item.pipe.lengthM >> item.pipe.bendRadiusM >>
                item.pipe.designPressureBar >> std::quoted(item.pipe.material);
            if (token != "PIPE") return false;

            in >> token;
            if (token == "PRESSURE_TEST")
            {
                // v1-v5 régi elem-szintű nyomáspróba rekord.
                bool oldEnabled = true;
                float oldMultiplier = 1.5f;
                float oldTestPressureBar = 9.0f;
                float oldHoldTimeMin = 30.0f;
                float oldMeasuredDropBar = 0.0f;
                float oldAllowedDropBar = 0.20f;

                in >> oldEnabled >> oldMultiplier >> oldTestPressureBar >> oldHoldTimeMin >> oldMeasuredDropBar >>
                    oldAllowedDropBar;

                // Régi projektből az első bekapcsolt elem adatait átvesszük
                // az új globális hálózati nyomáspróba alapértékeinek.
                if (version < 6 && oldEnabled && !legacyPressureDefaultsCaptured)
                {
                    legacyPressureDefaultsCaptured = true;

                    m_pressureDesignPressureBar = item.pipe.designPressureBar;

                    m_pressureTestPressureBar = oldTestPressureBar;

                    m_pressureHoldTimeMin = oldHoldTimeMin;

                    m_pressureInitialMeasuredPressureBar = oldTestPressureBar;

                    m_pressureFinalMeasuredPressureBar =
                        std::max(0.0f, oldTestPressureBar - std::max(0.0f, oldMeasuredDropBar));

                    m_pressureAllowedDropBar = std::max(0.0f, oldAllowedDropBar);
                }

                in >> token;
            }

            if (token == "FLOW_CHECK")
            {
                bool ignoredEnabled = true;
                float ignoredFlowRateLps = 0.20f;
                float ignoredMinVelocityMps = 0.10f;
                float ignoredMaxVelocityMps = 2.00f;
                in >> ignoredEnabled >> ignoredFlowRateLps >> ignoredMinVelocityMps >> ignoredMaxVelocityMps;
                in >> token;
            }

            if (token == "DRAINAGE_CHECK")
            {
                if (version >= 3)
                {
                    in >> item.pipe.drainageCheckEnabled >> item.pipe.drainageMinAngleDeg >>
                        item.pipe.drainageMaxAngleDeg;
                }
                else
                {
                    // v1/v2: bool + régi iránymező + min/max százalék.
                    int ignoredOldDirection = 0;
                    float oldMinPercent = 1.0f;
                    float oldMaxPercent = 15.0f;

                    in >> item.pipe.drainageCheckEnabled >> ignoredOldDirection >> oldMinPercent >> oldMaxPercent;

                    constexpr float Pi = 3.14159265359f;

                    item.pipe.drainageMinAngleDeg =
                        std::atan(std::max(0.0f, oldMinPercent) / 100.0f) * (180.0f / Pi);

                    item.pipe.drainageMaxAngleDeg =
                        std::atan(std::max(0.0f, oldMaxPercent) / 100.0f) * (180.0f / Pi);
                }

                in >> token;
            }

            if (token == "HYDRAULIC_DATA")
            {
                in >> item.pipe.hydraulicRoughnessMm;
                in >> token;
            }
            else if (token == "HYDRAULIC_CHECK")
            {
                bool ignoredEnabled = true;
                float ignoredDensity = 998.2f;
                float ignoredViscosity = 0.000001004f;
                float ignoredMaxElementLoss = 0.50f;
                in >> ignoredEnabled >> item.pipe.hydraulicRoughnessMm >> ignoredDensity >> ignoredViscosity >>
                    ignoredMaxElementLoss;
                in >> token;
            }

            if (version >= 10)
            {
                if (token != "FITTING_DATA") return false;

                in >> item.pipe.secondaryPipeSizeIndex >> item.pipe.secondaryDiameterMm >>
                    item.pipe.secondaryWallThicknessMm;

                in >> token;
            }
            else
            {
                item.pipe.secondaryPipeSizeIndex = -1;
                item.pipe.secondaryDiameterMm = 0.0f;
                item.pipe.secondaryWallThicknessMm = 0.0f;
            }

            if (token != "TRANSFORM") return false;
            in >> item.transform.position.x >> item.transform.position.y >> item.transform.position.z >>
                item.transform.rotationEuler.x >> item.transform.rotationEuler.y >>
                item.transform.rotationEuler.z >> item.transform.scale.x >> item.transform.scale.y >>
                item.transform.scale.z;

            std::size_t connectionCount = 0;
            in >> token >> connectionCount;
            if (token != "CONNECTIONS") return false;
            item.pipe.connections.resize(connectionCount);
            for (std::size_t i = 0; i < connectionCount; ++i)
            {
                int pointIndex = -1;
                in >> token >> pointIndex;
                if (token != "C" || pointIndex < 0 || pointIndex >= static_cast<int>(connectionCount))
                    return false;
                auto &cp = item.pipe.connections[pointIndex];
                in >> cp.connectedElementId >> cp.connectedPointIndex;

                if (version >= 7)
                {
                    in >> cp.intentionalOpenEnd;
                }
                else
                {
                    cp.intentionalOpenEnd = false;
                }

                // Kapcsolt csővég nem lehet szándékosan nyitott.
                if (!cp.isOpen())
                {
                    cp.intentionalOpenEnd = false;
                }
            }

            in >> token;
            if (token != "END_ELEMENT") return false;
            loaded.push_back(item);
        }

        struct LoadedArchitecture
        {
            ArchitecturalObstacle obstacle;
            Transform transform;
        };
        std::vector<LoadedArchitecture> loadedArchitecture;
        if (version >= 11)
        {
            std::size_t architectureCount = 0;
            in >> token >> architectureCount;
            if (token != "ARCHITECTURE") return false;
            loadedArchitecture.reserve(architectureCount);
            for (std::size_t i = 0; i < architectureCount; ++i)
            {
                LoadedArchitecture item;
                std::string typeToken;
                in >> token >> item.obstacle.id >> typeToken >> item.obstacle.sizeM.x >> item.obstacle.sizeM.y >>
                    item.obstacle.sizeM.z >> item.obstacle.blocksRouting >> item.transform.position.x >>
                    item.transform.position.y >> item.transform.position.z >> item.transform.rotationEuler.x >>
                    item.transform.rotationEuler.y >> item.transform.rotationEuler.z;
                if (token != "ARCH_OBSTACLE") return false;
                if (!architecturalObstacleTypeFromToken(typeToken, item.obstacle.type)) return false;
                item.obstacle.sizeM.x = std::clamp(item.obstacle.sizeM.x, 0.05f, 100.0f);
                item.obstacle.sizeM.y = std::clamp(item.obstacle.sizeM.y, 0.05f, 100.0f);
                item.obstacle.sizeM.z = std::clamp(item.obstacle.sizeM.z, 0.05f, 100.0f);
                loadedArchitecture.push_back(item);
            }
        }

        m_network.clear();
        m_architecturalObstacles.clear();
        m_selectedId = InvalidObjectId;
        m_validationMessages.clear();
        m_validationHasRun = false;
        m_endpointMarkers.clear();

        // Az A* keresési állapot és az "utolsó létrehozott útvonal" lista
        // nincs a projektfájlban perzisztálva, ezért betöltéskor nem maradhat
        // bent egy előző projektből / munkamenetből származó érték.
        m_routeStartId = InvalidObjectId;
        m_routeStartEndpoint = -1;
        m_routeEndId = InvalidObjectId;
        m_routeEndEndpoint = -1;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        m_lastRouteCreatedIds.clear();
        clearRoutePreview();

        // A rácsháló nem csőelem, ezért megtartjuk. Minden más jelenetobjektumot a mentésből építünk újra.
        m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
                                       [](const SceneObject &object) { return object.selectable; }),
                        m_objects.end());

        for (const auto &item : loaded)
        {
            m_network.addLoadedElement(item.pipe);
        }

        for (const auto &item : loaded)
        {
            const PipeElement *normalizedPipe = m_network.find(item.pipe.id);

            SceneObject object = makeSceneObjectForPipe(normalizedPipe ? *normalizedPipe : item.pipe);

            object.transform = item.transform;
            m_objects.push_back(object);
            m_renderer.uploadObject(object);
        }

        for (const LoadedArchitecture &item : loadedArchitecture)
        {
            m_architecturalObstacles.push_back(item.obstacle);
            SceneObject object = makeSceneObjectForArchitecturalObstacle(item.obstacle);
            object.transform = item.transform;
            m_objects.push_back(object);
            m_renderer.uploadObject(object);
        }

        cleanupInvalidConnections();

        auto normalizeValidationEndpoint = [&](ObjectId &id, int &endpoint) {
            const PipeElement *pipe = m_network.find(id);
            if (!pipe || endpoint < 0 || endpoint >= static_cast<int>(pipe->connections.size()))
            {
                id = InvalidObjectId;
                endpoint = -1;
            }
        };

        normalizeValidationEndpoint(m_validationStartId, m_validationStartEndpoint);
        normalizeValidationEndpoint(m_validationEndId, m_validationEndEndpoint);

        enforceElbowStraightConstraints();
        updateEndpointMarkers();
        return true;
    }
}
