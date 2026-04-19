#include "VulkanContext.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace chisel::render {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
#define VK_CHECK(expr) do { \
    VkResult r_ = (expr); \
    if (r_ != VK_SUCCESS) { \
        spdlog::critical("Vulkan error {} at {}:{}", static_cast<int>(r_), __FILE__, __LINE__); \
        throw std::runtime_error("Vulkan error"); \
    } \
} while(0)

#ifndef NDEBUG
static const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        spdlog::warn("[Vulkan] {}", data->pMessage);
    return VK_FALSE;
}
#endif

// ---------------------------------------------------------------------------
// init / destroy
// ---------------------------------------------------------------------------
void VulkanContext::init(GLFWwindow* window) {
    createInstance();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
#ifndef NDEBUG
    createDebugMessenger();
#endif
}

void VulkanContext::destroy() {
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
#ifndef NDEBUG
    if (m_debugMessenger) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(m_instance, m_debugMessenger, nullptr);
    }
#endif
    if (m_device)   vkDestroyDevice(m_device, nullptr);
    if (m_surface)  vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    if (m_instance) vkDestroyInstance(m_instance, nullptr);
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------
void VulkanContext::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ChiselCAD";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion       = VK_API_VERSION_1_2;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);

#ifndef NDEBUG
    exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

#ifndef NDEBUG
    // Check validation layer support
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    bool found = false;
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, kValidationLayer) == 0) { found = true; break; }

    if (found) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = &kValidationLayer;
    } else {
        spdlog::warn("Validation layer not available; running without it");
    }
#endif

    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));
}

// ---------------------------------------------------------------------------
// Debug messenger
// ---------------------------------------------------------------------------
#ifndef NDEBUG
void VulkanContext::createDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    ci.pfnUserCallback = debugCallback;

    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (fn) fn(m_instance, &ci, nullptr, &m_debugMessenger);
}
#endif

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------
void VulkanContext::createSurface(GLFWwindow* window) {
    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
}

// ---------------------------------------------------------------------------
// Physical device
// ---------------------------------------------------------------------------
static bool deviceSuitable(VkPhysicalDevice dev, VkSurfaceKHR surface) {
    // Require VK_KHR_swapchain
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());
    bool hasSwapchain = false;
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            hasSwapchain = true; break;
        }
    if (!hasSwapchain) return false;

    // Require at least one surface format and present mode
    uint32_t fmtCount = 0, modeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmtCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &modeCount, nullptr);
    return fmtCount > 0 && modeCount > 0;
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) throw std::runtime_error("No Vulkan-capable GPU found");

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());

    // Prefer discrete GPU
    for (auto dev : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(dev, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && deviceSuitable(dev, m_surface)) {
            m_physDevice = dev;
            spdlog::info("GPU: {} (discrete)", p.deviceName);
            return;
        }
    }
    for (auto dev : devs) {
        if (deviceSuitable(dev, m_surface)) {
            VkPhysicalDeviceProperties p{};
            vkGetPhysicalDeviceProperties(dev, &p);
            m_physDevice = dev;
            spdlog::info("GPU: {}", p.deviceName);
            return;
        }
    }
    throw std::runtime_error("No suitable GPU found");
}

// ---------------------------------------------------------------------------
// Logical device + queues
// ---------------------------------------------------------------------------
void VulkanContext::createLogicalDevice() {
    // Find queue families
    uint32_t famCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &famCount, nullptr);
    std::vector<VkQueueFamilyProperties> fams(famCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physDevice, &famCount, fams.data());

    m_graphicsFamily = UINT32_MAX;
    m_presentFamily  = UINT32_MAX;
    for (uint32_t i = 0; i < famCount; ++i) {
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            m_graphicsFamily = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physDevice, i, m_surface, &present);
        if (present) m_presentFamily = i;
        if (m_graphicsFamily != UINT32_MAX && m_presentFamily != UINT32_MAX) break;
    }
    if (m_graphicsFamily == UINT32_MAX || m_presentFamily == UINT32_MAX)
        throw std::runtime_error("Required queue families not found");

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCIs;
    for (uint32_t fam : {m_graphicsFamily, m_presentFamily}) {
        bool already = false;
        for (const auto& q : queueCIs) if (q.queueFamilyIndex == fam) { already = true; break; }
        if (already) continue;
        VkDeviceQueueCreateInfo q{};
        q.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        q.queueFamilyIndex = fam;
        q.queueCount       = 1;
        q.pQueuePriorities = &priority;
        queueCIs.push_back(q);
    }

    // Enable fillModeNonSolid if the GPU supports it (needed for wireframe)
    VkPhysicalDeviceFeatures available{};
    vkGetPhysicalDeviceFeatures(m_physDevice, &available);
    VkPhysicalDeviceFeatures enabled{};
    if (available.fillModeNonSolid) {
        enabled.fillModeNonSolid = VK_TRUE;
        m_fillModeNonSolid = true;
    }

    const char* swapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueCIs.size());
    ci.pQueueCreateInfos       = queueCIs.data();
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = &swapchainExt;
    ci.pEnabledFeatures        = &enabled;

    VK_CHECK(vkCreateDevice(m_physDevice, &ci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentFamily,  0, &m_presentQueue);
}

// ---------------------------------------------------------------------------
// Command pool
// ---------------------------------------------------------------------------
void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_graphicsFamily;
    ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &ci, nullptr, &m_commandPool));
}

// ---------------------------------------------------------------------------
// One-shot commands
// ---------------------------------------------------------------------------
VkCommandBuffer VulkanContext::beginOneShot() const {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void VulkanContext::endOneShot(VkCommandBuffer cmd) const {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
}

} // namespace chisel::render
