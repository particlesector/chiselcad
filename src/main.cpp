#include "app/Application.h"
#include <spdlog/spdlog.h>
#include <exception>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    spdlog::set_level(spdlog::level::info);

    std::filesystem::path scadPath;
    if (argc >= 2) {
        scadPath = argv[1];
    } else {
        // Default to chiselcad_test.scad in the working directory
        scadPath = "chiselcad_test.scad";
    }

    if (!std::filesystem::exists(scadPath)) {
        spdlog::error("File not found: {}", scadPath.string());
        std::cerr << "Usage: chiselcad <file.scad>\n";
        return 1;
    }

    try {
        chisel::app::Application app(scadPath);
        app.run();
    } catch (const std::exception& e) {
        spdlog::critical("Fatal: {}", e.what());
        return 1;
    }
    return 0;
}
