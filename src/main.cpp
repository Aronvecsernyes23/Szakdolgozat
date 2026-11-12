#include "App/Application.hpp"
#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        plumbcad::Application app;
        return app.run(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << std::endl;
        return 1;
    }
}
