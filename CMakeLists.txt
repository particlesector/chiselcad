cmake_minimum_required(VERSION 3.25)

# Must run before project(): the vcpkg toolchain file installs manifest
# features as part of the project() call, so CHISELCAD_BUILD_GUI (an
# ordinary option() below, too late for vcpkg to see) needs this mirror to
# actually skip fetching/building vulkan-memory-allocator/glfw3/imgui for a
# core-only configure. One flag controls both:
#   cmake -B build -DCHISELCAD_BUILD_GUI=OFF ...
if(NOT DEFINED CHISELCAD_BUILD_GUI OR CHISELCAD_BUILD_GUI)
    list(APPEND VCPKG_MANIFEST_FEATURES "gui")
endif()

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
# Build options
# ============================================================
# The Vulkan GUI app (chiselcad) needs the Vulkan SDK, GLFW, ImGui, VMA, and
# glslc. chiselcad_core/chiselcad_cli/chiselcad_tests need none of that —
# just Manifold and a few small libs — so a CI job that only builds those
# (e.g. the OpenSCAD compat/regression suite) can configure with this OFF
# and skip the entire Vulkan toolchain, including at `find_package` time.
option(CHISELCAD_BUILD_GUI "Build the Vulkan GUI application (chiselcad)" ON)

# ============================================================
# Dependencies shared by every target (via vcpkg)
# ============================================================
find_package(glm CONFIG REQUIRED)
find_package(manifold CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ZLIB REQUIRED) # raw DEFLATE decompression for import()'s .3mf (ZIP) support

# ============================================================
# Platform-specific sources
# ============================================================
if(WIN32)
    set(PLATFORM_SOURCES
        src/editor/FileWatcherWin32.cpp
    )
    set(PLATFORM_UTIL_SOURCES
        src/util/ResourcePathsWin32.cpp
    )
elseif(UNIX)
    set(PLATFORM_SOURCES
        src/editor/FileWatcherInotify.cpp
    )
    set(PLATFORM_UTIL_SOURCES
        src/util/ResourcePathsPosix.cpp
    )
endif()

# ============================================================
# chiselcad_core — language, CSG/mesh evaluation, import/export, headless
# build pipeline. No Vulkan/GLFW/ImGui/VMA dependency: this is the one
# target the compat/regression suite and the CLI actually need.
# ============================================================
set(CHISELCAD_CORE_SOURCES
    # Language
    src/lang/Lexer.cpp
    src/lang/Parser.cpp
    src/lang/Interpreter.cpp
    src/lang/SourceLoader.cpp

    # CSG / meshing
    src/csg/CsgEvaluator.cpp
    src/csg/PrimitiveGen.cpp
    src/csg/MeshEvaluator.cpp
    src/csg/MeshCache.cpp

    # Headless build pipeline (parse -> CSG evaluate -> mesh) + JSON stats
    src/app/HeadlessBuild.cpp
    src/app/BuildStats.cpp

    # Export / Import
    src/export/StlExporter.cpp
    src/import/StlLoader.cpp
    src/import/OffLoader.cpp
    src/import/DxfLoader.cpp
    src/import/SvgLoader.cpp
    src/import/MiniXml.cpp
    src/import/ZipReader.cpp
    src/import/ThreeMfLoader.cpp
    src/import/AmfLoader.cpp
    src/import/SurfaceLoader.cpp
    src/import/stb_truetype_impl.cpp
    src/import/stb_image_impl.cpp
    src/import/StbFontBackend.cpp
    src/import/NaiveLtrShaper.cpp
    src/import/TextLoader.cpp

    # Util
    src/util/ResourcePaths.cpp
    ${PLATFORM_UTIL_SOURCES}
)

add_library(chiselcad_core STATIC ${CHISELCAD_CORE_SOURCES})

target_include_directories(chiselcad_core PUBLIC
    src/
    third_party/
)

target_link_libraries(chiselcad_core PUBLIC
    manifold::manifold
    glm::glm
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ZLIB::ZLIB
)

target_compile_definitions(chiselcad_core PRIVATE
    CHISELCAD_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
)

if(MSVC)
    target_compile_options(chiselcad_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(chiselcad_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# stb_truetype's/stb_image's implementation TUs trigger warnings under
# -Wall -Wextra -Wpedantic (vendored code, not ours to fix) — silenced for
# these two files only rather than loosened project-wide.
if(MSVC)
    set_source_files_properties(src/import/stb_truetype_impl.cpp src/import/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(src/import/stb_truetype_impl.cpp src/import/stb_image_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# ============================================================
# chiselcad_cli — headless entry point. No Vulkan/GLFW/ImGui/VMA.
# ============================================================
add_executable(chiselcad_cli src/cli_main.cpp)
target_link_libraries(chiselcad_cli PRIVATE chiselcad_core)

if(MSVC)
    target_compile_options(chiselcad_cli PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(chiselcad_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS chiselcad_cli RUNTIME DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/chiselcad/resources)

# ============================================================
# chiselcad — Vulkan GUI application
# ============================================================
if(CHISELCAD_BUILD_GUI)
    find_package(Vulkan REQUIRED)
    find_package(glfw3 REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)

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

    set(CHISELCAD_GUI_SOURCES
        src/main.cpp

        # App
        src/app/Application.cpp
        src/app/Config.cpp
        src/app/MeshBuilder.cpp

        # Editor (external editor + file watcher only; no embedded editor in v1)
        src/editor/ExternalEditor.cpp
        src/editor/DiagnosticsPanel.cpp
        ${PLATFORM_SOURCES}

        # Render
        src/render/VulkanContext.cpp
        src/render/VmaAllocator.cpp
        src/render/Swapchain.cpp
        src/render/RenderGraph.cpp
        src/render/GpuMesh.cpp
        src/render/Pipeline.cpp
        src/render/Camera.cpp
        src/render/Renderer.cpp

        # GUI-only "open a bare .stl file" viewer path (needs render::Vertex
        # for direct GPU upload). Not part of the .scad build pipeline —
        # CsgEvaluator's import() builtin uses StlLoader.cpp directly, which
        # lives in chiselcad_core instead.
        src/import/StlImporter.cpp
    )

    add_executable(chiselcad ${CHISELCAD_GUI_SOURCES})
    add_dependencies(chiselcad chiselcad_shaders)

    target_include_directories(chiselcad PRIVATE
        src/
        third_party/
    )

    target_link_libraries(chiselcad PRIVATE
        chiselcad_core
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        glfw
        imgui::imgui
    )

    if(MSVC)
        target_compile_options(chiselcad PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(chiselcad PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    target_compile_definitions(chiselcad PRIVATE
        CHISELCAD_SHADER_DIR="${SHADER_BINARY_DIR}/"
        CHISELCAD_RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/"
        CHISELCAD_VERSION="${PROJECT_VERSION}"
        GLM_FORCE_DEPTH_ZERO_TO_ONE   # Vulkan depth range [0,1] not [-1,1]
    )

    install(TARGETS chiselcad RUNTIME DESTINATION bin)
    install(DIRECTORY ${SHADER_BINARY_DIR} DESTINATION share/chiselcad)
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
    tests/test_off_loader.cpp
    tests/test_dxf_loader.cpp
    tests/test_svg_loader.cpp
    tests/test_3mf_loader.cpp
    tests/test_amf_loader.cpp
    tests/test_surface_loader.cpp
    tests/test_text_loader.cpp
)

add_executable(chiselcad_tests ${CHISELCAD_TEST_SOURCES})

target_include_directories(chiselcad_tests PRIVATE src/ third_party/)

target_compile_definitions(chiselcad_tests PRIVATE
    CHISELCAD_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/"
)

target_link_libraries(chiselcad_tests PRIVATE
    chiselcad_core
    Catch2::Catch2WithMain
)

if(MSVC)
    target_compile_options(chiselcad_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(chiselcad_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(Catch)
catch_discover_tests(chiselcad_tests)
