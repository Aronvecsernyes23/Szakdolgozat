#include "Bom/BillOfMaterials.hpp"
#include "Graphics/Camera.hpp"
#include "Routing/AStarRouter.hpp"
#include "Routing/RouteGeometry.hpp"
#include "Validation/Validation.hpp"
#include "Validation/ValidationReport.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

namespace plumbcad
{
namespace
{
    bool hasSeverity(const std::vector<ValidationMessage>& messages, ValidationSeverity severity)
    {
        return std::any_of(messages.begin(), messages.end(), [severity](const auto& message)
        {
            return message.severity == severity;
        });
    }

    bool hasDetail(const std::vector<ValidationMessage>& messages, ValidationSeverity severity,
                   ObjectId id, int endpoint, const std::string& detail)
    {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& message)
        {
            return message.severity == severity && message.objectId == id &&
                   message.endpointIndex == endpoint && message.text.find(detail) != std::string::npos;
        });
    }

    ObjectId addPipe(PipeNetwork& network, int category, int size, float length = 1)
    {
        auto id = network.addStraightPipe(length, network.catalog()[category].sizes[size].diameterMm);
        network.setPipeType(id, category);
        network.setPipeSize(id, size);
        return id;
    }

    TEST(PipeNetwork, ConnectAndDisconnectUpdateBothEndpoints)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 0, 4);
        const auto b = addPipe(network, 0, 4);
        ASSERT_TRUE(network.setIntentionalOpenEnd(a, 1, true));
        network.connect(a, 1, b, 0);
        const auto& endA = network.find(a)->connections[1];
        const auto& endB = network.find(b)->connections[0];
        EXPECT_EQ(endA.connectedElementId, b);
        EXPECT_EQ(endA.connectedPointIndex, 0);
        EXPECT_EQ(endB.connectedElementId, a);
        EXPECT_EQ(endB.connectedPointIndex, 1);
        EXPECT_FALSE(endA.intentionalOpenEnd);
        network.disconnect(a, 1);
        EXPECT_TRUE(endA.isOpen());
        EXPECT_TRUE(endB.isOpen());
    }

    TEST(PipeNetwork, CatalogSetsDimensionsAndMaterial)
    {
        PipeNetwork network;
        const auto id = addPipe(network, 1, 1, 2);
        const auto& pipe = *network.find(id);
        EXPECT_EQ(pipe.material, "PVC-U");
        EXPECT_FLOAT_EQ(pipe.diameterMm, 110);
        EXPECT_FLOAT_EQ(pipe.wallThicknessMm, 3.2f);
        EXPECT_FLOAT_EQ(pipe.connections[0].localPosition.z, -1);
        EXPECT_FLOAT_EQ(pipe.connections[1].localPosition.z, 1);
        EXPECT_TRUE(network.setPipeSize(id, 999));
        EXPECT_FLOAT_EQ(pipe.diameterMm, 160);
    }

    TEST(PipeNetwork, ReducerExposesSeparateEndpointSizes)
    {
        PipeNetwork network;
        const auto id = network.addReducer(0.25f, 110, 75);
        ASSERT_TRUE(network.setPipeType(id, 1));
        ASSERT_TRUE(network.setPipeSize(id, 1));
        ASSERT_TRUE(network.setReducerSecondarySize(id, 0));
        const auto& reducer = *network.find(id);
        EXPECT_FLOAT_EQ(pipeEndpointDiameterMm(reducer, 0), 110);
        EXPECT_FLOAT_EQ(pipeEndpointDiameterMm(reducer, 1), 75);
        EXPECT_FLOAT_EQ(pipeEndpointWallThicknessMm(reducer, 0), 3.2f);
        EXPECT_FLOAT_EQ(pipeEndpointWallThicknessMm(reducer, 1), 1.9f);
    }

    TEST(NetworkValidation, DistinguishesAccidentalAndIntentionalOpenEnds)
    {
        PipeNetwork network;
        const auto id = addPipe(network, 0, 4);
        ASSERT_TRUE(network.setIntentionalOpenEnd(id, 1, true));
        NetworkValidator validator;
        auto messages = validator.validateOpenEnds(network);
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, id, 0, "NEM FELELT MEG"));
        EXPECT_FALSE(hasDetail(messages, ValidationSeverity::Error, id, 1, ""));
        ASSERT_TRUE(network.setIntentionalOpenEnd(id, 0, true));
        EXPECT_FALSE(hasSeverity(validator.validateOpenEnds(network), ValidationSeverity::Error));
    }

    TEST(NetworkValidation, ReportsConnectedEndsAlongsideOpenEndFailures)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 0, 4);
        const auto b = addPipe(network, 0, 4);
        network.connect(a, 1, b, 0);
        const auto messages = NetworkValidator{}.validateOpenEnds(network);
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Info, a, 1, ""));
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Info, b, 0, ""));
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, a, 0, ""));
    }

    TEST(ValidationReport, DoesNotLabelReferenceComponentAsPassed)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 1, 1);
        const auto b = addPipe(network, 1, 0);
        network.connect(a, 1, b, 0);
        const auto messages = NetworkValidator{}.validateConnectedComponents(network);
        const auto reference = std::find_if(messages.begin(), messages.end(),
            [](const auto& message) { return message.isReference; });
        ASSERT_NE(reference, messages.end());
        EXPECT_STREQ(validationResultName(*reference), "REFERENCIA");
    }

    TEST(NetworkValidation, ReportsIsolatedElement)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 0, 4);
        const auto b = addPipe(network, 0, 4);
        const auto isolated = addPipe(network, 0, 4);
        network.connect(a, 1, b, 0);
        const auto messages = NetworkValidator{}.validateIsolatedElements(network);
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, isolated, -1, ""));
        EXPECT_FALSE(hasDetail(messages, ValidationSeverity::Error, a, -1, ""));
    }

    TEST(NetworkValidation, RejectsDirectDiameterMismatch)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 1, 1);
        const auto b = addPipe(network, 1, 0);
        network.connect(a, 1, b, 0);
        auto messages = NetworkValidator{}.validateConnectedComponents(network);
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, a, 1, "110.0"));
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, a, 1, "75.0"));
    }

    TEST(NetworkValidation, AcceptsMatchingReducer)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 1, 1);
        const auto b = addPipe(network, 1, 0);
        const auto reducer = network.addReducer(0.25f, 110, 75);
        ASSERT_TRUE(network.setPipeType(reducer, 1));
        ASSERT_TRUE(network.setPipeSize(reducer, 1));
        ASSERT_TRUE(network.setReducerSecondarySize(reducer, 0));
        network.connect(a, 1, reducer, 0);
        network.connect(reducer, 1, b, 0);
        EXPECT_FALSE(hasSeverity(NetworkValidator{}.validateConnectedComponents(network), ValidationSeverity::Error));
    }

    TEST(NetworkValidation, RejectsDisconnectedGroups)
    {
        PipeNetwork network;
        const auto a = addPipe(network, 0, 4);
        const auto b = addPipe(network, 0, 4);
        const auto c = addPipe(network, 0, 4);
        const auto d = addPipe(network, 0, 4);
        network.connect(a, 1, b, 0);
        network.connect(c, 1, d, 0);
        EXPECT_TRUE(hasSeverity(NetworkValidator{}.validateConnectedComponents(network), ValidationSeverity::Error));
    }

    TEST(NetworkValidation, RejectsInvalidWallThickness)
    {
        PipeNetwork network;
        const auto id = addPipe(network, 0, 4);
        network.find(id)->wallThicknessMm = 25;
        EXPECT_TRUE(hasSeverity(NetworkValidator{}.validateMszEn806BasicData(network), ValidationSeverity::Error));
    }

    TEST(PressureTest, AppliesMeasuredPressureDropLimit)
    {
        PipeNetwork network;
        const auto id = addPipe(network, 0, 4);
        NetworkValidator validator;
        const auto acceptable = validator.validateMszEn806PressureTest(network, id, 6, 9, 30, 9, 8.9f, 0.2f);
        const auto excessive = validator.validateMszEn806PressureTest(network, id, 6, 9, 30, 9, 8.6f, 0.2f);
        ASSERT_FALSE(acceptable.empty());
        EXPECT_FALSE(hasSeverity(acceptable, ValidationSeverity::Error));
        EXPECT_FALSE(acceptable.back().isRedundantSummary);
        EXPECT_TRUE(hasDetail(acceptable, ValidationSeverity::Info, id, -1, "0.100 bar"));
        EXPECT_TRUE(hasDetail(excessive, ValidationSeverity::Error, id, -1, "0.400 bar"));
    }

    TEST(PressureTest, RejectsElementBelowProjectDesignPressure)
    {
        PipeNetwork network;
        const auto id = addPipe(network, 0, 4);
        network.find(id)->designPressureBar = 4;
        const auto messages = NetworkValidator{}.validateMszEn806PressureTest(network, id, 6, 9, 30, 9, 9, 0.2f);
        EXPECT_TRUE(hasSeverity(messages, ValidationSeverity::Error));
    }

    struct SinglePipe
    {
        PipeNetwork network;
        std::vector<SceneObject> objects{1};
        ObjectId id;

        SinglePipe(int category, int size, float length, float gradient = 0)
            : id(addPipe(network, category, size, length))
        {
            objects.front().id = id;
            objects.front().transform.rotationEuler.x = std::atan(gradient);
        }
    };

    TEST(WaterVelocity, ClassifiesRecommendedWarningAndExcessive)
    {
        SinglePipe pipe(0, 4, 1);
        NetworkValidator validator;
        struct Case { float flow; ValidationSeverity severity; const char* velocity; };
        for (const auto& test : {Case{1, ValidationSeverity::Info, "1.141 m/s"},
                                 Case{2, ValidationSeverity::Warning, "2.283 m/s"},
                                 Case{3, ValidationSeverity::Error, "3.424 m/s"}})
        {
            SCOPED_TRACE(test.flow);
            auto messages = validator.validateMszEn806FlowVelocity(pipe.network, pipe.objects,
                                                                   pipe.id, 0, pipe.id, 1, test.flow);
            EXPECT_TRUE(hasDetail(messages, test.severity, pipe.id, 0, test.velocity));
        }
    }

    TEST(WaterVelocity, RejectsZeroFlow)
    {
        SinglePipe pipe(0, 4, 1);
        auto messages = NetworkValidator{}.validateMszEn806FlowVelocity(pipe.network, pipe.objects,
                                                                       pipe.id, 0, pipe.id, 1, 0);
        EXPECT_TRUE(hasSeverity(messages, ValidationSeverity::Error));
    }

    TEST(DrainageTable6, ChecksGradientCapacityAndDirection)
    {
        struct Case { float gradient; float flow; ValidationSeverity severity; const char* detail; };
        for (const auto& test : {Case{1.0f/100, 0.5f, ValidationSeverity::Error, "1:40"},
                                 Case{1.0f/50, 3.0f, ValidationSeverity::Error, "2.80 l/s"},
                                 Case{1.0f/30, 0.5f, ValidationSeverity::Info, "MEGFELELT"},
                                 Case{-1.0f/30, 0.5f, ValidationSeverity::Error, "START -> END"}})
        {
            SCOPED_TRACE(test.gradient);
            SinglePipe pipe(1, 0, 4, test.gradient);
            const auto messages = NetworkValidator{}.validateMszEn1610Drainage(pipe.network, pipe.objects,
                                                                              pipe.id, 0, pipe.id, 1, test.flow);
            EXPECT_TRUE(hasDetail(messages, test.severity, pipe.id, 0, test.detail));
            EXPECT_EQ(hasSeverity(messages, ValidationSeverity::Error), test.severity == ValidationSeverity::Error);
        }
    }

    TEST(DrainageTable6, ExplainsHorizontalPipeAndBothFailureReasons)
    {
        SinglePipe pipe(1, 0, 4);
        const auto messages = NetworkValidator{}.validateMszEn1610Drainage(
            pipe.network, pipe.objects, pipe.id, 0, pipe.id, 1, 3.0f);
        ASSERT_EQ(messages.size(), 2u);
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, pipe.id, 0, "0%"));
        EXPECT_TRUE(hasDetail(messages, ValidationSeverity::Error, pipe.id, 0, "1:80"));
        EXPECT_EQ(messages.front().text.find("1:inf"), std::string::npos);
        EXPECT_FALSE(messages.front().isRedundantSummary);
        EXPECT_TRUE(messages.back().isRedundantSummary);
        EXPECT_EQ(messages.back().severity, ValidationSeverity::Error);
        EXPECT_NE(messages.back().text.find("Hibaokok"), std::string::npos);
        EXPECT_NE(messages.back().text.find("Hibás: 1"), std::string::npos);
        EXPECT_NE(messages.back().text.find("Túl kicsi lejtés: 1"), std::string::npos);
        EXPECT_NE(messages.back().text.find("Elégtelen kapacitás: 1"), std::string::npos);
    }

    TEST(DarcyWeisbach, MatchesAnalyticLaminarLoss)
    {
        SinglePipe pipe(0, 4, 8);
        const float flow = 0.02f;
        const float viscosity = 1.0e-6f;
        const float density = 1000;
        const double diameter = 0.0334;
        const double velocity = (flow / 1000.0) / (std::numbers::pi * diameter * diameter / 4);
        ASSERT_LT(velocity * diameter / viscosity, 2300);
        const double expectedBar = 32.0 * density * viscosity * 8.0 * velocity /
                                   (diameter * diameter * 100000);
        const auto messages = NetworkValidator{}.validateDarcyWeisbachPressureLoss(pipe.network, pipe.objects,
            pipe.id, 0, pipe.id, 1, flow, density, viscosity, 1);
        ASSERT_FALSE(messages.empty());
        EXPECT_EQ(messages.back().severity, ValidationSeverity::Info);
        const std::string& text = messages.back().text;
        const auto colon = text.find(':');
        ASSERT_NE(colon, std::string::npos);
        std::istringstream value(text.substr(colon + 1));
        double actualBar = 0;
        ASSERT_TRUE(value >> actualBar);
        EXPECT_NEAR(actualBar, expectedBar, 0.000051);
    }

    TEST(DarcyWeisbach, WarnsWhenLossExceedsLimit)
    {
        SinglePipe pipe(0, 4, 4);
        const auto messages = NetworkValidator{}.validateDarcyWeisbachPressureLoss(pipe.network, pipe.objects,
            pipe.id, 0, pipe.id, 1, 1, 998.2f, 1.004e-6f, 0.01f);
        ASSERT_FALSE(messages.empty());
        EXPECT_EQ(messages.back().severity, ValidationSeverity::Warning);
        EXPECT_NE(messages.back().text.find("0.0178 bar"), std::string::npos);
    }

    TEST(RoutingGeometry, Classifies45And90DegreeBends)
    {
        using routing::RouteBendKind;
        EXPECT_EQ(routing::routeBendKind({1, 0, 0}, {1, 0, 0}), RouteBendKind::None);
        EXPECT_EQ(routing::routeBendKind({1, 0, 0}, glm::normalize(glm::vec3(1, 0, 1))), RouteBendKind::Deg45);
        EXPECT_EQ(routing::routeBendKind({1, 0, 0}, {0, 0, 1}), RouteBendKind::Deg90);
        EXPECT_EQ(routing::routeBendKind({1, 0, 0}, {-1, 0, 0}), RouteBendKind::Invalid);
    }

    TEST(RoutingGeometry, DetectsCrossingAndClearSegments)
    {
        const RouteObstacle wall{{-0.45f, -0.8f, 2.375f}, {0.45f, 0.8f, 2.625f}};
        EXPECT_TRUE(routing::segmentIntersectsRouteObstacle({0, 0, 0}, {0, 0, 5}, wall));
        EXPECT_FALSE(routing::segmentIntersectsRouteObstacle({1, 0, 0}, {1, 0, 5}, wall));
    }

    TEST(RoutingGeometry, ConvertsRightAngleToTwoStraightsAndAnElbow)
    {
        std::vector<RouteBuildPrimitive> plan;
        ASSERT_TRUE(routing::buildRouteBuildPlan({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, 0.1f, plan));
        ASSERT_EQ(plan.size(), 3u);
        EXPECT_EQ(plan[0].kind, RouteBuildPrimitiveKind::Straight);
        EXPECT_EQ(plan[1].kind, RouteBuildPrimitiveKind::Elbow90);
        EXPECT_EQ(plan[2].kind, RouteBuildPrimitiveKind::Straight);
        EXPECT_FLOAT_EQ(plan[1].bendRadiusM, 0.05f);
        EXPECT_NEAR(routing::routeBuildPlanPhysicalLength(plan, 0.1f), 1.9 + std::numbers::pi * 0.025, 1e-4);
    }

    TEST(AStarRouter, FindsDetourWithoutIntersectingWall)
    {
        RouteRequest request;
        request.startWorld = {0, 0, 0};
        request.endWorld = {0, 0, 5};
        request.maxGridStepM = 0.25f;
        const int forward = routingDirectionIndex({0, 0, 1});
        request.startDirectionIndex = forward;
        request.endDirectionIndex = forward;
        request.obstacles.push_back({{-0.45f, -0.8f, 2.375f}, {0.45f, 0.8f, 2.625f}});
        const auto result = AStarRouter{}.findRoute(request);
        ASSERT_TRUE(result.success) << result.message;
        ASSERT_GT(result.worldPoints.size(), 2u);
        EXPECT_NEAR(glm::distance(result.worldPoints.front(), request.startWorld), 0, 1e-4);
        EXPECT_NEAR(glm::distance(result.worldPoints.back(), request.endWorld), 0, 1e-4);
        for (std::size_t i = 1; i < result.worldPoints.size(); ++i)
        {
            EXPECT_FALSE(routing::segmentIntersectsRouteObstacle(result.worldPoints[i-1],
                result.worldPoints[i], request.obstacles.front()));
        }
    }

    TEST(AStarRouter, RejectsDestinationInsideObstacle)
    {
        RouteRequest request;
        request.endWorld = {0, 0, 5};
        request.obstacles.push_back({{-1, -1, 4}, {1, 1, 6}});
        const auto result = AStarRouter{}.findRoute(request);
        EXPECT_FALSE(result.success);
        EXPECT_TRUE(result.worldPoints.empty());
    }

    TEST(BillOfMaterials, GroupsByTypeCategoryAndSize)
    {
        PipeNetwork network;
        addPipe(network, 0, 4, 1.25f);
        addPipe(network, 0, 4, 2.75f);
        addPipe(network, 1, 0, 1.5f);
        const auto elbow = network.addElbow90(0.2f, 50);
        ASSERT_TRUE(network.setPipeSize(elbow, 4));
        const auto summary = buildBillOfMaterials(network);
        ASSERT_EQ(summary.items.size(), 3u);
        EXPECT_EQ(summary.totalNetworkElementCount, 4);
        EXPECT_EQ(summary.straightPipePieceCount, 3);
        EXPECT_EQ(summary.fittingPieceCount, 1);
        EXPECT_FLOAT_EQ(summary.totalStraightPipeLengthM, 5.5f);
        const auto ppr = std::find_if(summary.items.begin(), summary.items.end(), [](const auto& item)
        {
            return item.elementType == PipeElementType::StraightPipe && item.material == "PPR";
        });
        ASSERT_NE(ppr, summary.items.end());
        EXPECT_EQ(ppr->quantity, 2);
        EXPECT_FLOAT_EQ(ppr->totalLengthM, 4);
    }

    TEST(BillOfMaterials, EmptyNetworkHasZeroTotals)
    {
        const auto summary = buildBillOfMaterials(PipeNetwork{});
        EXPECT_TRUE(summary.items.empty());
        EXPECT_EQ(summary.totalNetworkElementCount, 0);
        EXPECT_FLOAT_EQ(summary.totalStraightPipeLengthM, 0);
    }

    TEST(Camera, OrthographicProjectionPreservesScaleWithDepth)
    {
        Camera camera;
        auto projectedWidth = [&](float depth)
        {
            const auto projection = camera.projection(1.0f);
            const auto point = projection * glm::vec4(1, 0, -depth, 1);
            return point.x / point.w;
        };
        EXPECT_GT(projectedWidth(2), projectedWidth(4));
        camera.orthographic = true;
        EXPECT_FLOAT_EQ(projectedWidth(2), projectedWidth(4));
    }
}
}
