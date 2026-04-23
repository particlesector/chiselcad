#include "Application.h"
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <commdlg.h>
#endif

#include "editor/ExternalEditor.h"

namespace chisel::app {
using namespace chisel::io;

// Font scale lookup (indexed by m_config.fontSize: 0=small, 1=normal, 2=large)
static constexpr std::array<float, 3> kFontScales = { 0.85f, 1.0f, 1.3f };

// Formats an integer with comma thousands separators, e.g. 47024 → "47,024"
static std::string formatCount(uint32_t n) {
    std::string s = std::to_string(n);
    int i = static_cast<int>(s.size()) - 3;
    while (i > 0) { s.insert(static_cast<size_t>(i), ","); i -= 3; }
    return s;
}

// ---------------------------------------------------------------------------
// Platform file-open dialog
// ---------------------------------------------------------------------------
static std::filesystem::path openFileDialog() {
#ifdef _WIN32
    char szFile[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = sizeof(szFile);
    ofn.lpstrFilter  = "Supported Files\0*.scad;*.stl\0OpenSCAD Files\0*.scad\0STL Files\0*.stl\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn))
        return std::filesystem::path(szFile);
#endif
    return {};
}

static std::filesystem::path saveFileDialog(const std::string& defaultName) {
#ifdef _WIN32
    char szFile[MAX_PATH] = {};
    defaultName.copy(szFile, MAX_PATH - 1);
    OPENFILENAMEA ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.lpstrFile    = szFile;
    ofn.nMaxFile     = sizeof(szFile);
    ofn.lpstrFilter  = "STL Files\0*.stl\0All Files\0*.*\0";
    ofn.lpstrDefExt  = "stl";
    ofn.nFilterIndex = 1;
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn))
        return std::filesystem::path(szFile);
#else
    (void)defaultName;
#endif
    return {};
}

// ---------------------------------------------------------------------------
// ctor / dtor
// ---------------------------------------------------------------------------
Application::Application(std::filesystem::path scadPath) {
    m_state.scadPath = std::move(scadPath);
    m_config = Config::load(Config::defaultPath());
    m_fontScale = kFontScales[std::clamp(m_config.fontSize, 0, 2)];
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
    m_camera.setState(m_config.cameraYaw, m_config.cameraPitch,
                      m_config.cameraDistance,
                      {m_config.cameraTargetX, m_config.cameraTargetY, m_config.cameraTargetZ});
    m_meshBuilder.setWarnOverlappingRoots(m_config.warnOverlappingRoots);

    // Restore last-opened file when none was provided on the command line
    if (m_state.scadPath.empty() && !m_config.lastFilePath.empty()) {
        std::filesystem::path lastPath(m_config.lastFilePath);
        std::error_code ec;
        if (std::filesystem::exists(lastPath, ec)) {
            m_state.scadPath = lastPath;
            m_firstMesh = false; // use restored camera, don't auto-fit
        }
    }

    if (!m_state.scadPath.empty()) {
        auto ext = m_state.scadPath.extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (ext == ".stl") {
            loadStlFile(m_state.scadPath);
        } else {
            m_diagPanel.setScadPath(m_state.scadPath);
            m_watcher = std::make_unique<editor::FileWatcher>(
                m_state.scadPath,
                [this](const auto& p) { onFileChanged(p); });
            m_meshBuilder.requestBuild(m_state.scadPath);
        }
    }

    m_lastFrameTime = glfwGetTime();

    while (!glfwWindowShouldClose(m_window) && m_state.running) {
        glfwPollEvents();
        if (m_watcher) m_watcher->poll();

        double now = glfwGetTime();
        float dt = static_cast<float>(now - m_lastFrameTime);
        m_lastFrameTime = now;

        if (m_presentationMode)
            m_camera.orbitYaw(dt * 0.5f); // ~28.6 deg/s

        if (m_state.meshDirty.exchange(false) && !m_state.scadPath.empty())
            m_meshBuilder.requestBuild(m_state.scadPath);

        if (auto result = m_meshBuilder.poll()) {
            m_diagPanel.setDiagnostics(result->diags);
            if (!result->verts.empty()) {
                // Compute AABB for fit-to-view
                glm::vec3 bmin{ 1e30f,  1e30f,  1e30f};
                glm::vec3 bmax{-1e30f, -1e30f, -1e30f};
                for (const auto& v : result->verts) {
                    bmin = glm::min(bmin, v.pos);
                    bmax = glm::max(bmax, v.pos);
                }
                m_meshBoundsMin  = bmin;
                m_meshBoundsMax  = bmax;
                m_hasMeshBounds  = true;
                if (m_firstMesh) {
                    m_camera.fitToBounds(bmin, bmax);
                    m_firstMesh = false;
                }
                m_meshVerts   = result->verts;
                m_meshIndices = result->indices;
                m_renderer.uploadMesh(m_ctx, m_vma, result->verts, result->indices);
            }
        }

        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        if (w == 0 || h == 0) continue;

        if (m_resized) {
            m_swapchain.recreate(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            m_resized = false;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().FontGlobalScale = m_fontScale;
        drawImGui();
        ImGui::Render();

        bool ok = m_renderer.drawFrame(
            m_ctx, m_swapchain, m_pipeline, m_camera,
            static_cast<uint32_t>(w), static_cast<uint32_t>(h),
            m_renderMode);

        if (!ok)
            m_swapchain.recreate(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    vkDeviceWaitIdle(m_ctx.device());

    // Persist window state
    int ww = 0, wh = 0;
    glfwGetWindowSize(m_window, &ww, &wh);
    if (ww > 0 && wh > 0) {
        m_config.windowWidth  = ww;
        m_config.windowHeight = wh;
    }

    // Persist camera state
    m_config.cameraDistance = m_camera.distance();
    m_config.cameraYaw      = m_camera.yaw();
    m_config.cameraPitch    = m_camera.pitch();
    m_config.cameraTargetX  = m_camera.target().x;
    m_config.cameraTargetY  = m_camera.target().y;
    m_config.cameraTargetZ  = m_camera.target().z;

    // Persist last opened file
    m_config.lastFilePath = m_state.scadPath.string();

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
    glfwSetKeyCallback(m_window, onKey);
    glfwSetFramebufferSizeCallback(m_window, onFramebufferResize);
}

void Application::initVulkan() {
    m_ctx.init(m_window);

    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    m_vma.init(m_ctx.instance(), m_ctx.physDevice(), m_ctx.device());
    m_swapchain.init(m_ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    m_pipeline.init(m_ctx.device(), m_swapchain.renderPass(), CHISELCAD_SHADER_DIR,
                    m_ctx.fillModeNonSolidSupported());
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
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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
// Camera helpers
// ---------------------------------------------------------------------------
void Application::fitToView() {
    if (!m_hasMeshBounds) return;
    m_camera.fitToBounds(m_meshBoundsMin, m_meshBoundsMax);
}

void Application::openFile() {
    auto path = openFileDialog();
    if (path.empty()) return;

    auto ext = path.extension().string();
    // lowercase the extension for comparison
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".stl") {
        loadStlFile(path);
        return;
    }

    // .scad path
    m_isStlFile      = false;
    m_state.scadPath = path;
    m_firstMesh      = true;
    m_diagPanel.setScadPath(path);
    m_watcher = std::make_unique<editor::FileWatcher>(
        path, [this](const auto& p) { onFileChanged(p); });
    m_meshBuilder.requestBuild(path);
}

void Application::loadStlFile(const std::filesystem::path& path) {
    auto mesh = io::loadStl(path);
    if (!mesh.error.empty()) {
        m_exportError = "STL load failed: " + mesh.error; // reuse error modal
        spdlog::error("STL load failed: {}", mesh.error);
        return;
    }

    m_isStlFile      = true;
    m_stlTriCount    = static_cast<uint32_t>(mesh.indices.size() / 3);
    m_stlVertCount   = static_cast<uint32_t>(mesh.verts.size());
    m_state.scadPath = path; // used for window title / export default name
    m_firstMesh      = true;
    m_watcher.reset(); // no file watching for STL

    m_meshVerts   = mesh.verts;
    m_meshIndices = mesh.indices;

    // Compute AABB and fit camera
    glm::vec3 bmin{ 1e30f,  1e30f,  1e30f};
    glm::vec3 bmax{-1e30f, -1e30f, -1e30f};
    for (const auto& v : mesh.verts) {
        bmin = glm::min(bmin, v.pos);
        bmax = glm::max(bmax, v.pos);
    }
    m_meshBoundsMin = bmin;
    m_meshBoundsMax = bmax;
    m_hasMeshBounds = true;
    m_camera.fitToBounds(bmin, bmax);
    m_firstMesh = false;

    m_renderer.uploadMesh(m_ctx, m_vma, mesh.verts, mesh.indices);
    spdlog::info("Loaded STL: {} ({} triangles)", path.string(), m_stlTriCount);
}

void Application::exportStl() {
    if (m_meshVerts.empty()) return;
    std::string defaultName = m_state.scadPath.stem().string();
    if (defaultName.empty()) defaultName = "export";
    defaultName += ".stl";

    auto outPath = saveFileDialog(defaultName);
    if (outPath.empty()) return;

    auto err = io::exportBinaryStl(outPath, m_meshVerts, m_meshIndices);
    if (!err.empty()) {
        m_exportError = err;
        spdlog::error("STL export failed: {}", err);
    } else {
        spdlog::info("Exported STL: {}", outPath.string());
    }
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------
void Application::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;

    // --- File ---
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            openFile();
        ImGui::Separator();
        if (ImGui::MenuItem("Reload", "R"))
            m_meshBuilder.requestBuild(m_state.scadPath);
        ImGui::Separator();
        bool hasMesh = !m_meshVerts.empty();
        if (!hasMesh) ImGui::BeginDisabled();
        if (ImGui::MenuItem("Export STL..."))
            exportStl();
        if (!hasMesh) ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            m_state.running = false;
        ImGui::EndMenu();
    }

    // --- View ---

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("ChiselCAD Panel", nullptr, &m_showChiselPanel);
        if (ImGui::MenuItem("Presentation Mode", "P", m_presentationMode))
            m_presentationMode = !m_presentationMode;
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences..."))
            m_showPrefs = true;
        ImGui::Separator();

        if (ImGui::BeginMenu("Rendering")) {
            bool isSolid     = (m_renderMode == render::RenderMode::Solid);
            bool isWire      = (m_renderMode == render::RenderMode::Wireframe);
            bool isSolidEdge = (m_renderMode == render::RenderMode::SolidEdges);
            if (ImGui::MenuItem("Solid",        nullptr, isSolid))     m_renderMode = render::RenderMode::Solid;
            if (ImGui::MenuItem("Wireframe",    nullptr, isWire))      m_renderMode = render::RenderMode::Wireframe;
            if (ImGui::MenuItem("Solid + Edges",nullptr, isSolidEdge)) m_renderMode = render::RenderMode::SolidEdges;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Font Size")) {
            if (ImGui::MenuItem("Small",  nullptr, m_config.fontSize == 0)) { m_config.fontSize = 0; m_fontScale = kFontScales[0]; }
            if (ImGui::MenuItem("Normal", nullptr, m_config.fontSize == 1)) { m_config.fontSize = 1; m_fontScale = kFontScales[1]; }
            if (ImGui::MenuItem("Large",  nullptr, m_config.fontSize == 2)) { m_config.fontSize = 2; m_fontScale = kFontScales[2]; }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- Camera ---
    if (ImGui::BeginMenu("Camera")) {
        if (ImGui::MenuItem("Fit All",    "F")) fitToView();
        ImGui::Separator();
        if (ImGui::MenuItem("Front",      "1")) m_camera.setNamedView(render::Camera::NamedView::Front);
        if (ImGui::MenuItem("Back",       "2")) m_camera.setNamedView(render::Camera::NamedView::Back);
        if (ImGui::MenuItem("Right",      "3")) m_camera.setNamedView(render::Camera::NamedView::Right);
        if (ImGui::MenuItem("Left",       "4")) m_camera.setNamedView(render::Camera::NamedView::Left);
        if (ImGui::MenuItem("Top",        "5")) m_camera.setNamedView(render::Camera::NamedView::Top);
        if (ImGui::MenuItem("Bottom",     "6")) m_camera.setNamedView(render::Camera::NamedView::Bottom);
        if (ImGui::MenuItem("Isometric",  "7")) m_camera.setNamedView(render::Camera::NamedView::Isometric);
        ImGui::EndMenu();
    }

    // --- Help ---
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About ChiselCAD"))
            m_showAbout = true;
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

// ---------------------------------------------------------------------------
// Preferences popup
// ---------------------------------------------------------------------------
void Application::drawPrefsPopup() {
    if (m_showPrefs) {
        ImGui::OpenPopup("Preferences");
        m_showPrefs = false;
    }

    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Preferences", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {

        // ── Analysis ─────────────────────────────────────────────────────
        ImGui::SeparatorText("Analysis");

        bool prev = m_config.warnOverlappingRoots;
        ImGui::Checkbox("Warn on overlapping root objects", &m_config.warnOverlappingRoots);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "After each build, test whether any top-level objects\n"
                "overlap and warn if so. Has a small per-pair cost;\n"
                "disable for large scenes with many root objects.");
        if (m_config.warnOverlappingRoots != prev) {
            m_meshBuilder.setWarnOverlappingRoots(m_config.warnOverlappingRoots);
            if (!m_state.scadPath.empty())
                m_meshBuilder.requestBuild(m_state.scadPath);
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Close", {80, 0}))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// ImGui
// ---------------------------------------------------------------------------
void Application::drawImGui() {
    // ------------------------------------------------------------------
    // Fullscreen host window — covers the full viewport.
    // The menu bar lives INSIDE this window (ImGuiWindowFlags_MenuBar) so
    // the dockspace starts immediately below it with no manual offset needed.
    // ------------------------------------------------------------------
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    constexpr ImGuiWindowFlags kHostFlags =
        ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoTitleBar        | ImGuiWindowFlags_NoCollapse   |
        ImGuiWindowFlags_NoResize          | ImGuiWindowFlags_NoMove       |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,  0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(0, 0));
    ImGui::Begin("##DockHost", nullptr, kHostFlags);
    ImGui::PopStyleVar(3);

    drawMenuBar(); // renders into the window's menu bar slot via BeginMenuBar/EndMenuBar

    ImGuiID dockId = ImGui::GetID("MainDockSpace");
    bool needsLayout = (ImGui::DockBuilderGetNode(dockId) == nullptr);
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    if (needsLayout) {
        ImGuiID left = 0, centre = 0;
        ImGui::DockBuilderSplitNode(dockId, ImGuiDir_Left, 0.25f, &left, &centre);
        ImGui::DockBuilderDockWindow("ChiselCAD", left);
        ImGui::DockBuilderFinish(dockId);
    }

    ImGui::End(); // ##DockHost

    // ------------------------------------------------------------------
    // ChiselCAD panel (contains Diagnostics as a collapsible section)
    // ------------------------------------------------------------------
    if (m_showChiselPanel) {
        static const char* kSpinner[] = { "|", "/", "-", "\\" };
        int spinIdx = static_cast<int>(ImGui::GetTime() * 8.0) % 4;

        ImGui::Begin("ChiselCAD");

        ImGui::Text("File: %s", m_state.scadPath.filename().string().c_str());

        if (m_isStlFile) {
            ImGui::TextDisabled("STL — view only");
            ImGui::Separator();
            ImGui::Text("%s facets | %s vertices",
                formatCount(m_stlTriCount).c_str(),
                formatCount(m_stlVertCount).c_str());
        } else {
            if (ImGui::Button("Reload"))
                m_meshBuilder.requestBuild(m_state.scadPath);

            ImGui::Separator();

            bool prev = m_useManifoldSphere;
            ImGui::Checkbox("Manifold sphere", &m_useManifoldSphere);
            if (m_useManifoldSphere != prev) {
                m_meshBuilder.setUseManifoldSphere(m_useManifoldSphere);
                m_meshBuilder.requestBuild(m_state.scadPath);
            }

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
                    ImGui::TextColored({0.5f, 1.0f, 0.5f, 1.0f}, "Render: %.3fs",
                        m_meshBuilder.elapsedMs() / 1000.0);
                    ImGui::Text("%s facets | %s vertices",
                        formatCount(m_meshBuilder.lastTriCount()).c_str(),
                        formatCount(m_meshBuilder.lastVertCount()).c_str());
                    ImGui::Text("Volume:  %.4f", m_meshBuilder.lastVolume());
                    ImGui::Text("Area:    %.4f", m_meshBuilder.lastSurfaceArea());
                    break;
                case BuildPhase::Error:
                    ImGui::TextColored({1.0f, 0.35f, 0.35f, 1.0f},
                                       "Error — see Diagnostics");
                    break;
            }
        }

        ImGui::Separator();

        // Diagnostics — only relevant for .scad files
        if (!m_isStlFile) {
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
                m_diagPanel.drawInline();
        }

        ImGui::End();
    }

    // ------------------------------------------------------------------
    // About modal
    // ------------------------------------------------------------------
    // Export error modal
    if (!m_exportError.empty())
        ImGui::OpenPopup("Export Error");
    if (ImGui::BeginPopupModal("Export Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(m_exportError.c_str());
        ImGui::Separator();
        if (ImGui::Button("OK", {80, 0})) {
            ImGui::CloseCurrentPopup();
            m_exportError.clear();
        }
        ImGui::EndPopup();
    }

    drawPrefsPopup();

    if (m_showAbout)
        ImGui::OpenPopup("About ChiselCAD");

    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("About ChiselCAD", nullptr,
                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("ChiselCAD  v" CHISELCAD_VERSION);
        ImGui::TextDisabled("GPU-accelerated CSG modeler");
        ImGui::Separator();
        ImGui::BulletText("Vulkan 1.2");
        ImGui::BulletText("Dear ImGui");
        ImGui::BulletText("Manifold CSG");
        ImGui::BulletText("OpenSCAD .scad syntax");
        ImGui::Separator();
        if (ImGui::Button("Close", {80, 0})) {
            ImGui::CloseCurrentPopup();
            m_showAbout = false;
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// GLFW callbacks
// ---------------------------------------------------------------------------
void Application::onScroll(GLFWwindow* w, double, double y) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!ImGui::GetIO().WantCaptureMouse) app->m_camera.onScroll(y);
}

void Application::onMouseButton(GLFWwindow* w, int button, int action, int) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!ImGui::GetIO().WantCaptureMouse) app->m_camera.onMouseButton(button, action);
}

void Application::onMouseMove(GLFWwindow* w, double x, double y) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (!ImGui::GetIO().WantCaptureMouse) app->m_camera.onMouseMove(x, y);
}

void Application::onKey(GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    using NV = render::Camera::NamedView;
    switch (key) {
        case GLFW_KEY_F: app->fitToView(); break;
        case GLFW_KEY_1: app->m_camera.setNamedView(NV::Front);     break;
        case GLFW_KEY_2: app->m_camera.setNamedView(NV::Back);      break;
        case GLFW_KEY_3: app->m_camera.setNamedView(NV::Right);     break;
        case GLFW_KEY_4: app->m_camera.setNamedView(NV::Left);      break;
        case GLFW_KEY_5: app->m_camera.setNamedView(NV::Top);       break;
        case GLFW_KEY_6: app->m_camera.setNamedView(NV::Bottom);    break;
        case GLFW_KEY_7: app->m_camera.setNamedView(NV::Isometric); break;
        case GLFW_KEY_P: app->m_presentationMode = !app->m_presentationMode; break;
        default: break;
    }
}

void Application::onFramebufferResize(GLFWwindow* w, int, int) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(w));
    app->m_resized = true;
}

} // namespace chisel::app
