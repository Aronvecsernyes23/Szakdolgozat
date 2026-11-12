#include "App/Application.hpp"
#include <exception>
#include <iostream>

int main()
{
    try
    {
        plumbcad::Application app;
        return app.run();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}
