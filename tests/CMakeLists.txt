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
    if(MSVC)
        target_compile_options(PlumbCADIntegrationTests PRIVATE /utf-8)
    endif()
    add_test(NAME PlumbCADIntegration
        COMMAND PlumbCADIntegrationTests "${CMAKE_CURRENT_BINARY_DIR}/results"
    )
    set_tests_properties(PlumbCADIntegration PROPERTIES LABELS integration TIMEOUT 60)
endif()

if(WIN32 AND TARGET PlumbCADIntegrationTests)
    find_program(PLUMBCAD_CSHARP_COMPILER NAMES csc.exe
        HINTS "$ENV{WINDIR}/Microsoft.NET/Framework64/v4.0.30319"
              "$ENV{WINDIR}/Microsoft.NET/Framework/v4.0.30319"
    )
    if(PLUMBCAD_CSHARP_COMPILER)
        set(test_launcher "${CMAKE_CURRENT_BINARY_DIR}/FunkcionalisTesztek.exe")
        set(test_launcher_source "${CMAKE_CURRENT_SOURCE_DIR}/FunkcionalisTesztek.cs")
        cmake_path(NATIVE_PATH test_launcher test_launcher_native)
        cmake_path(NATIVE_PATH test_launcher_source test_launcher_source_native)
        add_custom_command(
            OUTPUT "${test_launcher}"
            COMMAND "${PLUMBCAD_CSHARP_COMPILER}" /nologo /target:exe /codepage:65001
                    "/out:${test_launcher_native}" "${test_launcher_source_native}"
            DEPENDS "${test_launcher_source}"
            VERBATIM
        )
        add_custom_target(PlumbCADTestTools
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:PlumbCAD>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${test_launcher}"
                    "$<TARGET_FILE_DIR:PlumbCAD>/FunkcionalisTesztek.exe"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PlumbCADIntegrationTests>"
                    "$<TARGET_FILE_DIR:PlumbCAD>/PlumbCADIntegrationTests.exe"
            DEPENDS "${test_launcher}" PlumbCADIntegrationTests
            VERBATIM
        )
        add_dependencies(PlumbCAD PlumbCADTestTools)
        target_compile_definitions(PlumbCAD PRIVATE PLUMBCAD_HAS_TEST_RUNNER)
        target_link_libraries(PlumbCAD PRIVATE shell32)
    else()
        message(WARNING "C# compiler not found. The PlumbCAD test panel can load samples, but cannot launch the Windows test runner.")
    endif()
endif()
