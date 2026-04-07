cmake_minimum_required(VERSION 3.25)

project(chiselcad
    VERSION 0.1.0
    DESCRIPTION "A fast, precise, GPU-accelerated CSG modeler"
    LANGUAGES CXX
)

# ============================================================
# C++20
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Build type default
# ============================================================
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# ============================================================
# Dependencies (via vcpkg)
# ============================================================
find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(manifold CONFIG REQUIRED)
find_package(embree 3 REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# VMA is header-only, often needs manual include path from vcpkg
find_path(VMA_INCLUDE_DIR vk_mem_alloc.h)

# ImGuiColorTextEdit — may need manual integration if not in vcpkg
# Adjust this path if using FetchContent or a submodule instead
find_path(IMGUICOLORTEXTEDIT_INCLUDE_DIR TextEditor.h)

# ============================================================
# Shader compilation (GLSL → SPIR-V)
# ============================================================
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_BINARY_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/preview.vert
    ${SHADER_SOURCE_DIR}/preview.frag
    ${SHADER_SOURCE_DIR}/mesh.vert
    ${SHADER_SOURCE_DIR}/mesh.frag
    ${SHADER_SOURCE_DIR}/grid.vert
    ${SHADER_SOURCE_DIR}/grid.frag
)

set(SHADER_BINARIES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV "${SHADER_BINARY_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_BINARIES ${SPIRV})
endforeach()

add_custom_target(chiselcad_shaders ALL DEPENDS ${SHADER_BINARIES})

# ============================================================
# Platform-specific sources
# ============================================================
if(WIN32)
    set(PLATFORM_SOURCES
        src/editor/FileWatcherWin32.cpp
    )
elseif(UNIX)
    set(PLATFORM_SOURCES
        src/editor/FileWatcherInotify.cpp
    )
endif()

# ============================================================
# Source files
# ============================================================
set(CHISELCAD_SOURCES
    src/main.cpp

    # App
    src/app/Application.cpp
    src/app/Config.cpp

    # Editor
    src/editor/EditorPanel.cpp
    src/editor/ExternalEditor.cpp
    src/editor/DiagnosticsPanel.cpp
    ${PLATFORM_SOURCES}

    # Language
    src/lang/Lexer.cpp
    src/lang/Parser.cpp

    # CSG
    src/csg/CsgEvaluator.cpp
    src/csg/PrimitiveGen.cpp
    src/csg/MeshEvaluator.cpp
    src/csg/MeshCache.cpp

    # Render
    src/render/VulkanContext.cpp
    src/render/VmaAllocator.cpp
    src/render/Swapchain.cpp
    src/render/RenderGraph.cpp
    src/render/GpuMesh.cpp
    src/render/Pipeline.cpp
    src/render/Camera.cpp
    src/render/Renderer.cpp

    # Export
    src/export/StlExporter.cpp
)

# ============================================================
# Target
# ============================================================
add_executable(chiselcad ${CHISELCAD_SOURCES})
add_dependencies(chiselcad chiselcad_shaders)

target_include_directories(chiselcad PRIVATE
    src/
    ${VMA_INCLUDE_DIR}
    ${IMGUICOLORTEXTEDIT_INCLUDE_DIR}
)

target_link_libraries(chiselcad PRIVATE
    Vulkan::Vulkan
    glfw
    imgui::imgui
    glm::glm
    manifold::manifold
    embree
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# ============================================================
# Compiler warnings
# ============================================================
if(MSVC)
    target_compile_options(chiselcad PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(chiselcad PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ============================================================
# Shader output directory available as a compile definition
# ============================================================
target_compile_definitions(chiselcad PRIVATE
    CHISELCAD_SHADER_DIR="${SHADER_BINARY_DIR}/"
    CHISELCAD_VERSION="${PROJECT_VERSION}"
)

# ============================================================
# Install
# ============================================================
install(TARGETS chiselcad RUNTIME DESTINATION bin)
install(DIRECTORY ${SHADER_BINARY_DIR} DESTINATION share/chiselcad)
