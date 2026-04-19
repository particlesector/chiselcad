#pragma once
#include "Camera.h"
#include "GpuMesh.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "VmaAllocator.h"
#include "VulkanContext.h"
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>

namespace chisel::render {

static constexpr int kMaxFrames = 2;

// ---------------------------------------------------------------------------
// Renderer — owns per-frame sync objects and command buffers, drives the
// acquire → record → submit → present loop.
// ---------------------------------------------------------------------------
class Renderer {
public:
    void init(const VulkanContext& ctx, const Swapchain& swapchain);
    void destroy(VkDevice device);

    // Upload a new mesh (called when .scad is re-evaluated)
    void uploadMesh(const VulkanContext& ctx, VmaWrapper& vma,
                    const std::vector<Vertex>& verts,
                    const std::vector<uint32_t>& indices);

    // Draw one frame; returns false if swapchain needs recreation
    bool drawFrame(const VulkanContext& ctx,
                   Swapchain& swapchain,
                   const Pipeline& pipeline,
                   Camera& camera,
                   uint32_t winWidth, uint32_t winHeight,
                   RenderMode mode = RenderMode::Solid);

private:
    void recordCommands(VkCommandBuffer cmd,
                        const Swapchain& swapchain,
                        const Pipeline& pipeline,
                        uint32_t imageIndex,
                        const glm::mat4& mvp,
                        const glm::vec3& eyePos,
                        RenderMode mode);

    struct FrameData {
        VkCommandBuffer cmd             = VK_NULL_HANDLE;
        VkSemaphore     imageAvailable  = VK_NULL_HANDLE;
        VkSemaphore     renderFinished  = VK_NULL_HANDLE;
        VkFence         inFlight        = VK_NULL_HANDLE;
    };

    std::array<FrameData, kMaxFrames> m_frames{};
    GpuMesh  m_mesh;
    uint32_t m_currentFrame = 0;
};

} // namespace chisel::render
