include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "Use the shared MSVC runtime for GoogleTest")
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

add_executable(PlumbCADUnitTests
    UnitTests.cpp
    ../src/Pipeline/PipeNetwork.cpp
    ../src/Validation/Validation.cpp
    ../src/Validation/ValidationReport.cpp
    ../src/Routing/AStarRouter.cpp
    ../src/Routing/RouteGeometry.cpp
    ../src/Bom/BillOfMaterials.cpp
    ../src/Graphics/Camera.cpp
)
target_include_directories(PlumbCADUnitTests PRIVATE ../src)
target_compile_definitions(PlumbCADUnitTests PRIVATE GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(PlumbCADUnitTests PRIVATE glm::glm GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(PlumbCADUnitTests PROPERTIES LABELS unit)

if(PLUMBCAD_BUILD_INTEGRATION_TESTS)
    set(application_sources ${PLUMBCAD_SOURCES})
    list(REMOVE_ITEM application_sources "${PROJECT_SOURCE_DIR}/src/main.cpp")
    add_executable(PlumbCADIntegrationTests FunctionalTests.cpp ${application_sources})
    target_include_directories(PlumbCADIntegrationTests PRIVATE ../src)
    target_compile_definitions(PlumbCADIntegrationTests PRIVATE
        GLFW_INCLUDE_NONE
        GLM_ENABLE_EXPERIMENTAL
        PLUMBCAD_INTEGRATION_TESTS
        ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    )
    target_link_libraries(PlumbCADIntegrationTests PRIVATE glfw glm::glm glad_gl_core_33 imgui_backend)
    add_test(NAME PlumbCADIntegration
        COMMAND PlumbCADIntegrationTests "${CMAKE_CURRENT_BINARY_DIR}/results"
    )
    set_tests_properties(PlumbCADIntegration PROPERTIES LABELS integration TIMEOUT 60)
endif()
