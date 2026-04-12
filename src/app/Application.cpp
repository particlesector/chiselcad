#include "Application.h"
#include "csg/CsgEvaluator.h"
#include "csg/MeshCache.h"
#include "csg/MeshEvaluator.h"
#include "lang/Lexer.h"
#include "lang/Parser.h"
#include "render/GpuMesh.h"
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui.h>
#include <manifold/manifold.h>
#include <spdlog/spdlog.h>
#include <array>
#include <fstream>
#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace chisel::app {

// ---------------------------------------------------------------------------
// Manifold → flat-shaded vertex/index soup
// ---------------------------------------------------------------------------
static void manifoldToMesh(const manifold::Manifold& m,
                            std::vector<render::Vertex>& verts,
                            std::vector<uint32_t>& indices)
{
    verts.clear();
    indices.clear();

    auto mesh = m.GetMeshGL();
    // mesh.numProp == 3 (x,y,z per vertex)
    size_t triCount = mesh.triVerts.size() / 3;
    verts.reserve(triCount * 3);
    indices.reserve(triCount * 3);

    for (size_t t = 0; t < triCount; ++t) {
        uint32_t i0 = mesh.triVerts[t * 3 + 0];
        uint32_t i1 = mesh.triVerts[t * 3 + 1];
        uint32_t i2 = mesh.triVerts[t * 3 + 2];

        auto vp = [&](uint32_t i) {
            return glm::vec3(
                mesh.vertProperties[i * mesh.numProp + 0],
                mesh.vertProperties[i * mesh.numProp + 1],
                mesh.vertProperties[i * mesh.numProp + 2]);
        };

        glm::vec3 p0 = vp(i0), p1 = vp(i1), p2 = vp(i2);
        glm::vec3 n  = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({p0, n});
        verts.push_back({p1, n});
        verts.push_back({p2, n});
        indices.push_back(base);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }
}

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

    while (!glfwWindowShouldClose(m_window) && m_state.running) {
        glfwPollEvents();
        m_watcher->poll();

        if (m_state.meshDirty.exchange(false))
            rebuildMesh();

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

        if (!ok) {
            m_swapchain.recreate(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
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
    // Descriptor pool for ImGui
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
// File change + mesh rebuild
// ---------------------------------------------------------------------------
void Application::onFileChanged(const std::filesystem::path&) {
    m_state.meshDirty = true;
}

void Application::rebuildMesh() {
    spdlog::info("Rebuilding mesh from {}", m_state.scadPath.string());

    std::ifstream f(m_state.scadPath);
    if (!f.is_open()) {
        spdlog::error("Cannot open {}", m_state.scadPath.string());
        return;
    }
    std::string src((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    lang::Lexer  lexer(src);
    auto tokens = lexer.tokenize();
    if (lexer.hasErrors()) {
        m_diagPanel.setDiagnostics(lexer.diagnostics());
        return;
    }

    lang::Parser parser(std::move(tokens));
    auto result = parser.parse();
    if (parser.hasErrors()) {
        m_diagPanel.setDiagnostics(parser.diagnostics());
        return;
    }

    m_diagPanel.setDiagnostics({});

    csg::CsgEvaluator  csgEval;
    auto scene = csgEval.evaluate(result);

    csg::MeshCache     cache;
    csg::MeshEvaluator meshEval(cache);
    auto manifold = meshEval.evaluate(scene);

    std::vector<render::Vertex>  verts;
    std::vector<uint32_t>        indices;
    manifoldToMesh(manifold, verts, indices);

    if (!verts.empty())
        m_renderer.uploadMesh(m_ctx, m_vma, verts, indices);

    spdlog::info("Mesh: {} triangles", indices.size() / 3);
}

// ---------------------------------------------------------------------------
// ImGui
// ---------------------------------------------------------------------------
void Application::drawImGui() {
    ImGui::Begin("ChiselCAD");
    ImGui::Text("File: %s", m_state.scadPath.filename().string().c_str());
    if (ImGui::Button("Reload")) m_state.meshDirty = true;
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
