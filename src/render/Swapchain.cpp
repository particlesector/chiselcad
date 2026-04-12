#include "Swapchain.h"
#include "VulkanContext.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace chisel::render {

#define VK_CHECK(expr) do { \
    VkResult r_ = (expr); \
    if (r_ != VK_SUCCESS) \
        throw std::runtime_error("Vulkan error in Swapchain"); \
} while(0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return formats[0];
}

static VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR)   return m;
    for (auto m : modes) if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h) {
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    VkExtent2D e{};
    e.width  = std::clamp(w, caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t mask, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(pd, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable memory type");
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void Swapchain::init(const VulkanContext& ctx, uint32_t width, uint32_t height) {
    createSwapchain(ctx, width, height);
    createImageViews(ctx.device());
    createDepthResources(ctx);
    createRenderPass(ctx.device());
    createFramebuffers(ctx.device());
}

void Swapchain::recreate(const VulkanContext& ctx, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(ctx.device());
    destroyResources(ctx.device());
    createSwapchain(ctx, width, height);
    createImageViews(ctx.device());
    createDepthResources(ctx);
    createFramebuffers(ctx.device());
}

void Swapchain::destroy(VkDevice device) {
    destroyResources(device);
    if (m_renderPass) { vkDestroyRenderPass(device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
    if (m_swapchain)  { vkDestroySwapchainKHR(device, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------
void Swapchain::createSwapchain(const VulkanContext& ctx, uint32_t width, uint32_t height) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physDevice(), ctx.surface(), &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physDevice(), ctx.surface(), &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx.physDevice(), ctx.surface(), &fmtCount, formats.data());

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physDevice(), ctx.surface(), &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx.physDevice(), ctx.surface(), &modeCount, modes.data());

    auto fmt      = chooseSurfaceFormat(formats);
    auto mode     = choosePresentMode(modes);
    m_extent      = chooseExtent(caps, width, height);
    m_colorFormat = fmt.format;

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imgCount = std::min(imgCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = ctx.surface();
    ci.minImageCount    = imgCount;
    ci.imageFormat      = fmt.format;
    ci.imageColorSpace  = fmt.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = mode;
    ci.clipped          = VK_TRUE;

    uint32_t families[] = {ctx.graphicsFamily(), ctx.presentFamily()};
    if (ctx.graphicsFamily() != ctx.presentFamily()) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &ci, nullptr, &m_swapchain));

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(ctx.device(), m_swapchain, &count, m_images.data());
}

void Swapchain::createImageViews(VkDevice device) {
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = m_images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format   = m_colorFormat;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &ci, nullptr, &m_imageViews[i]));
    }
}

void Swapchain::createDepthResources(const VulkanContext& ctx) {
    constexpr VkFormat kDepth = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo ici{};
    ici.sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.format      = kDepth;
    ici.extent      = {m_extent.width, m_extent.height, 1};
    ici.mipLevels   = 1;
    ici.arrayLayers = 1;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VK_CHECK(vkCreateImage(ctx.device(), &ici, nullptr, &m_depthImage));

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(ctx.device(), m_depthImage, &req);

    VkMemoryAllocateInfo mai{};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = findMemoryType(ctx.physDevice(), req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx.device(), &mai, nullptr, &m_depthMemory));
    vkBindImageMemory(ctx.device(), m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image    = m_depthImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = kDepth;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(ctx.device(), &vci, nullptr, &m_depthView));
}

void Swapchain::createRenderPass(VkDevice device) {
    std::array<VkAttachmentDescription, 2> atts{};
    atts[0].format         = m_colorFormat;
    atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    atts[1].format         = VK_FORMAT_D32_SFLOAT;
    atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(atts.size());
    ci.pAttachments    = atts.data();
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;
    VK_CHECK(vkCreateRenderPass(device, &ci, nullptr, &m_renderPass));
}

void Swapchain::createFramebuffers(VkDevice device) {
    m_framebuffers.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        std::array<VkImageView, 2> views = {m_imageViews[i], m_depthView};
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_renderPass;
        ci.attachmentCount = static_cast<uint32_t>(views.size());
        ci.pAttachments    = views.data();
        ci.width           = m_extent.width;
        ci.height          = m_extent.height;
        ci.layers          = 1;
        VK_CHECK(vkCreateFramebuffer(device, &ci, nullptr, &m_framebuffers[i]));
    }
}

void Swapchain::destroyResources(VkDevice device) {
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();
    if (m_depthView)   { vkDestroyImageView(device, m_depthView,   nullptr); m_depthView   = VK_NULL_HANDLE; }
    if (m_depthMemory) { vkFreeMemory(device, m_depthMemory,        nullptr); m_depthMemory = VK_NULL_HANDLE; }
    if (m_depthImage)  { vkDestroyImage(device, m_depthImage,       nullptr); m_depthImage  = VK_NULL_HANDLE; }
    for (auto v : m_imageViews) vkDestroyImageView(device, v, nullptr);
    m_imageViews.clear();
}

} // namespace chisel::render
