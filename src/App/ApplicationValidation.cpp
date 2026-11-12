#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    void Application::setValidationStart(ObjectId id, int endpoint)
    {
        const PipeElement *pipe = m_network.find(id);
        if (!pipe || endpoint < 0 || endpoint >= static_cast<int>(pipe->connections.size()))
        {
            setStatusMessage("Érvénytelen közös validációs kezdőpont.");
            return;
        }

        m_validationStartId = id;
        m_validationStartEndpoint = endpoint;
        m_validationHasRun = false;
        m_validationMessages.clear();
        updateEndpointMarkers();
        setStatusMessage(
            "Validációs START kiválasztva: #" + std::to_string(id) + " / " + std::to_string(endpoint) +
            ". vég. A nyomáspróba hálózatrésze, az áramlás, a lejtés és a Darcy-vizsgálat ezt használja.");
    }

    void Application::setValidationEnd(ObjectId id, int endpoint)
    {
        const PipeElement *pipe = m_network.find(id);
        if (!pipe || endpoint < 0 || endpoint >= static_cast<int>(pipe->connections.size()))
        {
            setStatusMessage("Érvénytelen közös validációs végpont.");
            return;
        }

        m_validationEndId = id;
        m_validationEndEndpoint = endpoint;
        m_validationHasRun = false;
        m_validationMessages.clear();
        updateEndpointMarkers();
        setStatusMessage("Validációs END kiválasztva: #" + std::to_string(id) + " / " + std::to_string(endpoint) +
                         ". vég. Az áramlás, lejtés és Darcy-vizsgálat ezt használja.");
    }

    void Application::clearValidationPath()
    {
        m_validationStartId = InvalidObjectId;
        m_validationStartEndpoint = -1;
        m_validationEndId = InvalidObjectId;
        m_validationEndEndpoint = -1;
        m_validationHasRun = false;
        m_validationMessages.clear();
        updateEndpointMarkers();
        setStatusMessage("Közös validációs START/END pontok törölve.");
    }

}
