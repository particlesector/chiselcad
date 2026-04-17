#pragma once
#include "AppState.h"
#include "Config.h"
#include "MeshBuilder.h"
#include "editor/DiagnosticsPanel.h"
#include "editor/FileWatcher.h"
#include "render/Camera.h"
#include "render/Pipeline.h"
#include "render/Renderer.h"
#include "render/Swapchain.h"
#include "render/VmaAllocator.h"
#include "render/VulkanContext.h"
#include <filesystem>
#include <memory>
#include <string>

struct GLFWwindow;

namespace chisel::app {

// ---------------------------------------------------------------------------
// Application — owns all subsystems and runs the main loop.
// ---------------------------------------------------------------------------
class Application {
public:
    explicit Application(std::filesystem::path scadPath);
    ~Application();

    void run();

private:
    void initWindow();
    void initVulkan();
    void initImGui();
    void shutdownImGui();

    void onFileChanged(const std::filesystem::path& path);

    void drawImGui();

    // GLFW callbacks (static → forward to instance)
    static void onScroll(GLFWwindow* w, double x, double y);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onMouseMove(GLFWwindow* w, double x, double y);
    static void onFramebufferResize(GLFWwindow* w, int width, int height);

    Config                             m_config;
    AppState                           m_state;
    GLFWwindow*                        m_window      = nullptr;
    bool                               m_resized     = false;

    render::VulkanContext              m_ctx;
    render::VmaWrapper                 m_vma;
    render::Swapchain                  m_swapchain;
    render::Pipeline                   m_pipeline;
    render::Renderer                   m_renderer;
    render::Camera                     m_camera;

    MeshBuilder                          m_meshBuilder;
    bool                                 m_useManifoldSphere = false;
    std::unique_ptr<editor::FileWatcher> m_watcher;
    editor::DiagnosticsPanel             m_diagPanel;

    VkDescriptorPool                   m_imguiPool   = VK_NULL_HANDLE;
};

} // namespace chisel::app
