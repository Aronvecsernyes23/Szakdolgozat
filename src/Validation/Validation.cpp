#include "Validation/Validation.hpp"
#include <sstream>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <glm/gtx/norm.hpp>

namespace plumbcad
{
    namespace
    {
        void appendSummary(std::vector<ValidationMessage> &results, ValidationMessage summary)
        {
            summary.isRedundantSummary = std::any_of(results.begin(), results.end(), [&](const auto &row) {
                return row.severity == summary.severity && !row.isReference;
            });
            results.push_back(std::move(summary));
        }

        const char *elementTypeName(PipeElementType type)
        {
            switch (type)
            {
            case PipeElementType::StraightPipe:
                return "Egyenes csőszakasz";
            case PipeElementType::Elbow90:
                return "90° könyök";
            case PipeElementType::Elbow45:
                return "45° könyök";
            case PipeElementType::AlignmentElbow:
                return "Automatikus végpont-illesztő könyök";
            case PipeElementType::Tee:
                return "T-idom";
            case PipeElementType::Reducer:
                return "Szűkítő";
            case PipeElementType::Valve:
                return "Szelep";
            case PipeElementType::EndCap:
                return "Végzáró";
            }
            return "Ismeretlen elem";
        }

        const SceneObject *findValidationSceneObject(const std::vector<SceneObject> &objects, ObjectId id)
        {
            for (const SceneObject &object : objects)
            {
                if (object.id == id) return &object;
            }
            return nullptr;
        }

        std::vector<glm::vec3> validationElementLocalCenterline(const PipeElement &element, int inletEndpoint,
                                                                int outletEndpoint)
        {
            std::vector<glm::vec3> points;

            if (inletEndpoint < 0 || outletEndpoint < 0 ||
                inletEndpoint >= static_cast<int>(element.connections.size()) ||
                outletEndpoint >= static_cast<int>(element.connections.size()) || inletEndpoint == outletEndpoint)
            {
                return points;
            }

            // Egyenes, szűkítő, szelep és egyéb kétcsatlakozásos elem:
            // a két tényleges csatlakozó közti középvonal.
            if (element.type != PipeElementType::Elbow45 && element.type != PipeElementType::Elbow90 &&
                element.type != PipeElementType::AlignmentElbow && element.type != PipeElementType::Tee)
            {
                points.push_back(element.connections[inletEndpoint].localPosition);
                points.push_back(element.connections[outletEndpoint].localPosition);
                return points;
            }

            // Könyökök: a tényleges körív-középvonalat mintavételezzük.
            if (element.type == PipeElementType::Elbow45 || element.type == PipeElementType::Elbow90 ||
                element.type == PipeElementType::AlignmentElbow)
            {
                if (element.connections.size() < 2 || inletEndpoint > 1 || outletEndpoint > 1)
                {
                    return points;
                }

                const float radius = std::max(0.1f, element.bendRadiusM);

                const float angle = element.type == PipeElementType::Elbow45
                                        ? 0.78539816339f
                                        : (element.type == PipeElementType::Elbow90
                                               ? 1.57079632679f
                                               : std::clamp(element.lengthM / radius, 0.001f, 1.57079632679f));

                const int samples = std::max(4, static_cast<int>(std::ceil(angle / 0.0872664626f)));

                const bool forward = inletEndpoint == 0 && outletEndpoint == 1;

                for (int i = 0; i <= samples; ++i)
                {
                    const float u = static_cast<float>(i) / static_cast<float>(samples);

                    const float t = forward ? angle * u : angle * (1.0f - u);

                    points.push_back(glm::vec3{radius * std::cos(t), 0.0f, radius * std::sin(t)});
                }

                // A numerikus minta két vége pontosan a PipeNetwork
                // csatlakozási pontja legyen.
                points.front() = element.connections[inletEndpoint].localPosition;
                points.back() = element.connections[outletEndpoint].localPosition;

                return points;
            }

            // T-idom: minden beömlő a geometriai középponton keresztül
            // halad a kijelölt downstream kimenet felé.
            if (element.type == PipeElementType::Tee)
            {
                points.push_back(element.connections[inletEndpoint].localPosition);

                const glm::vec3 center{0.0f};

                if (glm::length2(points.back() - center) > 0.000001f) points.push_back(center);

                const glm::vec3 outlet = element.connections[outletEndpoint].localPosition;

                if (points.empty() || glm::length2(points.back() - outlet) > 0.000001f)
                {
                    points.push_back(outlet);
                }

                return points;
            }

            return points;
        }

        std::vector<glm::vec3> validationElementWorldCenterline(const PipeElement &element,
                                                                const SceneObject &object, int inletEndpoint,
                                                                int outletEndpoint)
        {
            const std::vector<glm::vec3> local =
                validationElementLocalCenterline(element, inletEndpoint, outletEndpoint);

            std::vector<glm::vec3> world;
            world.reserve(local.size());

            const glm::mat4 model = object.transform.matrix();

            for (const glm::vec3 &point : local)
            {
                world.push_back(glm::vec3(model * glm::vec4(point, 1.0f)));
            }

            return world;
        }

        // Nyílt, ingyenesen elérhető proxy-referenciák.
        // NEM MSZ EN 806 / MSZ EN 1610 táblázatok.
        constexpr float OpenWaterRecommendedVelocityMps = 1.50f;
        constexpr float OpenWaterMaximumVelocityMps = 3.00f;

        struct OpenDrainageReferenceRow
        {
            float minimumNominalDiameterMm;
            float minimumFlowLps;
            float maximumFlowLps;
            float gradientDenominator;
            float capacityLps;
            const char *rowName;
        };

        constexpr OpenDrainageReferenceRow OpenDrainageRows[] = {
            {75.0f, 0.0f, 1.0f, 40.0f, 4.1f, "Q < 1 l/s | DN75 | 1:40 | 4.1 l/s"},
            {100.0f, 0.0f, 1.0f, 40.0f, 9.2f, "Q < 1 l/s | DN100 | 1:40 | 9.2 l/s"},
            {75.0f, 1.0f, 1000000.0f, 80.0f, 2.8f, "Q >= 1 l/s | DN75 | 1:80 | 2.8 l/s"},
            {100.0f, 1.0f, 1000000.0f, 80.0f, 6.3f, "Q >= 1 l/s | DN100 | 1:80 | 6.3 l/s"},
            {150.0f, 1.0f, 1000000.0f, 150.0f, 15.0f, "Q >= 1 l/s | DN150 | 1:150 | 15.0 l/s"}};

        struct OpenDrainageLookup
        {
            bool valid{false};
            bool conservativeLargerDiameterFallback{false};
            float referenceNominalDiameterMm{0.0f};
            OpenDrainageReferenceRow row{};
        };

        float drainageReferenceNominalDiameterMm(float selectedCatalogDiameterMm)
        {
            if (!std::isfinite(selectedCatalogDiameterMm) || selectedCatalogDiameterMm < 75.0f)
            {
                return 0.0f;
            }

            // A Table 6 sorát a felhasználó által kiválasztott
            // katalógusméretből választjuk, nem a számított Di-ből.
            // Jelenlegi PVC-U katalógus:
            // 75 -> DN75
            // 110 / 125 -> DN100
            // 160 -> DN150
            if (selectedCatalogDiameterMm >= 150.0f) return 150.0f;

            if (selectedCatalogDiameterMm >= 100.0f) return 100.0f;

            return 75.0f;
        }

        bool isFoulDrainReferenceMaterial(const PipeElement &element)
        {
            return element.material == "PVC-U";
        }

        OpenDrainageLookup lookupOpenDrainageReference(float selectedCatalogDiameterMm, float peakFlowRateLps)
        {
            OpenDrainageLookup result;
            result.referenceNominalDiameterMm = drainageReferenceNominalDiameterMm(selectedCatalogDiameterMm);

            if (result.referenceNominalDiameterMm <= 0.0f || !std::isfinite(peakFlowRateLps) ||
                peakFlowRateLps <= 0.0f)
            {
                return result;
            }

            if (peakFlowRateLps < 1.0f)
            {
                result.valid = true;

                if (result.referenceNominalDiameterMm < 100.0f)
                {
                    result.row = OpenDrainageRows[0];
                }
                else
                {
                    result.row = OpenDrainageRows[1];
                    result.conservativeLargerDiameterFallback = result.referenceNominalDiameterMm >= 150.0f;
                }

                return result;
            }

            result.valid = true;

            if (result.referenceNominalDiameterMm < 100.0f)
                result.row = OpenDrainageRows[2];
            else if (result.referenceNominalDiameterMm < 150.0f)
                result.row = OpenDrainageRows[3];
            else
                result.row = OpenDrainageRows[4];

            return result;
        }

        float gradientDenominatorToAngleDeg(float denominator)
        {
            if (!std::isfinite(denominator) || denominator <= 0.0f) return 0.0f;
            return std::atan(1.0f / denominator) * 57.29577951308232f;
        }

        struct DrainageSegmentCheck
        {
            bool valid{false};
            bool risesAnywhere{false};
            float minimumAngleDeg{std::numeric_limits<float>::max()};
            float maximumAngleDeg{std::numeric_limits<float>::lowest()};
            int checkedSegmentCount{0};
        };

        DrainageSegmentCheck checkDrainageAngles(const std::vector<glm::vec3> &points)
        {
            DrainageSegmentCheck check;

            if (points.size() < 2) return check;

            check.valid = true;

            constexpr float HeightToleranceM = 0.0005f;
            constexpr float SegmentToleranceM = 0.0001f;
            constexpr float Pi = 3.14159265359f;

            for (std::size_t i = 0; i + 1 < points.size(); ++i)
            {
                const glm::vec3 delta = points[i + 1] - points[i];

                const float length = glm::length(delta);

                if (length <= SegmentToleranceM) continue;

                // A víz a START -> END irányban halad.
                // Pozitív drop = lefelé haladás.
                // Negatív drop = helyi emelkedés -> azonnali hiba.
                const float drop = points[i].y - points[i + 1].y;

                if (drop < -HeightToleranceM) check.risesAnywhere = true;

                const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);

                // A lejtési szög a vízszinteshez képest:
                // 0° = vízszintes
                // +90° = függőlegesen lefelé
                // negatív = emelkedik
                const float angleDeg = std::atan2(drop, horizontal) * (180.0f / Pi);

                check.minimumAngleDeg = std::min(check.minimumAngleDeg, angleDeg);

                check.maximumAngleDeg = std::max(check.maximumAngleDeg, angleDeg);

                ++check.checkedSegmentCount;
            }

            return check;
        }

        struct NetworkEndpointNode
        {
            ObjectId objectId{InvalidObjectId};
            int endpoint{-1};

            bool operator==(const NetworkEndpointNode &other) const
            {
                return objectId == other.objectId && endpoint == other.endpoint;
            }
        };

        struct NetworkEndpointNodeHash
        {
            std::size_t operator()(const NetworkEndpointNode &node) const noexcept
            {
                return static_cast<std::size_t>(node.objectId) * 2654435761u ^
                       static_cast<std::size_t>(node.endpoint + 1) * 2246822519u;
            }
        };

        float networkInternalEdgeLength(const PipeElement &element, const SceneObject &object, int fromEndpoint,
                                        int toEndpoint)
        {
            const std::vector<glm::vec3> points =
                validationElementWorldCenterline(element, object, fromEndpoint, toEndpoint);

            float length = 0.0f;

            for (std::size_t i = 0; i + 1 < points.size(); ++i)
            {
                length += glm::length(points[i + 1] - points[i]);
            }

            return std::max(0.001f, length);
        }

        bool buildConnectedEndpointPath(const PipeNetwork &network, const std::vector<SceneObject> &objects,
                                        ObjectId startId, int startEndpoint, ObjectId endId, int endEndpoint,
                                        std::vector<NetworkEndpointNode> &path)
        {
            path.clear();

            const PipeElement *startPipe = network.find(startId);
            const PipeElement *endPipe = network.find(endId);

            if (!startPipe || !endPipe || startEndpoint < 0 || endEndpoint < 0 ||
                startEndpoint >= static_cast<int>(startPipe->connections.size()) ||
                endEndpoint >= static_cast<int>(endPipe->connections.size()))
            {
                return false;
            }

            struct QueueItem
            {
                float distance{0.0f};
                NetworkEndpointNode node{};

                bool operator<(const QueueItem &other) const { return distance > other.distance; }
            };

            const NetworkEndpointNode start{startId, startEndpoint};

            const NetworkEndpointNode goal{endId, endEndpoint};

            std::priority_queue<QueueItem> queue;

            std::unordered_map<NetworkEndpointNode, float, NetworkEndpointNodeHash> distance;

            std::unordered_map<NetworkEndpointNode, NetworkEndpointNode, NetworkEndpointNodeHash> parent;

            distance[start] = 0.0f;
            queue.push({0.0f, start});

            auto relax = [&](const NetworkEndpointNode &from, const NetworkEndpointNode &to, float edgeLength) {
                const float candidate = distance[from] + edgeLength;

                const auto it = distance.find(to);

                if (it != distance.end() && candidate >= it->second - 0.000001f)
                {
                    return;
                }

                distance[to] = candidate;
                parent[to] = from;

                queue.push({candidate, to});
            };

            while (!queue.empty())
            {
                const QueueItem current = queue.top();
                queue.pop();

                const auto stored = distance.find(current.node);

                if (stored == distance.end() || current.distance > stored->second + 0.000001f)
                {
                    continue;
                }

                if (current.node == goal) break;

                const PipeElement *pipe = network.find(current.node.objectId);

                if (!pipe || current.node.endpoint < 0 ||
                    current.node.endpoint >= static_cast<int>(pipe->connections.size()))
                {
                    continue;
                }

                // Kapcsolat két külön elem csővége között.
                const ConnectionPoint &connection = pipe->connections[current.node.endpoint];

                if (!connection.isOpen())
                {
                    const PipeElement *other = network.find(connection.connectedElementId);

                    if (other && connection.connectedPointIndex >= 0 &&
                        connection.connectedPointIndex < static_cast<int>(other->connections.size()))
                    {
                        relax(current.node, {connection.connectedElementId, connection.connectedPointIndex},
                              0.0001f);
                    }
                }

                // Áthaladás ugyanazon cső/idom belsején egyik portról
                // egy másik portra.
                const SceneObject *object = findValidationSceneObject(objects, current.node.objectId);

                if (!object) continue;

                for (int otherEndpoint = 0; otherEndpoint < static_cast<int>(pipe->connections.size());
                     ++otherEndpoint)
                {
                    if (otherEndpoint == current.node.endpoint)
                    {
                        continue;
                    }

                    relax(current.node, {current.node.objectId, otherEndpoint},
                          networkInternalEdgeLength(*pipe, *object, current.node.endpoint, otherEndpoint));
                }
            }

            if (!distance.contains(goal)) return false;

            NetworkEndpointNode current = goal;
            path.push_back(current);

            while (!(current == start))
            {
                const auto it = parent.find(current);

                if (it == parent.end()) return false;

                current = it->second;
                path.push_back(current);
            }

            std::reverse(path.begin(), path.end());

            return path.size() >= 2;
        }

        float hydraulicMinorLossCoefficient(const PipeElement &element, int fromEndpoint, int toEndpoint)
        {
            switch (element.type)
            {
            case PipeElementType::Elbow45:
                return 0.40f;
            case PipeElementType::Elbow90:
                return 0.90f;
            case PipeElementType::AlignmentElbow: {
                const float angle =
                    std::clamp(element.lengthM / std::max(element.bendRadiusM, 0.1f), 0.001f, 0.78539816339f);

                // Prototípus becslés: 45°-nál K=0.40, kisebb
                // végpont-illesztési szögnél arányosan kisebb.
                return 0.40f * (angle / 0.78539816339f);
            }
            case PipeElementType::Tee: {
                const bool mainRun =
                    (fromEndpoint == 0 && toEndpoint == 1) || (fromEndpoint == 1 && toEndpoint == 0);
                return mainRun ? 0.60f : 1.80f;
            }
            case PipeElementType::Reducer:
                return 0.30f;
            case PipeElementType::Valve:
                return 2.00f;
            default:
                return 0.0f;
            }
        }

        struct HydraulicElementResult
        {
            bool success{false};
            float innerDiameterM{0.0f};
            float velocityMps{0.0f};
            float reynolds{0.0f};
            float frictionFactor{0.0f};
            float k{0.0f};
            float majorLossBar{0.0f};
            float minorLossBar{0.0f};
            float totalLossBar{0.0f};
        };

        HydraulicElementResult calculateHydraulicElementLoss(const PipeElement &element, int fromEndpoint,
                                                             int toEndpoint, float flowRateLps, float densityKgM3,
                                                             float kinematicViscosityM2s)
        {
            HydraulicElementResult r;

            const float primaryInnerM = (element.diameterMm - 2.0f * element.wallThicknessMm) / 1000.0f;

            r.innerDiameterM = primaryInnerM;

            if (element.type == PipeElementType::Reducer && element.secondaryDiameterMm > 0.0f &&
                element.secondaryWallThicknessMm > 0.0f)
            {
                const float secondaryInnerM =
                    (element.secondaryDiameterMm - 2.0f * element.secondaryWallThicknessMm) / 1000.0f;

                // A prototípus-szintű szűkítőveszteség számításánál
                // a kisebb szabad keresztmetszet sebességét használjuk.
                r.innerDiameterM = std::min(primaryInnerM, secondaryInnerM);
            }

            if (r.innerDiameterM <= 0.0f || flowRateLps <= 0.0f || densityKgM3 <= 0.0f ||
                kinematicViscosityM2s <= 0.0f)
                return r;

            constexpr float Pi = 3.14159265359f;
            const float area = Pi * r.innerDiameterM * r.innerDiameterM * 0.25f;
            r.velocityMps = (flowRateLps / 1000.0f) / area;
            r.reynolds = r.velocityMps * r.innerDiameterM / kinematicViscosityM2s;
            if (r.reynolds <= 0.0f || !std::isfinite(r.reynolds)) return r;

            if (r.reynolds < 2300.0f)
                r.frictionFactor = 64.0f / r.reynolds;
            else
            {
                if (element.type == PipeElementType::StraightPipe && element.hydraulicRoughnessMm < 0.0f) return r;

                const float epsM = std::max(0.0f, element.hydraulicRoughnessMm) / 1000.0f;
                const float term = epsM / (3.7f * r.innerDiameterM) + 5.74f / std::pow(r.reynolds, 0.9f);
                const float lg = std::log10(std::max(term, 0.0000001f));
                r.frictionFactor = 0.25f / (lg * lg);
            }

            const float dynamicPa = densityKgM3 * r.velocityMps * r.velocityMps * 0.5f;

            if (element.type == PipeElementType::StraightPipe)
            {
                const float L = std::max(0.0f, element.lengthM);
                r.majorLossBar = r.frictionFactor * (L / r.innerDiameterM) * dynamicPa / 100000.0f;
            }

            r.k = hydraulicMinorLossCoefficient(element, fromEndpoint, toEndpoint);
            r.minorLossBar = r.k * dynamicPa / 100000.0f;
            r.totalLossBar = r.majorLossBar + r.minorLossBar;
            r.success = std::isfinite(r.totalLossBar) && r.totalLossBar >= 0.0f;
            return r;
        }
    }

    std::vector<ValidationMessage> NetworkValidator::validateOpenEnds(const PipeNetwork &network) const
    {
        std::vector<ValidationMessage> result;

        int checkedEndpointCount = 0;
        int connectedEndpointCount = 0;
        int intentionalOpenEndpointCount = 0;
        int unexpectedOpenEndpointCount = 0;

        for (const auto &[id, element] : network.elements())
        {
            for (std::size_t i = 0; i < element.connections.size(); ++i)
            {
                ++checkedEndpointCount;

                const ConnectionPoint &connection = element.connections[i];

                if (!connection.isOpen())
                {
                    ++connectedEndpointCount;
                    result.push_back({ValidationSeverity::Info, id, static_cast<int>(i),
                                      "Nyitott csővégek / Kapcsolt vég", "A végpont csatlakoztatva van."});
                    continue;
                }

                if (connection.intentionalOpenEnd)
                {
                    ++intentionalOpenEndpointCount;

                    std::ostringstream oss;
                    oss << elementTypeName(element.type) << " #" << id << " - " << i
                        << ". csatlakozási pont nyitott, "
                        << "de szándékos végpontként van megjelölve. "
                        << "EREDMÉNY: MEGFELELT.";

                    result.push_back({ValidationSeverity::Info, id, static_cast<int>(i),
                                      "Szándékos nyitott vég / MEGFELELT", oss.str()});

                    continue;
                }

                ++unexpectedOpenEndpointCount;

                std::ostringstream oss;
                oss << elementTypeName(element.type) << " #" << id << " - " << i << ". csatlakozási pont nyitott, "
                    << "és nincs szándékos végpontként megjelölve. "
                    << "EREDMÉNY: NEM FELELT MEG.";

                result.push_back({ValidationSeverity::Error, id, static_cast<int>(i),
                                  "Nyitott csővég / NEM FELELT MEG", oss.str()});
            }
        }

        const bool overallPassed = unexpectedOpenEndpointCount == 0;

        std::ostringstream summary;
        summary << "Nyitott csővégek ellenőrzése. "
                << "Vizsgált csatlakozási pontok: " << checkedEndpointCount
                << ", kapcsolt: " << connectedEndpointCount
                << ", szándékos nyitott vég: " << intentionalOpenEndpointCount
                << ", hibás nyitott vég: " << unexpectedOpenEndpointCount
                << ". EREDMÉNY: " << (overallPassed ? "MEGFELELT." : "NEM FELELT MEG.");

        appendSummary(result,
                      {overallPassed ? ValidationSeverity::Info : ValidationSeverity::Error, InvalidObjectId, -1,
                       overallPassed ? "Nyitott csővégek összesítés / MEGFELELT"
                                     : "Nyitott csővégek összesítés / NEM FELELT MEG",
                       summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateIsolatedElements(const PipeNetwork &network) const
    {
        std::vector<ValidationMessage> result;

        const int checkedElementCount = static_cast<int>(network.elements().size());

        if (checkedElementCount == 0)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Izolált elemek összesítés / NEM FELELT MEG",
                              "A projektben nincs vizsgálható cső- vagy idomelem. "
                              "EREDMÉNY: NEM FELELT MEG."});

            return result;
        }

        int connectedElementCount = 0;
        int isolatedElementCount = 0;

        for (const auto &[id, element] : network.elements())
        {
            int validConnectionCount = 0;

            for (int endpointIndex = 0; endpointIndex < static_cast<int>(element.connections.size());
                 ++endpointIndex)
            {
                const ConnectionPoint &connection = element.connections[endpointIndex];

                if (connection.isOpen()) continue;

                const PipeElement *other = network.find(connection.connectedElementId);

                if (!other) continue;

                if (connection.connectedPointIndex < 0 ||
                    connection.connectedPointIndex >= static_cast<int>(other->connections.size()))
                {
                    continue;
                }

                const ConnectionPoint &reciprocal = other->connections[connection.connectedPointIndex];

                if (reciprocal.connectedElementId != id || reciprocal.connectedPointIndex != endpointIndex)
                {
                    continue;
                }

                ++validConnectionCount;
            }

            if (validConnectionCount > 0)
            {
                ++connectedElementCount;
                result.push_back({ValidationSeverity::Info, id, -1, "Izolált elemek / Kapcsolt elem",
                                  "Érvényes kapcsolatok száma: " + std::to_string(validConnectionCount) + "."});
                continue;
            }

            ++isolatedElementCount;

            std::ostringstream oss;
            oss << elementTypeName(element.type) << " #" << id << ": érvényes kapcsolatok száma: 0. "
                << "Az elem nincs fizikailag összekapcsolva "
                << "egyetlen másik cső- vagy idomelemmel sem. "
                << "A szándékos nyitott végpont jelölés ettől még "
                << "nem számít hálózati kapcsolatnak. "
                << "EREDMÉNY: NEM FELELT MEG.";

            result.push_back({ValidationSeverity::Error, id, -1, "Izolált elem / NEM FELELT MEG", oss.str()});
        }

        const bool overallPassed = isolatedElementCount == 0;

        std::ostringstream summary;
        summary << "Izolált elemek ellenőrzése. "
                << "Vizsgált elemek: " << checkedElementCount
                << ", legalább egy érvényes kapcsolattal rendelkező elemek: " << connectedElementCount
                << ", izolált elemek: " << isolatedElementCount
                << ". EREDMÉNY: " << (overallPassed ? "MEGFELELT." : "NEM FELELT MEG.");

        appendSummary(result,
                      {overallPassed ? ValidationSeverity::Info : ValidationSeverity::Error, InvalidObjectId, -1,
                       overallPassed ? "Izolált elemek összesítés / MEGFELELT"
                                     : "Izolált elemek összesítés / NEM FELELT MEG",
                       summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateConnectedComponents(const PipeNetwork &network) const
    {
        std::vector<ValidationMessage> result;

        const auto &elements = network.elements();

        if (elements.empty())
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Hálózat összefüggősége összesítés / NEM FELELT MEG",
                              "A projektben nincs vizsgálható cső- vagy idomelem. "
                              "EREDMÉNY: NEM FELELT MEG."});

            return result;
        }

        std::unordered_map<ObjectId, std::vector<ObjectId>> adjacency;

        int malformedConnectionCount = 0;
        int diameterMismatchCount = 0;

        // Minden elem önálló gráfcsúcs, ezért a nulla kapcsolatú elem is
        // külön komponensként jelenik meg ebben a vizsgálatban.
        for (const auto &[id, element] : elements)
        {
            adjacency[id];

            for (int endpointIndex = 0; endpointIndex < static_cast<int>(element.connections.size());
                 ++endpointIndex)
            {
                const ConnectionPoint &connection = element.connections[endpointIndex];

                if (connection.isOpen()) continue;

                const PipeElement *other = network.find(connection.connectedElementId);

                if (!other)
                {
                    ++malformedConnectionCount;

                    std::ostringstream oss;
                    oss << elementTypeName(element.type) << " #" << id << " - " << endpointIndex
                        << ". végpont nem létező elemre hivatkozik (#" << connection.connectedElementId
                        << "). EREDMÉNY: NEM FELELT MEG.";

                    result.push_back({ValidationSeverity::Error, id, endpointIndex,
                                      "Hálózati kapcsolat hibás / NEM FELELT MEG", oss.str()});

                    continue;
                }

                if (connection.connectedPointIndex < 0 ||
                    connection.connectedPointIndex >= static_cast<int>(other->connections.size()))
                {
                    ++malformedConnectionCount;

                    std::ostringstream oss;
                    oss << elementTypeName(element.type) << " #" << id << " - " << endpointIndex
                        << ". végpont érvénytelen ellenoldali végpontra hivatkozik: #"
                        << connection.connectedElementId << " / " << connection.connectedPointIndex
                        << ". EREDMÉNY: NEM FELELT MEG.";

                    result.push_back({ValidationSeverity::Error, id, endpointIndex,
                                      "Hálózati kapcsolat hibás / NEM FELELT MEG", oss.str()});

                    continue;
                }

                const ConnectionPoint &reciprocal = other->connections[connection.connectedPointIndex];

                if (reciprocal.connectedElementId != id || reciprocal.connectedPointIndex != endpointIndex)
                {
                    ++malformedConnectionCount;

                    std::ostringstream oss;
                    oss << elementTypeName(element.type) << " #" << id << " - " << endpointIndex
                        << ". végpont kapcsolata nem kölcsönös a #" << connection.connectedElementId << " / "
                        << connection.connectedPointIndex << ". végponttal. EREDMÉNY: NEM FELELT MEG.";

                    result.push_back({ValidationSeverity::Error, id, endpointIndex,
                                      "Hálózati kapcsolat hibás / NEM FELELT MEG", oss.str()});

                    continue;
                }

                // Közvetlenül összekapcsolt végpontok külső átmérője
                // csak akkor lehet eltérő, ha a hálózatban tényleges
                // szűkítő biztosítja az átmenetet. A szűkítő két végének
                // külön átmérőjét pipeEndpointDiameterMm() kezeli.
                //
                // Egy fizikai kapcsolatot csak egyszer jelentünk.
                const bool firstSideOfConnection =
                    id < connection.connectedElementId ||
                    (id == connection.connectedElementId && endpointIndex < connection.connectedPointIndex);

                if (firstSideOfConnection)
                {
                    const float thisDiameterMm = pipeEndpointDiameterMm(element, endpointIndex);

                    const float otherDiameterMm = pipeEndpointDiameterMm(*other, connection.connectedPointIndex);

                    constexpr float DiameterToleranceMm = 0.10f;

                    if (!std::isfinite(thisDiameterMm) || !std::isfinite(otherDiameterMm) ||
                        std::abs(thisDiameterMm - otherDiameterMm) > DiameterToleranceMm)
                    {
                        ++diameterMismatchCount;

                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(1) << elementTypeName(element.type) << " #" << id
                            << " / " << endpointIndex << ". vég (Ø" << thisDiameterMm
                            << " mm) közvetlenül kapcsolódik a " << elementTypeName(other->type) << " #"
                            << connection.connectedElementId << " / " << connection.connectedPointIndex
                            << ". véghez (Ø" << otherDiameterMm << " mm). Eltérő csőátmérők közvetlenül "
                            << "nem kapcsolhatók össze; használj megfelelő szűkítőt. "
                            << "EREDMÉNY: NEM FELELT MEG.";

                        result.push_back({ValidationSeverity::Error, id, endpointIndex,
                                          "Közvetlen átmérő-eltérés / NEM FELELT MEG", oss.str()});
                    }
                }

                adjacency[id].push_back(connection.connectedElementId);
            }
        }

        std::unordered_set<ObjectId> visited;
        std::vector<std::vector<ObjectId>> components;

        // Determinisztikus indulási sorrend.
        std::vector<ObjectId> allIds;
        allIds.reserve(elements.size());

        for (const auto &[id, element] : elements)
        {
            (void)element;
            allIds.push_back(id);
        }

        std::sort(allIds.begin(), allIds.end());

        for (ObjectId startId : allIds)
        {
            if (visited.contains(startId)) continue;

            std::vector<ObjectId> component;
            std::queue<ObjectId> queue;

            visited.insert(startId);
            queue.push(startId);

            while (!queue.empty())
            {
                const ObjectId current = queue.front();
                queue.pop();

                component.push_back(current);

                const auto adjacencyIt = adjacency.find(current);

                if (adjacencyIt == adjacency.end())
                {
                    continue;
                }

                for (ObjectId next : adjacencyIt->second)
                {
                    if (!elements.contains(next)) continue;

                    if (visited.insert(next).second) queue.push(next);
                }
            }

            std::sort(component.begin(), component.end());

            components.push_back(std::move(component));
        }

        // Fő hálózat: a legtöbb elemet tartalmazó komponens.
        // Holtversenynél a kisebb első ObjectId nyer.
        std::sort(components.begin(), components.end(),
                  [](const std::vector<ObjectId> &a, const std::vector<ObjectId> &b) {
                      if (a.size() != b.size()) return a.size() > b.size();

                      if (a.empty() || b.empty()) return a.size() > b.size();

                      return a.front() < b.front();
                  });

        const std::vector<ObjectId> &mainComponent = components.front();

        auto appendIdList = [](std::ostringstream &oss, const std::vector<ObjectId> &ids) {
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                if (i > 0) oss << ", ";

                oss << "#" << ids[i];
            }
        };

        {
            const bool passed =
                components.size() == 1 && malformedConnectionCount == 0 && diameterMismatchCount == 0;
            std::ostringstream oss;
            oss << "Fő hálózatrész: " << mainComponent.size() << " elem. Elem ID-k: ";

            appendIdList(oss, mainComponent);

            oss << ". EREDMÉNY: " << (passed ? "MEGFELELT." : "REFERENCIA HÁLÓZATRÉSZ.");

            result.push_back(
                {ValidationSeverity::Info, mainComponent.empty() ? InvalidObjectId : mainComponent.front(), -1,
                 passed ? "Fő hálózatrész / MEGFELELT" : "Fő hálózatrész / REFERENCIA", oss.str(), !passed});
        }

        for (std::size_t componentIndex = 1; componentIndex < components.size(); ++componentIndex)
        {
            const std::vector<ObjectId> &component = components[componentIndex];

            std::ostringstream oss;
            oss << "Különálló hálózatrész #" << componentIndex << ": " << component.size() << " elem. Elem ID-k: ";

            appendIdList(oss, component);

            oss << ". Ez a hálózatrész nincs összekapcsolva a fő hálózattal. "
                << "EREDMÉNY: NEM FELELT MEG.";

            result.push_back({ValidationSeverity::Error, component.empty() ? InvalidObjectId : component.front(),
                              -1, "Különálló hálózatrész / NEM FELELT MEG", oss.str()});
        }

        const bool overallPassed =
            components.size() == 1 && malformedConnectionCount == 0 && diameterMismatchCount == 0;

        std::ostringstream summary;
        summary << "Kapcsolt hálózat összefüggőségi vizsgálata. "
                << "Vizsgált elemek: " << elements.size()
                << ", kapcsolt hálózatrészek száma: " << components.size()
                << ", fő hálózat elemei: " << mainComponent.size()
                << ", különálló hálózatrészek: " << (components.size() > 0 ? components.size() - 1 : 0)
                << ", hibás kapcsolat-hivatkozások: " << malformedConnectionCount
                << ", közvetlen átmérő-eltérések: " << diameterMismatchCount
                << ". EREDMÉNY: " << (overallPassed ? "MEGFELELT." : "NEM FELELT MEG.");

        appendSummary(result,
                      {overallPassed ? ValidationSeverity::Info : ValidationSeverity::Error, InvalidObjectId, -1,
                       overallPassed ? "Hálózat összefüggősége összesítés / MEGFELELT"
                                     : "Hálózat összefüggősége összesítés / NEM FELELT MEG",
                       summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateMszEn806BasicData(const PipeNetwork &network) const
    {
        std::vector<ValidationMessage> result;

        if (network.elements().empty())
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Vízhálózati alapadatok összesítés / NEM FELELT MEG",
                              "A projektben nincs vizsgálható cső- vagy idomelem. EREDMÉNY: NEM FELELT MEG."});

            return result;
        }

        const auto &catalog = network.catalog();

        int checkedElementCount = 0;
        int passedElementCount = 0;
        int failedElementCount = 0;

        for (const auto &[id, element] : network.elements())
        {
            ++checkedElementCount;

            std::vector<std::string> issues;

            const bool diameterValid = std::isfinite(element.diameterMm) && element.diameterMm > 0.0f;

            const bool wallThicknessValid =
                std::isfinite(element.wallThicknessMm) && element.wallThicknessMm > 0.0f;

            float innerDiameterMm = std::numeric_limits<float>::quiet_NaN();

            if (!diameterValid)
            {
                issues.push_back("a külső átmérő nem pozitív vagy nem véges");
            }

            if (!wallThicknessValid)
            {
                issues.push_back("a falvastagság nem pozitív vagy nem véges");
            }

            if (diameterValid && wallThicknessValid)
            {
                innerDiameterMm = element.diameterMm - 2.0f * element.wallThicknessMm;

                if (element.wallThicknessMm >= element.diameterMm * 0.5f)
                {
                    issues.push_back("a falvastagság nem lehet az átmérő fele vagy annál nagyobb");
                }

                if (!std::isfinite(innerDiameterMm) || innerDiameterMm <= 0.0f)
                {
                    issues.push_back("a számított belső átmérő nem pozitív");
                }
            }

            if (element.type == PipeElementType::Reducer)
            {
                const bool secondaryDiameterValid =
                    std::isfinite(element.secondaryDiameterMm) && element.secondaryDiameterMm > 0.0f;

                const bool secondaryWallValid =
                    std::isfinite(element.secondaryWallThicknessMm) && element.secondaryWallThicknessMm > 0.0f;

                if (!secondaryDiameterValid)
                {
                    issues.push_back("a szűkítő 1. végének külső átmérője nem pozitív vagy nem véges");
                }

                if (!secondaryWallValid)
                {
                    issues.push_back("a szűkítő 1. végének falvastagsága nem pozitív vagy nem véges");
                }

                if (secondaryDiameterValid && secondaryWallValid)
                {
                    if (element.secondaryWallThicknessMm >= element.secondaryDiameterMm * 0.5f)
                    {
                        issues.push_back(
                            "a szűkítő 1. végének falvastagsága nem lehet az átmérő fele vagy annál nagyobb");
                    }

                    const float secondaryInnerDiameterMm =
                        element.secondaryDiameterMm - 2.0f * element.secondaryWallThicknessMm;

                    if (!std::isfinite(secondaryInnerDiameterMm) || secondaryInnerDiameterMm <= 0.0f)
                    {
                        issues.push_back("a szűkítő 1. végének számított belső átmérője nem pozitív");
                    }

                    if (std::abs(element.secondaryDiameterMm - element.diameterMm) < 0.001f)
                    {
                        issues.push_back("a szűkítő két csatlakozási átmérője azonos");
                    }
                }
            }

            const bool materialValid = element.material.find_first_not_of(" \t\r\n") != std::string::npos;

            if (!materialValid)
            {
                issues.push_back("az anyag megnevezése hiányzik");
            }

            if (!std::isfinite(element.designPressureBar) || element.designPressureBar <= 0.0f)
            {
                issues.push_back("a tervezési nyomás nem pozitív vagy nem véges");
            }

            if (!std::isfinite(element.lengthM) || element.lengthM <= 0.0f)
            {
                issues.push_back("az elem hossza nem pozitív vagy nem véges");
            }
            else if (element.type == PipeElementType::StraightPipe &&
                     element.lengthM > MaxStraightPipeLengthM + 0.0001f)
            {
                issues.push_back("az egyenes cső hossza meghaladja a megengedett 8.00 m maximumot");
            }

            bool catalogReferenceValid = true;

            if (element.pipeTypeIndex < 0 || element.pipeTypeIndex >= static_cast<int>(catalog.size()))
            {
                catalogReferenceValid = false;

                issues.push_back("a csőtípus katalógusindexe érvénytelen");
            }
            else
            {
                const PipeTypeDefinition &pipeType = catalog[element.pipeTypeIndex];

                if (element.pipeSizeIndex < 0 || element.pipeSizeIndex >= static_cast<int>(pipeType.sizes.size()))
                {
                    catalogReferenceValid = false;

                    issues.push_back("a csőméret katalógusindexe érvénytelen");
                }

                if (element.type == PipeElementType::Reducer)
                {
                    if (element.secondaryPipeSizeIndex < 0 ||
                        element.secondaryPipeSizeIndex >= static_cast<int>(pipeType.sizes.size()))
                    {
                        catalogReferenceValid = false;

                        issues.push_back("a szűkítő 1. végének katalógusméret-indexe érvénytelen");
                    }
                    else if (element.secondaryPipeSizeIndex == element.pipeSizeIndex && pipeType.sizes.size() > 1)
                    {
                        catalogReferenceValid = false;

                        issues.push_back("a szűkítő két oldala ugyanarra a katalógusméretre hivatkozik");
                    }
                }
            }

            const bool passed = issues.empty();

            if (passed)
                ++passedElementCount;
            else
                ++failedElementCount;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << elementTypeName(element.type) << " #" << id
                << ": anyag=" << (materialValid ? element.material : "<hiányzik>")
                << ", külső átmérő=" << element.diameterMm << " mm, falvastagság=" << element.wallThicknessMm
                << " mm, belső átmérő=";

            if (std::isfinite(innerDiameterMm))
                oss << innerDiameterMm << " mm";
            else
                oss << "nem számítható";

            oss << ", tervezési nyomás=" << element.designPressureBar << " bar, hossz=" << std::setprecision(3)
                << element.lengthM << " m";

            if (element.type == PipeElementType::Reducer)
            {
                oss << ", szűkítő 1. vég=" << std::setprecision(2) << element.secondaryDiameterMm << "x"
                    << element.secondaryWallThicknessMm << " mm";
            }

            oss << ", katalógus-hivatkozás=" << (catalogReferenceValid ? "érvényes" : "érvénytelen")
                << ". EREDMÉNY: " << (passed ? "MEGFELELT." : "NEM FELELT MEG.");

            if (!issues.empty())
            {
                oss << " Hibák: ";

                for (std::size_t issueIndex = 0; issueIndex < issues.size(); ++issueIndex)
                {
                    if (issueIndex > 0) oss << "; ";

                    oss << issues[issueIndex];
                }

                oss << ".";
            }

            result.push_back(
                {passed ? ValidationSeverity::Info : ValidationSeverity::Error, id, -1,
                 passed ? "Vízhálózati alapadatok / MEGFELELT" : "Vízhálózati alapadatok / NEM FELELT MEG",
                 oss.str()});
        }

        const bool overallPassed = failedElementCount == 0;

        std::ostringstream summary;
        summary << "Vízhálózati alapadat-ellenőrzés. "
                << "Vizsgált elemek: " << checkedElementCount << ", megfelelt: " << passedElementCount
                << ", hibás: " << failedElementCount
                << ". EREDMÉNY: " << (overallPassed ? "MEGFELELT." : "NEM FELELT MEG.");

        appendSummary(result,
                      {overallPassed ? ValidationSeverity::Info : ValidationSeverity::Error, InvalidObjectId, -1,
                       overallPassed ? "Vízhálózati alapadatok összesítés / MEGFELELT"
                                     : "Vízhálózati alapadatok összesítés / NEM FELELT MEG",
                       summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateMszEn806PressureTest(
        const PipeNetwork &network, ObjectId networkRootId, float designPressureBar, float testPressureBar,
        float holdTimeMin, float initialMeasuredPressureBar, float finalMeasuredPressureBar,
        float allowedPressureDropBar) const
    {
        std::vector<ValidationMessage> result;

        const PipeElement *root = network.find(networkRootId);

        if (!root)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1, "Projekt nyomáspróba hálózat",
                              "A nyomáspróbához előbb válassz Validáció START pontot. "
                              "A program a START elem teljes kapcsolt hálózatrészét vizsgálja."});

            return result;
        }

        if (designPressureBar <= 0.0f || testPressureBar <= 0.0f || holdTimeMin <= 0.0f ||
            initialMeasuredPressureBar < 0.0f || finalMeasuredPressureBar < 0.0f || allowedPressureDropBar < 0.0f)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << "Érvénytelen nyomáspróba bemeneti adat. "
                << "Tervezési nyomás=" << designPressureBar << " bar, próbanyomás=" << testPressureBar
                << " bar, tartási idő=" << holdTimeMin
                << " perc, kezdeti mért nyomás=" << initialMeasuredPressureBar
                << " bar, végső mért nyomás=" << finalMeasuredPressureBar
                << " bar, megengedett nyomásesés=" << allowedPressureDropBar
                << " bar. A nyomások és a tartási idő legyenek érvényesek, "
                << "a megengedett nyomásesés pedig nem lehet negatív.";

            result.push_back(
                {ValidationSeverity::Error, networkRootId, -1, "Projekt nyomáspróba bemeneti adatok", oss.str()});

            return result;
        }

        if (finalMeasuredPressureBar > initialMeasuredPressureBar + 0.0001f)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << "A végső mért nyomás (" << finalMeasuredPressureBar
                << " bar) nagyobb a kezdeti mért nyomásnál (" << initialMeasuredPressureBar
                << " bar). Ebben a prototípus nyomásesés-vizsgálatban "
                << "ez érvénytelen mérési adatsornak számít.";

            result.push_back(
                {ValidationSeverity::Error, networkRootId, -1, "Projekt nyomáspróba mérési sorrend", oss.str()});

            return result;
        }

        // A referenciaelemből bejárjuk a teljes kapcsolt hálózatrészt.
        std::vector<ObjectId> component;
        std::unordered_set<ObjectId> visited;
        std::queue<ObjectId> queue;

        visited.insert(networkRootId);
        queue.push(networkRootId);

        while (!queue.empty())
        {
            const ObjectId currentId = queue.front();
            queue.pop();

            component.push_back(currentId);

            const PipeElement *element = network.find(currentId);

            if (!element) continue;

            for (const ConnectionPoint &connection : element->connections)
            {
                if (connection.isOpen()) continue;

                const ObjectId nextId = connection.connectedElementId;

                if (!network.find(nextId)) continue;

                if (visited.insert(nextId).second) queue.push(nextId);
            }
        }

        if (component.empty())
        {
            result.push_back({ValidationSeverity::Error, networkRootId, -1, "Projekt nyomáspróba hálózat",
                              "A Validáció START elemből nem építhető vizsgálható hálózatrész."});

            return result;
        }

        bool networkDesignDataValid = true;
        int underRatedElementCount = 0;

        for (ObjectId id : component)
        {
            const PipeElement *element = network.find(id);

            if (!element) continue;

            if (element->designPressureBar <= 0.0f)
            {
                networkDesignDataValid = false;

                std::ostringstream oss;
                oss << elementTypeName(element->type) << " #" << element->id << " tervezési nyomása nem érvényes.";

                result.push_back({ValidationSeverity::Error, element->id, -1,
                                  "Projekt nyomáspróba - Hálózati elem tervezési nyomása", oss.str()});

                continue;
            }

            if (element->designPressureBar < designPressureBar - 0.0001f)
            {
                networkDesignDataValid = false;
                ++underRatedElementCount;

                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << elementTypeName(element->type) << " #" << element->id
                    << " beállított tervezési nyomása " << element->designPressureBar
                    << " bar, ami kisebb a vizsgált hálózat " << designPressureBar << " bar tervezési nyomásánál.";

                result.push_back({ValidationSeverity::Error, element->id, -1,
                                  "Projekt nyomáspróba - Hálózati elem nyomásbesorolása", oss.str()});
            }
        }

        const bool testPressureReached = testPressureBar >= designPressureBar - 0.0001f;

        if (!testPressureReached)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << "A megadott próbanyomás " << testPressureBar
                << " bar, ami kisebb a hálózat " << designPressureBar << " bar tervezési nyomásánál. "
                << "A program itt nem használ beégetett szabványos szorzót; "
                << "az ellenőrzés minimuma a megadott tervezési nyomás.";

            result.push_back(
                {ValidationSeverity::Error, networkRootId, -1, "Projekt nyomáspróba - Próbanyomás", oss.str()});
        }

        const float measuredPressureDropBar = initialMeasuredPressureBar - finalMeasuredPressureBar;

        const bool pressureDropPassed = measuredPressureDropBar <= allowedPressureDropBar + 0.0001f;

        if (!pressureDropPassed)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << "Számított nyomásesés: " << measuredPressureDropBar
                << " bar, megengedett maximum: " << allowedPressureDropBar << " bar. EREDMÉNY: NEM FELELT MEG.";

            result.push_back({ValidationSeverity::Error, networkRootId, -1,
                              "Projekt nyomáspróba - Nyomásesés / NEM FELELT MEG", oss.str()});
        }

        const bool overallPassed = networkDesignDataValid && testPressureReached && pressureDropPassed;

        std::ostringstream summary;
        summary << std::fixed << std::setprecision(3) << "Vizsgált kapcsolt hálózatrész: " << component.size()
                << " elem. Tervezési nyomás: " << designPressureBar << " bar. Próbanyomás: " << testPressureBar
                << " bar. Tartási idő: " << holdTimeMin
                << " perc. Kezdeti mért nyomás: " << initialMeasuredPressureBar
                << " bar. Végső mért nyomás: " << finalMeasuredPressureBar
                << " bar. Számított nyomásesés: " << measuredPressureDropBar
                << " bar. Megengedett nyomásesés: " << allowedPressureDropBar
                << " bar. Alacsonyabb tervezési nyomású hálózati elemek: " << underRatedElementCount
                << ". EREDMÉNY: " << (overallPassed ? "MEGFELELT." : "NEM FELELT MEG.");

        appendSummary(result,
                      {overallPassed ? ValidationSeverity::Info : ValidationSeverity::Error, networkRootId, -1,
                       overallPassed ? "Projekt nyomáspróba összesítés / MEGFELELT"
                                     : "Projekt nyomáspróba összesítés / NEM FELELT MEG",
                       summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateMszEn806FlowVelocity(
        const PipeNetwork &network, const std::vector<SceneObject> &objects, ObjectId startId, int startEndpoint,
        ObjectId endId, int endEndpoint, float flowRateLps) const
    {
        std::vector<ValidationMessage> result;

        if (startId == InvalidObjectId || endId == InvalidObjectId || startEndpoint < 0 || endEndpoint < 0)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Nyílt vízellátási referencia - Kezdő- és végpont",
                              "Az áramlási sebesség ellenőrzéséhez válassz közös Validáció START és END pontot."});
            return result;
        }
        if (startId == endId && startEndpoint == endEndpoint)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Nyílt vízellátási referencia - Kezdő- és végpont",
                              "A közös Validáció START és END pont nem lehet ugyanaz."});
            return result;
        }
        if (!std::isfinite(flowRateLps) || flowRateLps <= 0.0f)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Nyílt vízellátási referencia - Térfogatáram",
                              "Pozitív START -> END térfogatáram szükséges."});
            return result;
        }

        std::vector<NetworkEndpointNode> path;
        if (!buildConnectedEndpointPath(network, objects, startId, startEndpoint, endId, endEndpoint, path))
        {
            result.push_back(
                {ValidationSeverity::Error, startId, startEndpoint,
                 "Nyílt vízellátási referencia - Áramlási útvonal",
                 "A kiválasztott START és END pont között nincs végig kapcsolt csőhálózati útvonal."});
            return result;
        }

        int evaluated = 0, recommended = 0, warnings = 0, failed = 0, invalid = 0;
        constexpr float Pi = 3.14159265359f;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            const auto &from = path[i];
            const auto &to = path[i + 1];
            if (from.objectId != to.objectId || from.endpoint == to.endpoint) continue;
            const PipeElement *element = network.find(from.objectId);
            if (!element || element->type != PipeElementType::StraightPipe) continue;
            ++evaluated;
            const float di = (element->diameterMm - 2.0f * element->wallThicknessMm) / 1000.0f;
            if (!(di > 0.0f) || !std::isfinite(di))
            {
                ++invalid;
                result.push_back({ValidationSeverity::Error, element->id, from.endpoint,
                                  "Nyílt vízellátási referencia - Belső átmérő",
                                  "A belső átmérő nem pozitív vagy nem számítható."});
                continue;
            }
            const float v = (flowRateLps / 1000.0f) / (Pi * di * di * 0.25f);
            if (!std::isfinite(v))
            {
                ++invalid;
                result.push_back({ValidationSeverity::Error, element->id, from.endpoint,
                                  "Nyílt vízellátási referencia - Sebesség",
                                  "A számított vízsebesség nem véges."});
                continue;
            }
            ValidationSeverity sev = ValidationSeverity::Info;
            std::string outcome = "AJÁNLOTT";
            if (v > OpenWaterMaximumVelocityMps + 0.0001f)
            {
                sev = ValidationSeverity::Error;
                outcome = "NEM FELELT MEG";
                ++failed;
            }
            else if (v > OpenWaterRecommendedVelocityMps + 0.0001f)
            {
                sev = ValidationSeverity::Warning;
                outcome = "FIGYELMEZTETÉS";
                ++warnings;
            }
            else
                ++recommended;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(3) << "Egyenes cső #" << element->id << ": Di=" << di * 1000.0f
                << " mm, Q=" << flowRateLps << " l/s, v=" << v
                << " m/s. Nyílt referencia: <=1.5 m/s konzervatív tervezési cél; "
                << "<=3.0 m/s NCC hidegvíz felső teljesítményhatár. EREDMÉNY: " << outcome << ".";
            result.push_back({sev, element->id, from.endpoint,
                              sev == ValidationSeverity::Error
                                  ? "Nyílt vízellátási referencia - Sebesség / NEM FELELT MEG"
                                  : (sev == ValidationSeverity::Warning
                                         ? "Nyílt vízellátási referencia - Sebesség / FIGYELMEZTETÉS"
                                         : "Nyílt vízellátási referencia - Sebesség / AJÁNLOTT"),
                              oss.str()});
        }
        if (evaluated <= 0)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Nyílt vízellátási referencia - Áramlási útvonal",
                              "A START -> END útvonalon nincs vizsgálható egyenes csőszakasz."});
            return result;
        }
        const bool ok = failed == 0 && invalid == 0;
        std::ostringstream sum;
        sum << "Nyílt vízellátási sebesség-referencia. Vizsgált csövek: " << evaluated
            << ", <=1.5 m/s: " << recommended << ", 1.5-3.0 m/s figyelmeztetés: " << warnings
            << ", >3.0 m/s hiba: " << failed << ", nem számítható: " << invalid
            << ". EREDMÉNY: " << (ok ? "MEGFELELT A FELSŐ HATÁRNAK." : "NEM FELELT MEG.");
        appendSummary(
            result,
            {ok ? (warnings ? ValidationSeverity::Warning : ValidationSeverity::Info) : ValidationSeverity::Error,
             endId, endEndpoint, "Nyílt vízellátási referencia - Sebesség összesítés", sum.str()});
        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateMszEn1610Drainage(
        const PipeNetwork &network, const std::vector<SceneObject> &objects, ObjectId startId, int startEndpoint,
        ObjectId endId, int endEndpoint, float peakFlowRateLps) const
    {
        std::vector<ValidationMessage> result;
        if (startId == InvalidObjectId || endId == InvalidObjectId || startEndpoint < 0 || endEndpoint < 0)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Nyílt lefolyási referencia - Kezdő- és végpont",
                              "A gravitációs ellenőrzéshez válassz közös Validáció START és END pontot."});
            return result;
        }
        if (startId == endId && startEndpoint == endEndpoint)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Nyílt lefolyási referencia - Kezdő- és végpont",
                              "A START és END nem lehet ugyanaz."});
            return result;
        }
        if (!std::isfinite(peakFlowRateLps) || peakFlowRateLps <= 0.0f)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Nyílt lefolyási referencia - Csúcstérfogatáram",
                              "Adj meg nullánál nagyobb csúcstérfogatáramot l/s-ban a lefolyás ellenőrzéséhez."});
            return result;
        }
        std::vector<NetworkEndpointNode> path;
        if (!buildConnectedEndpointPath(network, objects, startId, startEndpoint, endId, endEndpoint, path))
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Nyílt lefolyási referencia - Lefolyási útvonal",
                              "A kiválasztott START és END között nincs végig kapcsolt útvonal."});
            return result;
        }
        int evaluated = 0, passed = 0, warnings = 0, failed = 0, skipped = 0;
        int unsupportedSize = 0;
        int invalidGeometry = 0;
        int rising = 0;
        int insufficientSlope = 0;
        int insufficientCapacity = 0;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            const auto &from = path[i];
            const auto &to = path[i + 1];
            if (from.objectId != to.objectId || from.endpoint == to.endpoint) continue;
            const PipeElement *element = network.find(from.objectId);
            const SceneObject *object = findValidationSceneObject(objects, from.objectId);
            if (!element || !object)
            {
                ++failed;
                ++invalidGeometry;
                continue;
            }
            if (element->type != PipeElementType::StraightPipe) continue;
            if (!element->drainageCheckEnabled)
            {
                ++skipped;
                continue;
            }
            ++evaluated;
            const auto lookup = lookupOpenDrainageReference(element->diameterMm, peakFlowRateLps);

            if (!lookup.valid)
            {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << "HIBA: a kiválasztott Ø" << element->diameterMm
                    << " mm csőmérethez nincs alkalmazható Table 6 lefolyási sor. "
                    << "A minimális használható referencia-méret DN75. "
                    << "Javítás: válassz legalább PVC-U Ø75 mm lefolyócsövet. "
                    << "Érintett elem: #" << element->id << ".";

                result.push_back({ValidationSeverity::Error, element->id, from.endpoint,
                                  "Nyílt lefolyási referencia - Nincs táblázati sor", oss.str()});

                ++failed;
                ++unsupportedSize;
                continue;
            }

            const bool foulDrainMaterial = isFoulDrainReferenceMaterial(*element);
            const auto centerline =
                validationElementWorldCenterline(*element, *object, from.endpoint, to.endpoint);
            const auto check = checkDrainageAngles(centerline);
            if (!check.valid || check.checkedSegmentCount <= 0)
            {
                result.push_back(
                    {ValidationSeverity::Error, element->id, from.endpoint,
                     "Nyílt lefolyási referencia - Geometria",
                     "HIBA: a cső lejtése a jelenlegi geometriából nem határozható meg. Ellenőrizd a cső hosszát és START -> END irányát."});
                ++failed;
                ++invalidGeometry;
                continue;
            }
            if (check.risesAnywhere)
            {
                result.push_back(
                    {ValidationSeverity::Error, element->id, from.endpoint,
                     "Nyílt lefolyási referencia - Visszaemelkedés / NEM FELELT MEG",
                     "HIBA: a cső a START -> END lefolyási irányban emelkedik, ezért a gravitációs víz nem tud végig lefelé haladni. Javítás: fordítsd meg a lejtést úgy, hogy END felé folyamatosan csökkenjen a magasság."});
                ++failed;
                ++rising;
                continue;
            }
            const float reqDeg = gradientDenominatorToAngleDeg(lookup.row.gradientDenominator);
            const bool gradOk = check.minimumAngleDeg >= reqDeg - 0.01f;
            const bool capOk = peakFlowRateLps <= lookup.row.capacityLps + 0.0001f;
            ValidationSeverity sev = ValidationSeverity::Info;

            if (!gradOk || !capOk)
            {
                sev = ValidationSeverity::Error;
                ++failed;
                insufficientSlope += !gradOk;
                insufficientCapacity += !capOk;
            }
            else if (lookup.conservativeLargerDiameterFallback || !foulDrainMaterial)
            {
                sev = ValidationSeverity::Warning;
                ++warnings;
            }
            else
            {
                ++passed;
            }
            std::ostringstream oss;
            oss << std::fixed;

            if (!gradOk || !capOk)
            {
                if (!gradOk)
                {
                    oss << "HIBA: a cső lejtése túl kicsi. ";
                    if (std::abs(check.minimumAngleDeg) <= 0.001f)
                    {
                        oss << "A cső vízszintes (0% lejtés). ";
                    }
                    else
                    {
                        const float gradient = std::tan(check.minimumAngleDeg * 3.14159265359f / 180.0f);
                        oss << "Tényleges lejtés: " << std::setprecision(2) << gradient * 100.0f << "%";
                        if (gradient > 0.0f) oss << " (kb. 1:" << std::setprecision(0) << 1.0f / gradient << ")";
                        oss << ". ";
                    }
                    oss << "Elvárt minimum: 1:" << std::setprecision(0) << lookup.row.gradientDenominator << " ("
                        << std::setprecision(2) << 100.0f / lookup.row.gradientDenominator << "%).";

                    if (!capOk) oss << "\n";
                }

                if (!capOk)
                {
                    oss << std::setprecision(2) << "HIBA: a cső kapacitása nem elegendő. "
                        << "Megadott csúcstérfogatáram: " << peakFlowRateLps
                        << " l/s. A cső megengedett kapacitása: " << lookup.row.capacityLps << " l/s.";
                }

                oss << "\nJavítás: ";

                if (!gradOk && !capOk)
                {
                    oss << "növeld a lejtést és/vagy válassz nagyobb lefolyócső-méretet.";
                }
                else if (!gradOk)
                {
                    oss << "növeld a cső lejtését legalább 1:" << std::setprecision(0)
                        << lookup.row.gradientDenominator << " értékre.";
                }
                else
                {
                    oss << "válassz nagyobb kapacitású lefolyócső-méretet.";
                }

                oss << "\nÉrintett elem: #" << element->id << " | PVC-U referencia: DN" << std::setprecision(0)
                    << lookup.referenceNominalDiameterMm << ".";
            }
            else if (!foulDrainMaterial)
            {
                oss << "FIGYELMEZTETÉS: a Table 6 lefolyási ellenőrzés teljesült, "
                    << "de a kiválasztott cső anyaga " << element->material << ", nem PVC-U lefolyócső. "
                    << "Érintett elem: #" << element->id << ".";
            }
            else if (lookup.conservativeLargerDiameterFallback)
            {
                oss << "FIGYELMEZTETÉS: ehhez a DN150 / Q<1 l/s esethez nincs külön Table 6 sor. "
                    << "A program konzervatívan a DN100 1:40 követelményt alkalmazta. "
                    << "Érintett elem: #" << element->id << ".";
            }
            else
            {
                oss << "MEGFELELT: a lejtés és a Table 6 kapacitás megfelelő. "
                    << "Érintett elem: #" << element->id << ".";
            }
            result.push_back({sev, element->id, from.endpoint,
                              sev == ValidationSeverity::Error
                                  ? "Nyílt lefolyási referencia - Table 6 / NEM FELELT MEG"
                                  : (sev == ValidationSeverity::Warning
                                         ? "Nyílt lefolyási referencia - Table 6 / FIGYELMEZTETÉS"
                                         : "Nyílt lefolyási referencia - Table 6 / MEGFELELT"),
                              oss.str()});
        }
        if (evaluated <= 0)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Nyílt lefolyási referencia - Nincs vizsgálható cső",
                              "A START -> END útvonalon nincs bekapcsolt Table 6 vizsgálatú egyenes cső."});
            return result;
        }
        std::ostringstream summary;
        summary << "Gravitációs lefolyás ellenőrzése, " << std::fixed << std::setprecision(3) << peakFlowRateLps
                << " l/s csúcstérfogatáramnál.\n"
                << "Ellenőrzött szakaszok: " << evaluated << ". Megfelelt: " << passed
                << ". Figyelmeztetés: " << warnings << ". Hibás: " << failed << ".";
        if (failed > 0)
        {
            summary << "\nHibaokok az érintett szakaszok száma szerint:";
            if (insufficientSlope > 0) summary << "\nTúl kicsi lejtés: " << insufficientSlope << ".";
            if (rising > 0) summary << "\nEmelkedés a lefolyás irányában: " << rising << ".";
            if (insufficientCapacity > 0) summary << "\nElégtelen kapacitás: " << insufficientCapacity << ".";
            if (unsupportedSize > 0)
                summary << "\nA táblázatban nem szereplő csőméret: " << unsupportedSize << ".";
            if (invalidGeometry > 0) summary << "\nNem ellenőrizhető geometria: " << invalidGeometry << ".";
        }
        if (skipped > 0) summary << "\nKihagyott szakaszok (ellenőrzés kikapcsolva): " << skipped << ".";
        summary << "\nA vizsgálat a lejtést és a kapacitást az Approved Document H 6. táblázatához hasonlítja.";
        const auto severity = failed > 0     ? ValidationSeverity::Error
                              : warnings > 0 ? ValidationSeverity::Warning
                                             : ValidationSeverity::Info;
        appendSummary(result, {severity, endId, endEndpoint,
                               "Nyílt lefolyási referencia - Gravitációs lefolyás összesítése", summary.str()});
        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateDarcyWeisbachPressureLoss(
        const PipeNetwork &network, const std::vector<SceneObject> &objects, ObjectId startId, int startEndpoint,
        ObjectId endId, int endEndpoint, float flowRateLps, float densityKgM3, float kinematicViscosityM2s,
        float maxTotalPressureLossBar) const
    {
        std::vector<ValidationMessage> result;

        if (startId == InvalidObjectId || endId == InvalidObjectId || startEndpoint < 0 || endEndpoint < 0)
        {
            result.push_back(
                {ValidationSeverity::Error, InvalidObjectId, -1, "Darcy-Weisbach - Kezdő- és végpont",
                 "A Darcy-Weisbach számításhoz válassz közös Validáció START és Validáció END pontot."});
            return result;
        }

        if (startId == endId && startEndpoint == endEndpoint)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Darcy-Weisbach - Kezdő- és végpont",
                              "A közös Validáció START és END pont nem lehet ugyanaz."});
            return result;
        }

        if (flowRateLps <= 0.0f)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1, "Darcy-Weisbach - Térfogatáram",
                              "Pozitív START -> END térfogatáram szükséges."});
            return result;
        }

        if (densityKgM3 <= 0.0f || kinematicViscosityM2s <= 0.0f)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1, "Darcy-Weisbach - Folyadékadatok",
                              "A folyadék sűrűségének és kinematikai viszkozitásának pozitívnak kell lennie."});
            return result;
        }

        if (maxTotalPressureLossBar <= 0.0f)
        {
            result.push_back({ValidationSeverity::Error, InvalidObjectId, -1,
                              "Darcy-Weisbach - Megengedett teljes nyomásveszteség",
                              "A megengedett teljes nyomásveszteségnek pozitívnak kell lennie."});
            return result;
        }

        std::vector<NetworkEndpointNode> path;
        if (!buildConnectedEndpointPath(network, objects, startId, startEndpoint, endId, endEndpoint, path))
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Darcy-Weisbach - Hidraulikai útvonal",
                              "A kiválasztott START és END között nincs végig kapcsolt csőhálózati útvonal."});
            return result;
        }

        float majorTotal = 0.0f;
        float minorTotal = 0.0f;
        int evaluated = 0;

        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            const NetworkEndpointNode &from = path[i];
            const NetworkEndpointNode &to = path[i + 1];

            if (from.objectId != to.objectId || from.endpoint == to.endpoint) continue;

            const PipeElement *element = network.find(from.objectId);
            if (!element) continue;

            const HydraulicElementResult h = calculateHydraulicElementLoss(
                *element, from.endpoint, to.endpoint, flowRateLps, densityKgM3, kinematicViscosityM2s);

            if (!h.success)
            {
                std::ostringstream oss;
                oss << elementTypeName(element->type) << " #" << element->id
                    << " vesztesége nem számítható. Ellenőrizd az átmérőt és falvastagságot";
                if (element->type == PipeElementType::StraightPipe) oss << ", valamint a csőérdességet";
                oss << ".";

                result.push_back({ValidationSeverity::Error, element->id, from.endpoint,
                                  "Darcy-Weisbach - Elem számíthatósága", oss.str()});
                continue;
            }

            majorTotal += h.majorLossBar;
            minorTotal += h.minorLossBar;
            ++evaluated;

            std::ostringstream oss;
            oss << std::fixed << std::setprecision(4) << elementTypeName(element->type) << " #" << element->id
                << ": Δp=" << h.totalLossBar << " bar";

            if (element->type == PipeElementType::StraightPipe)
            {
                oss << " (súrlódás=" << h.majorLossBar << " bar, L=" << std::setprecision(3) << element->lengthM
                    << " m, Di=" << h.innerDiameterM * 1000.0f << " mm, v=" << h.velocityMps
                    << " m/s, Re=" << std::setprecision(0) << h.reynolds << ", f=" << std::setprecision(4)
                    << h.frictionFactor << ")";
            }
            else
            {
                oss << " (helyi veszteség, K=" << std::setprecision(2) << h.k << ", v=" << std::setprecision(3)
                    << h.velocityMps << " m/s)";
            }

            result.push_back({ValidationSeverity::Info, element->id, from.endpoint,
                              "Darcy-Weisbach - Elemveszteség", oss.str()});
        }

        if (evaluated <= 0)
        {
            result.push_back({ValidationSeverity::Error, startId, startEndpoint,
                              "Darcy-Weisbach - Hidraulikai útvonal",
                              "A START -> END útvonalon nincs kiértékelhető cső- vagy idomátfolyás."});
            return result;
        }

        const float total = majorTotal + minorTotal;
        std::ostringstream summary;
        summary << std::fixed << std::setprecision(4) << "START -> END teljes nyomásveszteség: " << total
                << " bar. Egyenes csövek súrlódása: " << majorTotal
                << " bar, idomok helyi vesztesége: " << minorTotal
                << " bar. Megengedett maximum: " << maxTotalPressureLossBar
                << " bar. Térfogatáram: " << std::setprecision(3) << flowRateLps << " l/s.";

        result.push_back({total > maxTotalPressureLossBar ? ValidationSeverity::Warning : ValidationSeverity::Info,
                          endId, endEndpoint,
                          total > maxTotalPressureLossBar ? "Darcy-Weisbach - Teljes nyomásveszteség túllépés"
                                                          : "Darcy-Weisbach - Teljes nyomásveszteség",
                          summary.str()});

        return result;
    }

    std::vector<ValidationMessage> NetworkValidator::validateNetwork(
        const PipeNetwork &network, const std::vector<SceneObject> &objects, float pressureDesignPressureBar,
        float pressureTestPressureBar, float pressureHoldTimeMin, float pressureInitialMeasuredPressureBar,
        float pressureFinalMeasuredPressureBar, float pressureAllowedDropBar, ObjectId validationStartId,
        int validationStartEndpoint, ObjectId validationEndId, int validationEndEndpoint, float flowRateLps,
        float hydraulicFlowRateLps, float hydraulicDensityKgM3, float hydraulicKinematicViscosityM2s,
        float hydraulicMaxTotalPressureLossBar) const
    {
        std::vector<ValidationMessage> result = validateOpenEnds(network);

        std::vector<ValidationMessage> isolated = validateIsolatedElements(network);

        result.insert(result.end(), isolated.begin(), isolated.end());

        std::vector<ValidationMessage> connectedComponents = validateConnectedComponents(network);

        result.insert(result.end(), connectedComponents.begin(), connectedComponents.end());

        std::vector<ValidationMessage> basicData = validateMszEn806BasicData(network);

        result.insert(result.end(), basicData.begin(), basicData.end());

        std::vector<ValidationMessage> pressureTest = validateMszEn806PressureTest(
            network, validationStartId, pressureDesignPressureBar, pressureTestPressureBar, pressureHoldTimeMin,
            pressureInitialMeasuredPressureBar, pressureFinalMeasuredPressureBar, pressureAllowedDropBar);

        result.insert(result.end(), pressureTest.begin(), pressureTest.end());

        std::vector<ValidationMessage> flowVelocity =
            validateMszEn806FlowVelocity(network, objects, validationStartId, validationStartEndpoint,
                                         validationEndId, validationEndEndpoint, flowRateLps);

        result.insert(result.end(), flowVelocity.begin(), flowVelocity.end());

        std::vector<ValidationMessage> drainage =
            validateMszEn1610Drainage(network, objects, validationStartId, validationStartEndpoint,
                                      validationEndId, validationEndEndpoint, flowRateLps);

        result.insert(result.end(), drainage.begin(), drainage.end());

        std::vector<ValidationMessage> pressureLoss = validateDarcyWeisbachPressureLoss(
            network, objects, validationStartId, validationStartEndpoint, validationEndId, validationEndEndpoint,
            hydraulicFlowRateLps, hydraulicDensityKgM3, hydraulicKinematicViscosityM2s,
            hydraulicMaxTotalPressureLossBar);

        result.insert(result.end(), pressureLoss.begin(), pressureLoss.end());

        return result;
    }
}
