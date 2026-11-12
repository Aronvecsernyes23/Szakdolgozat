#include "App/Application.hpp"
#include "Bom/BillOfMaterials.hpp"
#include "Validation/ValidationReport.hpp"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace plumbcad
{
struct FunctionalTests
{
    Application app;
    std::filesystem::path output;
    int passed = 0;
    int failed = 0;

    static void require(bool condition, const char* reason)
    {
        if (!condition) throw std::runtime_error(reason);
    }

    void reset()
    {
        app.clearRouteSelection();
        app.clearValidationPath();
        app.m_network.clear();
        app.m_objects.clear();
        app.m_architecturalObstacles.clear();
        app.m_dragObjectIds.clear();
        app.m_selectedId = InvalidObjectId;
    }

    template<class Test>
    void run(const char* name, Test test)
    {
        std::cout << "\nCASE " << name << std::endl;
        try
        {
            reset();
            test();
            ++passed;
            std::cout << "PASS" << std::endl;
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cout << "FAIL: " << error.what() << std::endl;
        }
    }

    static int count(const std::vector<ValidationMessage>& messages, ValidationSeverity level)
    {
        return static_cast<int>(std::count_if(messages.begin(), messages.end(),
            [level](const auto& message) { return message.severity == level; }));
    }

    static void print(const std::vector<ValidationMessage>& messages)
    {
        for (const auto& m : messages)
            std::cout << static_cast<int>(m.severity) << " ID=" << m.objectId
                << " endpoint=" << m.endpointIndex << " " << m.ruleName << " | " << m.text << '\n';
    }

    ObjectId addPipe(float length, int type, int size, glm::vec3 position = {})
    {
        auto& net = app.m_network;
        ObjectId id = net.addStraightPipe(length, net.catalog()[type].sizes[size].diameterMm);
        net.setPipeType(id, type);
        net.setPipeSize(id, size);
        auto object = app.makeSceneObjectForPipe(*net.find(id));
        object.transform.position = position;
        app.m_objects.push_back(std::move(object));
        return id;
    }

    ObjectId addReducer()
    {
        auto& net = app.m_network;
        auto id = net.addReducer(0.25f, 110, 75);
        net.setPipeType(id, 1);
        net.setPipeSize(id, 1);
        net.setReducerSecondarySize(id, 0);
        app.m_objects.push_back(app.makeSceneObjectForPipe(*net.find(id)));
        return id;
    }

    void snap()
    {
        const auto a = addPipe(1, 0, 4);
        const auto b = addPipe(1, 0, 4, {0, 0, 1.06f});
        app.m_selectedId = b;
        app.trySnapSelectedPipe(true);
        require(app.m_network.find(b)->connections[0].isOpen(), "60 mm gap was snapped");
        auto* moving = app.findSceneObject(b);
        moving->transform.rotationEuler.y = 0.18f;
        const auto outward = glm::vec3(moving->transform.matrix() * glm::vec4(0, 0, 1, 0));
        moving->transform.position = glm::vec3(0, 0, 0.53f) + outward * 0.5f;
        const auto before = glm::distance(app.connectionWorldPosition(a, 1), app.connectionWorldPosition(b, 0));
        app.trySnapSelectedPipe(true);
        const auto after = glm::distance(app.connectionWorldPosition(a, 1), app.connectionWorldPosition(b, 0));
        const auto direction = glm::normalize(app.connectionWorldPosition(b, 1) - app.connectionWorldPosition(b, 0));
        std::cout << "Gap outside tolerance=0.06 m; before=" << before << " m; after=" << after
                  << " m; direction dot=" << direction.z << '\n';
        require(after < 1e-5f && direction.z > 0.9999f, "position or direction is wrong");
        require(app.m_network.find(a)->connections[1].connectedElementId == b &&
                app.m_network.find(b)->connections[0].connectedElementId == a &&
                app.m_network.find(a)->connections[1].connectedPointIndex == 0 &&
                app.m_network.find(b)->connections[0].connectedPointIndex == 1, "not reciprocal");
    }

    void openEnd(bool intentional)
    {
        auto id = addPipe(1, 0, 4);
        app.m_network.setIntentionalOpenEnd(id, 1, true);
        app.m_network.setIntentionalOpenEnd(id, 0, intentional);
        auto messages = app.m_validator.validateOpenEnds(app.m_network);
        print(messages);
        const auto matches = std::count_if(messages.begin(), messages.end(), [id](auto& m)
            { return m.objectId == id && m.endpointIndex == 0 && m.severity == ValidationSeverity::Error; });
        require(matches == (intentional ? 0 : 1), "unexpected end result");
        if (intentional) require(count(messages, ValidationSeverity::Error) == 0, "unexpected error");
    }

    void diameter(bool reducer)
    {
        auto& net = app.m_network;
        auto a = addPipe(1, 1, 1, {0, 0, -0.625f});
        auto b = addPipe(1, 1, 0, {0, 0, reducer ? 0.625f : 0.375f});
        if (reducer)
        {
            auto r = addReducer();
            require(pipeEndpointDiameterMm(*net.find(r), 0) == 110 &&
                    pipeEndpointDiameterMm(*net.find(r), 1) == 75, "wrong reducer sizes");
            net.connect(a, 1, r, 0);
            net.connect(r, 1, b, 0);
        }
        else net.connect(a, 1, b, 0);
        auto messages = app.m_validator.validateConnectedComponents(net);
        print(messages);
        require((count(messages, ValidationSeverity::Error) > 0) == !reducer,
                "unexpected connectivity result");
        if (!reducer)
            require(std::any_of(messages.begin(), messages.end(), [](auto& m)
                { return m.severity == ValidationSeverity::Error &&
                  m.text.find("75") != std::string::npos && m.text.find("110") != std::string::npos; }),
                "missing concrete diameter diagnostics");
    }

    void route(bool checkCategory)
    {
        const int endType = checkCategory ? 0 : 1;
        const int endSize = checkCategory ? 6 : 0;
        auto a = addPipe(1, endType, endSize, {0, 1, -0.5f});
        auto b = addPipe(1, endType, endSize, {0, 1, 5.5f});
        auto wall = app.addArchitecturalObstacle(ArchitecturalObstacleType::Wall);
        require(app.setArchitecturalObstacleSize(wall, {0.9f, 1.6f, 0.25f}), "wall setup failed");
        app.findSceneObject(wall)->transform.position = {0, 1, 2.5f};
        app.m_routeGridSizeM = 0.35f;
        app.m_routeClearanceM = 0.10f;
        app.setRouteStart(a, 1);
        app.setRouteEnd(b, 0);
        app.m_routePipeTypeIndex = 1;
        if (!checkCategory)
            require(app.saveProject((output / "astar_bemenet.pcad").string()), "could not save route input");
        bool searched = app.searchAutomaticRoutes();
        std::cout << app.m_statusMessage << '\n';
        require(searched && !app.m_routeAlternatives.empty(), "no route");
        const auto route = app.m_routeAlternatives.at(app.m_selectedRouteAlternative);
        std::cout << "Alternatives=" << app.m_routeAlternatives.size() << " length=" << route.totalLengthM
                  << " m; 45deg=" << route.bend45Count << "; 90deg=" << route.bend90Count << '\n';
        bool built = app.buildSelectedAutomaticRoute();
        std::cout << app.m_statusMessage << '\n';
        require(built, "route build failed");
        auto& net = app.m_network;
        require(!net.find(a)->connections[1].isOpen() && !net.find(b)->connections[0].isOpen(),
                "route endpoint left open");
        const auto connected = app.connectedComponent(a);
        require(std::find(connected.begin(), connected.end(), b) != connected.end(), "route is disconnected");
        int inspectedVertices = 0;
        const auto expectedColor = app.pipeCategoryColor(1);
        for (auto id : app.m_lastRouteCreatedIds)
        {
            const auto* pipe = net.find(id);
            const auto* object = app.findSceneObject(id);
            require(pipe && object, "missing built object");
            require(pipe->type == PipeElementType::StraightPipe || pipe->type == PipeElementType::Elbow45 ||
                    pipe->type == PipeElementType::Elbow90, "nonstandard element");
            if (checkCategory)
            {
                require(pipe->material == "PVC-U" && pipe->pipeTypeIndex == 1 &&
                    pipe->diameterMm == 75 && pipe->wallThicknessMm == 1.9f, "wrong catalogue entry");
                require(glm::distance(object->color, expectedColor) < 1e-5f, "wrong category color");
            }
            for (const auto& v : object->mesh.vertices)
            {
                const auto p = glm::abs(glm::vec3(object->transform.matrix() * glm::vec4(v.position, 1)) -
                                        glm::vec3(0, 1, 2.5f));
                require(p.x > 0.45f || p.y > 0.80f || p.z > 0.125f, "mesh vertex inside wall");
                ++inspectedVertices;
            }
        }
        std::cout << "Built elements=" << app.m_lastRouteCreatedIds.size()
                  << "; inspected mesh vertices=" << inspectedVertices
                  << "; PVC-U color=" << expectedColor.r << "," << expectedColor.g << "," << expectedColor.b << '\n';
        require(inspectedVertices > 0, "missing mesh");
        if (!checkCategory)
            require(app.saveProject((output / "astar_elkeszult.pcad").string()), "could not save built route");
    }

    void routeEndReducer(int startSize, int endSize, int routeCategory = 0)
    {
        const auto start = addPipe(1, 0, startSize, {0, 1, -0.5f});
        const auto end = addPipe(1, routeCategory, endSize, {0, 1, 5.5f});
        app.setRouteStart(start, 1);
        app.setRouteEnd(end, 0);
        app.m_routePipeTypeIndex = routeCategory;
        app.m_routeGridSizeM = 0.35f;
        app.m_routeClearanceM = 0.10f;
        const bool expectReducer = routeCategory == 0 && startSize != endSize;
        const auto endWorld = app.connectionWorldPosition(end, 0);
        const bool searched = app.searchAutomaticRoutes();
        std::cout << app.m_statusMessage << '\n';
        require(searched && !app.m_routeAlternatives.empty(), "END reducer search failed");
        const auto& selected = app.m_routeAlternatives.at(app.m_selectedRouteAlternative);
        require((selected.catalogPlan.reducerType >= 0) == expectReducer, "incorrect reducer search plan");
        const bool previewReducer = std::any_of(app.m_routePreviewObjects.begin(), app.m_routePreviewObjects.end(),
            [](const auto& object) { return object.name.find("A* END") == 0; });
        require(previewReducer == expectReducer, "incorrect reducer preview");
        const bool built = app.buildSelectedAutomaticRoute();
        std::cout << app.m_statusMessage << '\n';
        require(built, "END reducer build failed");

        auto& network = app.m_network;
        const auto startNeighbor = network.find(start)->connections[1].connectedElementId;
        const auto endNeighbor = network.find(end)->connections[0].connectedElementId;
        require(network.find(startNeighbor) && network.find(startNeighbor)->type != PipeElementType::Reducer,
                "unexpected START reducer");
        require(network.find(endNeighbor), "END was not connected");
        const auto reducerCount = std::count_if(network.elements().begin(), network.elements().end(),
            [](const auto& item) { return item.second.type == PipeElementType::Reducer; });
        require(reducerCount == (expectReducer ? 1 : 0), "incorrect reducer count");
        if (!expectReducer) return;

        const auto& reducer = *network.find(endNeighbor);
        require(reducer.type == PipeElementType::Reducer, "reducer is not immediately before END");
        require(reducer.diameterMm == network.find(start)->diameterMm &&
                reducer.secondaryDiameterMm == network.find(end)->diameterMm, "wrong reducer orientation or diameters");
        require(reducer.connections[1].connectedElementId == end &&
                reducer.connections[1].connectedPointIndex == 0 &&
                network.find(end)->connections[0].connectedPointIndex == 1, "wrong END links");
        require(glm::distance(app.connectionWorldPosition(endNeighbor, 1), endWorld) < 0.003f,
                "reducer end geometry does not coincide with END");
        require(count(app.m_validator.validateConnectedComponents(network), ValidationSeverity::Error) == 0,
                "built transition failed connectivity validation");

        const auto path = (output / (startSize > endSize ? "astar_end_50_32.pcad" : "astar_end_32_50.pcad")).string();
        require(app.saveProject(path), "could not save END transition");
        require(app.loadProject(path), "could not reload END transition");
        require(network.find(endNeighbor)->secondaryDiameterMm == network.find(end)->diameterMm &&
                network.find(end)->connections[0].connectedElementId == endNeighbor,
                "saved transition lost END size or connection");
    }

    void routeWithBlockedEndReducer()
    {
        auto start = addPipe(1, 0, 4, {0, 1, -0.5f});
        auto end = addPipe(1, 0, 2, {0, 1, 5.5f});
        auto wall = app.addArchitecturalObstacle(ArchitecturalObstacleType::Wall);
        require(app.setArchitecturalObstacleSize(wall, {0.1f, 0.1f, 0.05f}), "wall setup failed");
        app.findSceneObject(wall)->transform.position = {0, 1, 4.9f};
        app.setRouteStart(start, 1);
        app.setRouteEnd(end, 0);
        require(!app.searchAutomaticRoutes() && app.m_routeAlternatives.empty(), "blocked END reducer was planned");
        std::cout << app.m_statusMessage << '\n';
        require(app.m_statusMessage.find("A* END") != std::string::npos, "missing END reducer diagnostic");
        require(app.m_network.elements().size() == 2 && app.m_network.find(end)->connections[0].isOpen(),
                "failed search modified the network");
    }

    void drainage(float gradient, float flow, const char* expected, bool shouldFail)
    {
        auto id = addPipe(4, 1, 0);
        app.findSceneObject(id)->transform.rotationEuler.x = std::atan(gradient);
        auto messages = app.m_validator.validateMszEn1610Drainage(app.m_network, app.m_objects,
            id, 0, id, 1, flow);
        print(messages);
        require((count(messages, ValidationSeverity::Error) > 0) == shouldFail, "wrong drainage severity");
        require(std::any_of(messages.begin(), messages.end(), [id,expected](const auto& m)
            { return m.objectId == id && m.text.find(expected) != std::string::npos; }),
            "missing expected drainage detail");
    }

    void darcy()
    {
        auto a = addPipe(4, 0, 4);
        auto b = app.m_network.addValve(0.15f, 50);
        app.m_network.setPipeSize(b, 4);
        auto valve = app.makeSceneObjectForPipe(*app.m_network.find(b));
        valve.transform.position.z = 2.075f;
        app.m_objects.push_back(std::move(valve));
        app.m_network.connect(a, 1, b, 0);
        auto calc = [&](float limit) {return app.m_validator.validateDarcyWeisbachPressureLoss(
            app.m_network, app.m_objects, a, 0, b, 1, 1.0f, 998.2f, 1.004e-6f, limit);};
        const double d = 0.0334;
        const double velocity = 0.001 / (std::numbers::pi * d * d / 4);
        const double reynolds = velocity * d / 1.004e-6;
        const double f = 0.25 / std::pow(std::log10(0.000007/(3.7*d) + 5.74/std::pow(reynolds,0.9)), 2);
        const double dynamicBar = 998.2 * velocity * velocity / 200000;
        const double major = f * 4 / d * dynamicBar;
        const double minor = 2.0 * dynamicBar;
        auto result = calc(0.10f);
        print(result);
        const auto& summary = result.back();
        const auto colon = summary.text.find(':');
        const auto actual = std::stod(summary.text.substr(colon + 1));
        std::cout << "Reference major=" << major << "; minor=" << minor << "; total=" << major+minor
                  << " bar; reported=" << actual << " bar\n";
        require(std::abs(actual - major - minor) <= 0.000051, "numeric reference mismatch");
        require(summary.severity == ValidationSeverity::Info, "0.10 bar limit should pass");
        auto exceeded = calc(0.02f);
        print(exceeded);
        require(exceeded.back().severity == ValidationSeverity::Warning, "0.02 bar limit should warn");
    }

    void persistence()
    {
        auto a = addPipe(1, 1, 1);
        auto b = addPipe(2, 1, 1, {0,0,1.5f});
        app.m_network.connect(a, 1, b, 0);
        app.m_network.setIntentionalOpenEnd(a, 0, true);
        auto wall = app.addArchitecturalObstacle(ArchitecturalObstacleType::Wall);
        app.setArchitecturalObstacleSize(wall, {0.6f,1.2f,0.2f});
        app.findSceneObject(wall)->transform.position = {2,0.6f,1};
        app.m_flowRateLps = 0.42f;
        app.m_hydraulicMaxTotalPressureLossBar = 0.37f;
        app.m_pressureHoldTimeMin = 42;
        app.setValidationStart(a, 0);
        app.setValidationEnd(b, 1);
        const auto path = (output / "mentes_v11.pcad").string();
        require(app.saveProject(path), "save returned false");
        std::ifstream saved(path);
        std::string header;
        std::getline(saved, header);
        require(header == "PLUMBCAD_PROJECT 11", "wrong file version");
        reset();
        app.m_flowRateLps = 0.12f;
        app.m_hydraulicMaxTotalPressureLossBar = 0.88f;
        app.m_pressureHoldTimeMin = 15;
        require(app.loadProject(path), "load returned false");
        const auto& n = app.m_network;
        require(n.elements().size() == 2 && n.find(a) && n.find(b), "wrong pipe count or IDs");
        require(n.find(a)->connections[1].connectedElementId == b &&
                n.find(b)->connections[0].connectedElementId == a &&
                n.find(a)->connections[0].intentionalOpenEnd, "links or flag lost");
        require(n.find(a)->material == "PVC-U" && n.find(a)->diameterMm == 110 &&
                n.find(b)->lengthM == 2 && app.findSceneObject(b)->transform.position.z == 1.5f,
                "catalogue, length or position lost");
        require(app.findArchitecturalObstacle(wall) && app.findArchitecturalObstacle(wall)->blocksRouting &&
                app.findSceneObject(wall)->transform.position.x == 2, "wall lost");
        require(app.m_validationStartId == a && app.m_validationStartEndpoint == 0 &&
                app.m_validationEndId == b && app.m_validationEndEndpoint == 1 &&
                app.m_flowRateLps == 0.42f && app.m_hydraulicMaxTotalPressureLossBar == 0.37f &&
                app.m_pressureHoldTimeMin == 42, "validation settings lost");
        std::cout << header << "; pipes=2; walls=1; Q=0.42 l/s; loss limit=0.37 bar; hold=42 min\n";
    }

    void bom()
    {
        addPipe(1.25f,0,4);
        addPipe(2.75f,0,4);
        addPipe(1.5f,1,0);
        auto elbow = app.m_network.addElbow90(0.2f, 50);
        app.m_network.setPipeSize(elbow,4);
        const auto summary = buildBillOfMaterials(app.m_network);
        require(summary.totalNetworkElementCount == 4 && summary.straightPipePieceCount == 3 &&
                summary.fittingPieceCount == 1 && summary.items.size() == 3 &&
                summary.totalStraightPipeLengthM == 5.5f, "wrong BOM totals");
        const auto path = (output / "darabjegyzek.csv").string();
        require(exportBillOfMaterialsCsv(summary, path), "CSV export failed");
        std::ifstream file(path, std::ios::binary);
        const std::string csv((std::istreambuf_iterator<char>(file)), {});
        require(csv.starts_with("\xEF\xBB\xBF") && csv.find(";2;4.000") != std::string::npos &&
                csv.find(";1;1.500") != std::string::npos &&
                csv.find(";1;\n") != std::string::npos && csv.find(";5.500") != std::string::npos,
                "unexpected CSV grouping or totals");
        std::cout << csv;
    }

    void pressure()
    {
        const auto a = addPipe(1,0,4);
        auto check = [&](float final) { return app.m_validator.validateMszEn806PressureTest(
            app.m_network, a, 6, 9, 30, 9, final, 0.2f); };
        auto pass = check(8.9f);
        auto fail = check(8.6f);
        print(pass); print(fail);
        require(count(pass,ValidationSeverity::Error)==0 && count(pass,ValidationSeverity::Warning)==0 &&
                count(fail,ValidationSeverity::Error)>0, "wrong pressure classification");
    }

    void velocity()
    {
        const auto id = addPipe(1,0,4);
        for (const auto [q, level] : {std::pair{1.0f, ValidationSeverity::Info},
                std::pair{2.0f, ValidationSeverity::Warning}, std::pair{3.0f, ValidationSeverity::Error}})
        {
            auto messages = app.m_validator.validateMszEn806FlowVelocity(app.m_network, app.m_objects,
                id,0,id,1,q);
            print(messages);
            require(std::any_of(messages.begin(), messages.end(), [=](const auto& m)
                {return m.objectId == id && m.severity == level && m.text.find("m/s") != std::string::npos;}),
                "wrong velocity classification");
            if (level != ValidationSeverity::Error)
                require(count(messages, ValidationSeverity::Error)==0, "unexpected velocity error");
        }
    }

    void validationCsv()
    {
        const std::vector<ValidationMessage> messages{
            {ValidationSeverity::Info, 1, 0, "Pressure test", "0.0308 bar"},
            {ValidationSeverity::Warning, 2, -1, "Warning; test", "Value \"2.5\""},
            {ValidationSeverity::Error, InvalidObjectId, -1, "Check", "First line\nSecond line"},
            {ValidationSeverity::Info, 3, -1, "Reference component", "Main branch", true}
        };
        const auto path = (output / "plumbcad_validation.csv").string();
        require(exportValidationCsv(messages, path), "validation CSV export failed");
        std::ifstream file(path, std::ios::binary);
        const std::string csv((std::istreambuf_iterator<char>(file)), {});
        require(csv.starts_with("\xEF\xBB\xBF"), "missing Excel UTF-8 marker");
        require(csv.find("1;0;Pressure test;MEGFELELT;0.0308 bar") != std::string::npos,
                "passed result or numeric detail missing");
        require(csv.find("2;;\"Warning; test\";") != std::string::npos &&
                csv.find("\"Value \"\"2.5\"\"\"") != std::string::npos, "invalid CSV quoting");
        require(csv.find(";;Check;NEM FELELT MEG;\"First line\nSecond line\"") != std::string::npos,
                "invalid multiline error detail");
        require(csv.find("3;;Reference component;REFERENCIA;Main branch") != std::string::npos,
                "reference row wrongly classified");
        require(csv.find(";;Check;NEM FELELT MEG;") < csv.find("2;;") &&
                csv.find("2;;") < csv.find("1;0;Pressure test;") &&
                csv.find("1;0;Pressure test;") < csv.find("3;;Reference component;"),
                "validation export rows are not in result order");
    }

    void execute()
    {
        run("01 snap", [&]{snap();});
        run("02 open end", [&]{openEnd(false);});
        run("03 intentional open end", [&]{openEnd(true);});
        run("04 diameter mismatch", [&]{diameter(false);});
        run("05 reducer", [&]{diameter(true);});
        run("06 A* obstacle", [&]{route(false);});
        run("07 A* PVC-U", [&]{route(true);});
        run("08 Table 6 shallow", [&]{drainage(1.0f/100, 0.5f, "1:40", true);});
        run("09 Table 6 capacity", [&]{drainage(1.0f/50, 3.0f, "2.80", true);});
        run("10 Darcy-Weisbach", [&]{darcy();});
        run("11 save/load v11", [&]{persistence();});
        run("12 BOM CSV", [&]{bom();});
        run("13 pressure", [&]{pressure();});
        run("14 velocity", [&]{velocity();});
        run("15 Table 6 valid", [&]{drainage(1.0f/30, 0.5f, "MEGFELELT", false);});
        run("16 Table 6 reverse gradient", [&]{drainage(-1.0f/30, 0.5f, "START", true);});
        run("17 A* END reducer 50 to 32", [&]{routeEndReducer(4, 2);});
        run("18 A* END reducer 32 to 50", [&]{routeEndReducer(2, 4);});
        run("19 A* START mismatch only", [&]{routeEndReducer(4, 6, 2);});
        run("20 A* END reducer blocked", [&]{routeWithBlockedEndReducer();});
        run("21 validation CSV export", [&]{validationCsv();});
    }
};
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Usage: PlumbCAD <output-directory>");
        glfwInit();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        plumbcad::FunctionalTests tests;
        tests.output = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(tests.output);
        std::cout << "GPU=" << glGetString(GL_RENDERER) << "; OpenGL=" << glGetString(GL_VERSION) << '\n';
        tests.execute();
        std::cout << "\nTOTAL passed=" << tests.passed << " failed=" << tests.failed << '\n';
        return tests.failed == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal: " << e.what() << '\n';
        return 2;
    }
}
