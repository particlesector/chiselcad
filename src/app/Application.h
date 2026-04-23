#pragma once
#include "AppState.h"
#include "Config.h"
#include "MeshBuilder.h"
#include "editor/DiagnosticsPanel.h"
#include "editor/FileWatcher.h"
#include "export/StlExporter.h"
#include "import/StlImporter.h"
#include "render/Camera.h"
#include "render/GpuMesh.h"
#include "render/Pipeline.h"
#include "render/Renderer.h"
#include "render/Swapchain.h"
#include "render/VmaAllocator.h"
#include "render/VulkanContext.h"
#include <filesystem>
#include <memory>
#include <string>
#include <glm/glm.hpp>

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

    // ImGui drawing
    void drawMenuBar();
    void drawImGui();
    void drawPrefsPopup();

    // Camera / file helpers
    void fitToView();
    void openFile();
    void exportStl();
    void loadStlFile(const std::filesystem::path& path);

    // GLFW callbacks (static → forward to instance)
    static void onScroll(GLFWwindow* w, double x, double y);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onMouseMove(GLFWwindow* w, double x, double y);
    static void onKey(GLFWwindow* w, int key, int scancode, int action, int mods);
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

    // Render mode
    render::RenderMode m_renderMode = render::RenderMode::Solid;

    // Last successfully built/loaded mesh (kept for STL export)
    std::vector<render::Vertex>   m_meshVerts;
    std::vector<uint32_t>         m_meshIndices;

    // True when the current file is a loaded STL (no build pipeline)
    bool     m_isStlFile    = false;
    uint32_t m_stlTriCount  = 0;
    uint32_t m_stlVertCount = 0;

    // Mesh bounds (updated on each successful build)
    glm::vec3 m_meshBoundsMin{0.0f, 0.0f, 0.0f};
    glm::vec3 m_meshBoundsMax{1.0f, 1.0f, 1.0f};
    bool      m_hasMeshBounds = false;
    bool      m_firstMesh     = true;

    // Presentation mode — auto-orbits the camera at a fixed angular speed
    bool   m_presentationMode = false;
    double m_lastFrameTime    = 0.0;

    // User preferences (opt-in analysis / rendering flags)
    struct AppPrefs {
        bool warnOverlappingRoots = false;
    };
    AppPrefs  m_prefs;

    // UI state
    bool      m_showChiselPanel = true;
    bool      m_showAbout       = false;
    bool      m_showPrefs       = false;
    float     m_fontScale       = 1.0f;

    // Export error shown in a modal
    std::string m_exportError;

    VkDescriptorPool                   m_imguiPool   = VK_NULL_HANDLE;
};

} // namespace chisel::app
