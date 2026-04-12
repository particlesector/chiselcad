#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace chisel::render {
class VulkanContext;
}

namespace chisel::render {

// ---------------------------------------------------------------------------
// Swapchain — surface format, swap images, depth buffer, render pass,
// framebuffers.  Call recreate() on VK_ERROR_OUT_OF_DATE_KHR.
// ---------------------------------------------------------------------------
class Swapchain {
public:
    void init(const VulkanContext& ctx, uint32_t width, uint32_t height);
    void recreate(const VulkanContext& ctx, uint32_t width, uint32_t height);
    void destroy(VkDevice device);

    VkSwapchainKHR swapchain()   const { return m_swapchain; }
    VkRenderPass   renderPass()  const { return m_renderPass; }
    uint32_t       imageCount()  const { return static_cast<uint32_t>(m_images.size()); }
    VkFramebuffer  framebuffer(uint32_t i) const { return m_framebuffers[i]; }
    VkExtent2D     extent()      const { return m_extent; }
    VkFormat       colorFormat() const { return m_colorFormat; }

private:
    void createSwapchain(const VulkanContext& ctx, uint32_t width, uint32_t height);
    void createImageViews(VkDevice device);
    void createDepthResources(const VulkanContext& ctx);
    void createRenderPass(VkDevice device);
    void createFramebuffers(VkDevice device);
    void destroyResources(VkDevice device);

    VkSwapchainKHR           m_swapchain   = VK_NULL_HANDLE;
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat                 m_colorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent{};

    VkImage        m_depthImage      = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory     = VK_NULL_HANDLE;
    VkImageView    m_depthView       = VK_NULL_HANDLE;

    VkRenderPass                m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  m_framebuffers;
};

} // namespace chisel::render
