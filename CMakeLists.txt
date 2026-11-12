cmake_minimum_required(VERSION 3.20)
project(PlumbCAD LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLUMBCAD_FETCH_DEPS "Download GLFW, GLM, GLAD and Dear ImGui with FetchContent" ON)

include(FetchContent)

if(PLUMBCAD_FETCH_DEPS)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
    )
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.9b
    )
    FetchContent_Declare(glad
        GIT_REPOSITORY https://github.com/Dav1dde/glad.git
        GIT_TAG v2.0.8
        SOURCE_SUBDIR cmake
    )

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw glm imgui glad)
    glad_add_library(glad_gl_core_33 STATIC REPRODUCIBLE API gl:core=3.3)
endif()

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_backend PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_backend PUBLIC glfw)
target_compile_definitions(imgui_backend PUBLIC GLFW_INCLUDE_NONE GLM_ENABLE_EXPERIMENTAL)

file(GLOB_RECURSE PLUMBCAD_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.hpp
)

add_executable(PlumbCAD ${PLUMBCAD_SOURCES})

target_include_directories(PlumbCAD PRIVATE src)
target_compile_definitions(PlumbCAD PRIVATE GLFW_INCLUDE_NONE GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(PlumbCAD PRIVATE glfw glm::glm glad_gl_core_33 imgui_backend)

target_compile_definitions(PlumbCAD PRIVATE ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

if(MSVC)
    target_compile_options(PlumbCAD PRIVATE /W4 /permissive-)
else()
    target_compile_options(PlumbCAD PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(BUILD_TESTING "Build PlumbCAD tests" OFF)
option(PLUMBCAD_BUILD_INTEGRATION_TESTS "Build the application tests that require OpenGL" OFF)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()