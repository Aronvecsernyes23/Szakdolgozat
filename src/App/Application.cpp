#include "App/ApplicationInternal.hpp"

namespace plumbcad
{
    Application::Application()
    {
        initWindow();
        m_renderer.init();
        initImGui();
        seedScene();
    }
}
