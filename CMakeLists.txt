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
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# ============================================================
# Shader compilation (GLSL → SPIR-V)
# ============================================================
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin REQUIRED)

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
    ${SHADER_SOURCE_DIR}/background.vert
    ${SHADER_SOURCE_DIR}/background.frag
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
    src/app/MeshBuilder.cpp

    # Editor (external editor + file watcher only; no embedded editor in v1)
    src/editor/ExternalEditor.cpp
    src/editor/DiagnosticsPanel.cpp
    ${PLATFORM_SOURCES}

    # Language
    src/lang/Lexer.cpp
    src/lang/Parser.cpp
    src/lang/Interpreter.cpp
    src/lang/SourceLoader.cpp

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

    # Export / Import
    src/export/StlExporter.cpp
    src/import/StlLoader.cpp
    src/import/StlImporter.cpp
    src/import/SurfaceLoader.cpp
    src/import/stb_truetype_impl.cpp
    src/import/StbFontBackend.cpp
    src/import/NaiveLtrShaper.cpp
    src/import/TextLoader.cpp
)

# ============================================================
# Main target
# ============================================================
add_executable(chiselcad ${CHISELCAD_SOURCES})
add_dependencies(chiselcad chiselcad_shaders)

target_include_directories(chiselcad PRIVATE
    src/
    third_party/
)

target_link_libraries(chiselcad PRIVATE
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    glfw
    imgui::imgui
    glm::glm
    manifold::manifold
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
    CHISELCAD_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
    CHISELCAD_VERSION="${PROJECT_VERSION}"
    GLM_FORCE_DEPTH_ZERO_TO_ONE   # Vulkan depth range [0,1] not [-1,1]
)

# stb_truetype's implementation TU triggers warnings under -Wall -Wextra
# -Wpedantic (vendored code, not ours to fix) — silenced for this one file
# only rather than loosened project-wide.
if(MSVC)
    set_source_files_properties(src/import/stb_truetype_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(src/import/stb_truetype_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ============================================================
# Tests (Catch2 — via vcpkg, same binary cache as every other dependency,
# rather than FetchContent building it from source on every CI run)
# ============================================================
enable_testing()
find_package(Catch2 3 CONFIG REQUIRED)

set(CHISELCAD_TEST_SOURCES
    tests/test_main.cpp
    tests/test_lexer.cpp
    tests/test_parser.cpp
    tests/test_csg_evaluator.cpp
    tests/test_interpreter.cpp
    tests/test_source_loader.cpp
    tests/test_stl_loader.cpp
    tests/test_surface_loader.cpp
    tests/test_text_loader.cpp

    # Sources under test (no GPU/Manifold deps)
    src/lang/Lexer.cpp
    src/lang/Parser.cpp
    src/lang/Interpreter.cpp
    src/lang/SourceLoader.cpp
    src/csg/CsgEvaluator.cpp
    src/import/StlLoader.cpp
    src/import/SurfaceLoader.cpp
    src/import/stb_truetype_impl.cpp
    src/import/StbFontBackend.cpp
    src/import/NaiveLtrShaper.cpp
    src/import/TextLoader.cpp
)

add_executable(chiselcad_tests ${CHISELCAD_TEST_SOURCES})

target_include_directories(chiselcad_tests PRIVATE src/ third_party/)

target_compile_definitions(chiselcad_tests PRIVATE
    CHISELCAD_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/"
    CHISELCAD_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)

target_link_libraries(chiselcad_tests PRIVATE
    Catch2::Catch2WithMain
    glm::glm
    spdlog::spdlog
)

if(MSVC)
    target_compile_options(chiselcad_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(chiselcad_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(Catch)
catch_discover_tests(chiselcad_tests)

# ============================================================
# Install
# ============================================================
install(TARGETS chiselcad RUNTIME DESTINATION bin)
install(DIRECTORY ${SHADER_BINARY_DIR} DESTINATION share/chiselcad)
install(DIRECTORY resources/ DESTINATION share/chiselcad/resources)
