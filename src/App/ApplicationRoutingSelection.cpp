#include "App/ApplicationInternal.hpp"
#include "Routing/RouteGeometry.hpp"

namespace plumbcad
{
    using namespace routing;

    RouteCatalogPlan Application::selectRouteCatalog(float startDiameterMm, float endDiameterMm,
                                                     int startType) const
    {
        RouteCatalogPlan plan;
        const auto &catalog = m_network.catalog();
        if (catalog.empty()) return plan;

        plan.pipeType = std::clamp(m_routePipeTypeIndex >= 0 ? m_routePipeTypeIndex : startType, 0,
                                   static_cast<int>(catalog.size()) - 1);
        const auto &routeSizes = catalog[plan.pipeType].sizes;
        if (routeSizes.empty()) return plan;

        const auto nearest = std::min_element(
            routeSizes.begin(), routeSizes.end(), [startDiameterMm](const auto &a, const auto &b) {
                return std::abs(a.diameterMm - startDiameterMm) < std::abs(b.diameterMm - startDiameterMm);
            });
        plan.pipeSize = static_cast<int>(nearest - routeSizes.begin());
        const float routedDiameterMm = nearest->diameterMm;
        if (std::abs(routedDiameterMm - endDiameterMm) < 0.001f) return plan;

        // Prefer the route category. Another catalogue is usable only if it
        // contains both exact diameters of the transition.
        for (int offset = 0; offset < static_cast<int>(catalog.size()); ++offset)
        {
            const int type = (plan.pipeType + offset) % static_cast<int>(catalog.size());
            const auto &sizes = catalog[type].sizes;
            const auto findDiameter = [&](float diameter) {
                return std::find_if(sizes.begin(), sizes.end(), [diameter](const auto &size) {
                    return std::abs(size.diameterMm - diameter) < 0.001f;
                });
            };
            const auto inlet = findDiameter(routedDiameterMm);
            const auto outlet = findDiameter(endDiameterMm);
            if (inlet != sizes.end() && outlet != sizes.end())
            {
                plan.reducerType = type;
                plan.reducerInletSize = static_cast<int>(inlet - sizes.begin());
                plan.reducerOutletSize = static_cast<int>(outlet - sizes.begin());
                break;
            }
        }
        return plan;
    }

    void Application::clearRouteSelection()
    {
        m_routeStartId = InvalidObjectId;
        m_routeStartEndpoint = -1;
        m_routeEndId = InvalidObjectId;
        m_routeEndEndpoint = -1;
        m_routePipeTypeIndex = -1;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        updateEndpointMarkers();
        setStatusMessage("Az automatikus útvonal kezdő- és végpontja törölve.");
    }

    void Application::setRouteStart(ObjectId objectId, int endpointIndex)
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe || endpointIndex < 0 || endpointIndex >= static_cast<int>(pipe->connections.size()) ||
            !pipe->connections[endpointIndex].isOpen())
        {
            setStatusMessage("Útvonal kezdőpont csak nyitott csővég lehet.");
            return;
        }
        m_routeStartId = objectId;
        m_routeStartEndpoint = endpointIndex;

        // Alapértelmezésben a START elem kategóriáját követjük,
        // ezt az Útvonal panelen a felhasználó felülírhatja.
        m_routePipeTypeIndex = pipe->pipeTypeIndex;

        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        updateEndpointMarkers();
        setStatusMessage("Automatikus útvonal kezdőpont kiválasztva.");
    }

    void Application::setRouteEnd(ObjectId objectId, int endpointIndex)
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe || endpointIndex < 0 || endpointIndex >= static_cast<int>(pipe->connections.size()) ||
            !pipe->connections[endpointIndex].isOpen())
        {
            setStatusMessage("Útvonal végpont csak nyitott csővég lehet.");
            return;
        }
        if (objectId == m_routeStartId && endpointIndex == m_routeStartEndpoint)
        {
            setStatusMessage("A kezdő- és végpont nem lehet ugyanaz a csővég.");
            return;
        }
        m_routeEndId = objectId;
        m_routeEndEndpoint = endpointIndex;
        m_routeAlternatives.clear();
        m_selectedRouteAlternative = 0;
        clearRoutePreview();
        updateEndpointMarkers();
        setStatusMessage("Automatikus útvonal végpont kiválasztva.");
    }

    glm::vec3 Application::routeEndpointOutwardDirection(ObjectId objectId, int endpointIndex) const
    {
        const PipeElement *pipe = m_network.find(objectId);
        if (!pipe) return glm::vec3{0.0f, 0.0f, 1.0f};
        if (pipe->type == PipeElementType::StraightPipe)
        {
            // A straightContinuationDirectionWorld a végponttól a cső belseje felé mutat,
            // ezért az automatikus útvonalhoz az ellenkező irány kell.
            return -straightContinuationDirectionWorld(objectId, endpointIndex);
        }
        if (isDirectionalFittingType(pipe->type))
        {
            return fittingConnectorDirectionWorld(objectId, endpointIndex);
        }
        return glm::vec3{0.0f, 0.0f, 1.0f};
    }

}
