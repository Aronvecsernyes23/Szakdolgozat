#include "App/ApplicationInternal.hpp"
#include "Routing/RouteGeometry.hpp"

namespace plumbcad
{
    using namespace routing;

    void Application::clearRoutePreview()
    {
        m_routePreviewObjects.clear();
    }

    void Application::rebuildRoutePreview()
    {
        clearRoutePreview();

        if (m_routeAlternatives.empty() || m_selectedRouteAlternative < 0 ||
            m_selectedRouteAlternative >= static_cast<int>(m_routeAlternatives.size()))
        {
            return;
        }

        const PipeElement *startPipe = m_network.find(m_routeStartId);

        if (!startPipe) return;

        const float routeStartDiameterMm = pipeEndpointDiameterMm(*startPipe, m_routeStartEndpoint);

        const float bendRadius = std::max(0.12f, (routeStartDiameterMm / 1000.0f) * 1.5f);

        const RouteAlternative &alternative = m_routeAlternatives[m_selectedRouteAlternative];
        const auto &catalogPlan = alternative.catalogPlan;
        const auto &catalog = m_network.catalog();
        const float previewDiameterMm = catalog[catalogPlan.pipeType].sizes[catalogPlan.pipeSize].diameterMm;

        const float actualPipeRadius = std::max(0.01f, (previewDiameterMm / 1000.0f) * 0.5f);

        const float previewRadius = std::clamp(actualPipeRadius * 0.45f, 0.010f, 0.035f);

        std::vector<RouteLineSegment> previewSegments =
            buildRouteBuildPlanCenterlineSegments(alternative.buildPrimitives, bendRadius);

        constexpr ObjectId RoutePreviewBaseId = 3900000000u;

        for (std::size_t i = 0; i < previewSegments.size(); ++i)
        {
            const RouteLineSegment &line = previewSegments[i];

            const glm::vec3 delta = line.b - line.a;

            const float length = glm::length(delta);

            if (length < 0.0001f) continue;

            const glm::vec3 direction = delta / length;

            SceneObject preview;
            preview.id = RoutePreviewBaseId + static_cast<ObjectId>(i);

            preview.name = "A* útvonal előnézet";

            preview.mesh = MeshFactory::makeCylinder(previewRadius, length, 12);

            preview.color = pipeCategoryColor(catalogPlan.pipeType);

            preview.selectable = false;

            preview.transform.position = (line.a + line.b) * 0.5f;

            setObjectRotationFromLocalDirection(preview, glm::vec3{0.0f, 0.0f, 1.0f}, direction);

            m_routePreviewObjects.push_back(preview);

            m_renderer.uploadObject(m_routePreviewObjects.back());
        }

        if (catalogPlan.reducerType >= 0)
        {
            const auto &reducerSizes = catalog[catalogPlan.reducerType].sizes;
            const float inletRadius = reducerSizes[catalogPlan.reducerInletSize].diameterMm * 0.0005f;
            const float outletRadius = reducerSizes[catalogPlan.reducerOutletSize].diameterMm * 0.0005f;
            const glm::vec3 delta = alternative.endEndpointWorld - alternative.mainRouteEndWorld;
            SceneObject reducer;
            reducer.id = RoutePreviewBaseId + static_cast<ObjectId>(previewSegments.size());
            reducer.name = "A* END szűkítő előnézet";
            reducer.mesh = MeshFactory::makeReducer(inletRadius, outletRadius, glm::length(delta));
            reducer.color = pipeCategoryColor(catalogPlan.reducerType);
            reducer.selectable = false;
            reducer.transform.position = (alternative.endEndpointWorld + alternative.mainRouteEndWorld) * 0.5f;
            setObjectRotationFromLocalDirection(reducer, {0.0f, 0.0f, 1.0f}, delta);
            m_routePreviewObjects.push_back(std::move(reducer));
            m_renderer.uploadObject(m_routePreviewObjects.back());
        }
    }
}
