#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

struct GLFWwindow;

namespace chisel::render {

// ---------------------------------------------------------------------------
// VulkanContext — owns instance, device, queues, command pool, and surface.
// ---------------------------------------------------------------------------
class VulkanContext {
public:
    void init(GLFWwindow* window);
    void destroy();

    VkInstance       instance()       const { return m_instance; }
    VkPhysicalDevice physDevice()     const { return m_physDevice; }
    VkDevice         device()         const { return m_device; }
    VkQueue          graphicsQueue()  const { return m_graphicsQueue; }
    VkQueue          presentQueue()   const { return m_presentQueue; }
    uint32_t         graphicsFamily() const { return m_graphicsFamily; }
    uint32_t         presentFamily()  const { return m_presentFamily; }
    VkCommandPool    commandPool()    const { return m_commandPool; }
    VkSurfaceKHR     surface()        const { return m_surface; }
    bool fillModeNonSolidSupported()  const { return m_fillModeNonSolid; }

    // Submit a one-shot command buffer synchronously on the graphics queue
    VkCommandBuffer beginOneShot() const;
    void            endOneShot(VkCommandBuffer cmd) const;

private:
    void createInstance();
    void createSurface(GLFWwindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkPhysicalDevice m_physDevice     = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
    VkQueue          m_presentQueue   = VK_NULL_HANDLE;
    uint32_t         m_graphicsFamily = 0;
    uint32_t         m_presentFamily  = 0;
    VkCommandPool    m_commandPool    = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    bool             m_fillModeNonSolid = false;

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    void createDebugMessenger();
#endif
};

} // namespace chisel::render
