#include "Application.h"
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <array>
#include <stdexcept>
#include <string>

namespace chisel::app {

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
Application::Application(std::filesystem::path scadPath) {
    m_state.scadPath = std::move(scadPath);
    m_config = Config::load(Config::defaultPath());
}

Application::~Application() {
    if (m_window) {
        vkDeviceWaitIdle(m_ctx.device());
        shutdownImGui();
        m_renderer.destroy(m_ctx.device());
        m_pipeline.destroy(m_ctx.device());
        m_swapchain.destroy(m_ctx.device());
        m_vma.destroy();
        m_ctx.destroy();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }
}

// ---------------------------------------------------------------------------
// run
// ---------------------------------------------------------------------------
void Application::run() {
    initWindow();
    initVulkan();
    initImGui();

    m_camera.init(m_config.cameraDistance);

    m_watcher = std::make_unique<editor::FileWatcher>(
        m_state.scadPath,
        [this](const auto& p) { onFileChanged(p); });

    // Kick off the initial build
    m_meshBuilder.requestBuild(m_state.scadPath);

    while (!glfwWindowShouldClose(m_window) && m_state.running) {
        glfwPollEvents();
        m_watcher->poll();

        // Request a new async build if the file changed
        if (m_state.meshDirty.exchange(false))
            m_meshBuilder.requestBuild(m_state.scadPath);

        // Upload finished mesh on the main (Vulkan) thread
        if (auto result = m_meshBuilder.poll()) {
            m_diagPanel.setDiagnostics(result->diags);
            if (!result->verts.empty())
                m_renderer.uploadMesh(m_ctx, m_vma, result->verts, result->indices);
        }

        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        if (w == 0 || h == 0) continue; // minimised

        if (m_resized) {
            m_swapchain.recreate(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            m_resized = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawImGui();
        ImGui::Render();

        bool ok = m_renderer.drawFrame(
            m_ctx, m_swapchain, m_pipeline, m_camera,
            static_cast<uint32_t>(w), static_cast<uint32_t>(h));

        if (!ok)
            m_swapchain.recreate(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    vkDeviceWaitIdle(m_ctx.device());
    m_config.save(Config::defaultPath());
}

// ---------------------------------------------------------------------------
// Init helpers
// ---------------------------------------------------------------------------
void Application::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_config.windowWidth, m_config.windowHeight,
                                "ChiselCAD", nullptr, nullptr);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetScrollCallback(m_window, onScroll);
    glfwSetMouseButtonCallback(m_window, onMouseButton);
    glfwSetCursorPosCallback(m_window, onMouseMove);
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
}

void Application::initVulkan() {
    m_ctx.init(m_window);

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    m_vma.init(m_ctx.instance(), m_ctx.physDevice(), m_ctx.device());
    m_swapchain.init(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    m_pipeline.init(m_ctx.device(), m_swapchain.renderPass(), CHISELCAD_SHADER_DIR);
    m_renderer.init(m_ctx, m_swapchain);
}

void Application::initImGui() {
    std::array<VkDescriptorPoolSize, 1> poolSizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}
    }};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets       = 16;
    pci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pci.pPoolSizes    = poolSizes.data();
    vkCreateDescriptorPool(m_ctx.device(), &pci, nullptr, &m_imguiPool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    ImGui_ImplVulkan_InitInfo ii{};
    ii.Instance        = m_ctx.instance();
    ii.PhysicalDevice  = m_ctx.physDevice();
    ii.Device          = m_ctx.device();
    ii.QueueFamily     = m_ctx.graphicsFamily();
    ii.Queue           = m_ctx.graphicsQueue();
    ii.DescriptorPool  = m_imguiPool;
    ii.RenderPass      = m_swapchain.renderPass();
    ii.MinImageCount   = 2;
    ii.ImageCount      = m_swapchain.imageCount();
    ii.MSAASamples     = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&ii);
    ImGui_ImplVulkan_CreateFontsTexture();
}

void Application::shutdownImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_imguiPool) {
        vkDestroyDescriptorPool(m_ctx.device(), m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// File change
// ---------------------------------------------------------------------------
void Application::onFileChanged(const std::filesystem::path&) {
    m_state.meshDirty = true;
}

// ---------------------------------------------------------------------------
// ImGui
// ---------------------------------------------------------------------------
void Application::drawImGui() {
    // Animated spinner frames (cycles at ~8 fps)
    static const char* kSpinner[] = { "|", "/", "-", "\\" };
    int spinIdx = static_cast<int>(ImGui::GetTime() * 8.0) % 4;

    ImGui::Begin("ChiselCAD");
    ImGui::Text("File: %s", m_state.scadPath.filename().string().c_str());
    if (ImGui::Button("Reload"))
        m_meshBuilder.requestBuild(m_state.scadPath);

    ImGui::Separator();

    switch (m_meshBuilder.phase()) {
        case BuildPhase::Idle:
            ImGui::TextDisabled("Idle");
            break;
        case BuildPhase::Parsing:
            ImGui::Text("%s Parsing...", kSpinner[spinIdx]);
            break;
        case BuildPhase::Evaluating:
            ImGui::Text("%s Evaluating CSG...", kSpinner[spinIdx]);
            break;
        case BuildPhase::Meshing:
            ImGui::Text("%s Meshing (Manifold)...", kSpinner[spinIdx]);
            break;
        case BuildPhase::Converting:
            ImGui::Text("%s Converting...", kSpinner[spinIdx]);
            break;
        case BuildPhase::Done:
            ImGui::TextColored({0.5f, 1.0f, 0.5f, 1.0f}, "Ready");
            ImGui::SameLine();
            ImGui::Text("%u tris  %.2fs",
                m_meshBuilder.lastTriCount(),
                m_meshBuilder.elapsedMs() / 1000.0);
            break;
        case BuildPhase::Error:
            ImGui::TextColored({1.0f, 0.35f, 0.35f, 1.0f},
                               "Error — see Diagnostics");
            break;
    }

    ImGui::End();

    m_diagPanel.draw();
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------
void Application::onScroll(GLFWwindow* w, double, double y) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) app->m_camera.onScroll(y);
}

void Application::onMouseButton(GLFWwindow* w, int button, int action, int) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) app->m_camera.onMouseButton(button, action);
}

void Application::onMouseMove(GLFWwindow* w, double x, double y) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantCaptureMouse) app->m_camera.onMouseMove(x, y);
}

void Application::onFramebufferResize(GLFWwindow* w, int, int) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    app->m_resized = true;
}

} // namespace chisel::app
