#include "Renderer.h"
#include <array>
#include <stdexcept>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace chisel::render {

#define VK_CHECK(expr) do { \
    VkResult r_ = (expr); \
    if (r_ != VK_SUCCESS) \
        throw std::runtime_error("Vulkan error in Renderer"); \
} while(0)

// ---------------------------------------------------------------------------
// init / destroy
// ---------------------------------------------------------------------------
void Renderer::init(const VulkanContext& ctx, const Swapchain& swapchain) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = ctx.commandPool();
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kMaxFrames;

    VkCommandBuffer cmds[kMaxFrames]{};
    VK_CHECK(vkAllocateCommandBuffers(ctx.device(), &ai, cmds));

    for (int i = 0; i < kMaxFrames; ++i) {
        m_frames[i].cmd = cmds[i];

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(ctx.device(), &sci, nullptr, &m_frames[i].imageAvailable));
        VK_CHECK(vkCreateSemaphore(ctx.device(), &sci, nullptr, &m_frames[i].renderFinished));

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(ctx.device(), &fci, nullptr, &m_frames[i].inFlight));
    }

    (void)swapchain;
}

void Renderer::destroy(VkDevice device) {
    for (auto& f : m_frames) {
        if (f.inFlight)       vkDestroyFence(device, f.inFlight, nullptr);
        if (f.renderFinished) vkDestroySemaphore(device, f.renderFinished, nullptr);
        if (f.imageAvailable) vkDestroySemaphore(device, f.imageAvailable, nullptr);
    }
}

// ---------------------------------------------------------------------------
// Mesh upload
// ---------------------------------------------------------------------------
void Renderer::uploadMesh(const VulkanContext& ctx, VmaWrapper& vma,
                           const std::vector<Vertex>& verts,
                           const std::vector<uint32_t>& indices)
{
    vkDeviceWaitIdle(ctx.device());
    m_mesh.upload(vma.handle(), ctx.device(), ctx.commandPool(),
                  ctx.graphicsQueue(), verts, indices);
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------
bool Renderer::drawFrame(const VulkanContext& ctx,
                          Swapchain& swapchain,
                          const Pipeline& pipeline,
                          Camera& camera,
                          uint32_t winWidth, uint32_t winHeight,
                          RenderMode mode)
{
    auto& frame = m_frames[m_currentFrame];
    VK_CHECK(vkWaitForFences(ctx.device(), 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acquireResult = vkAcquireNextImageKHR(
        ctx.device(), swapchain.swapchain(), UINT64_MAX,
        frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    VK_CHECK(vkResetFences(ctx.device(), 1, &frame.inFlight));
    VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));

    float aspect = (winHeight > 0) ? static_cast<float>(winWidth) / static_cast<float>(winHeight) : 1.0f;
    glm::mat4 mvp    = camera.viewProjection(aspect);
    glm::vec3 eyePos = camera.eye();

    recordCommands(frame.cmd, swapchain, pipeline, imageIndex, mvp, eyePos, mode);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &frame.imageAvailable;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &frame.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &frame.renderFinished;
    VK_CHECK(vkQueueSubmit(ctx.graphicsQueue(), 1, &si, frame.inFlight));

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &frame.renderFinished;
    VkSwapchainKHR sc     = swapchain.swapchain();
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc;
    pi.pImageIndices      = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(ctx.presentQueue(), &pi);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        return false;
    if (presentResult != VK_SUCCESS)
        throw std::runtime_error("Failed to present");

    m_currentFrame = (m_currentFrame + 1) % kMaxFrames;
    return true;
}

// ---------------------------------------------------------------------------
// Command recording
// ---------------------------------------------------------------------------
void Renderer::recordCommands(VkCommandBuffer cmd,
                               const Swapchain& swapchain,
                               const Pipeline& pipeline,
                               uint32_t imageIndex,
                               const glm::mat4& mvp,
                               const glm::vec3& eyePos,
                               RenderMode mode)
{
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{0.0f, 0.0f, 0.0f, 1.0f}}; // overwritten by bg pass
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = swapchain.renderPass();
    rpbi.framebuffer       = swapchain.framebuffer(imageIndex);
    rpbi.renderArea.extent = swapchain.extent();
    rpbi.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    rpbi.pClearValues      = clearValues.data();
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width    = static_cast<float>(swapchain.extent().width);
    vp.height   = static_cast<float>(swapchain.extent().height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{{0, 0}, swapchain.extent()};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Background gradient — fullscreen triangle, no vertex input, no depth test
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.bgPipeline());
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (m_mesh.valid()) {
        // Push constants shared by both passes
        glm::mat4 model(1.0f);
        glm::vec4 eyePad(eyePos, 0.0f);

        auto pushAll = [&]() {
            vkCmdPushConstants(cmd, pipeline.pipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &mvp);
            vkCmdPushConstants(cmd, pipeline.pipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 64, 64, &model);
            vkCmdPushConstants(cmd, pipeline.pipelineLayout(),
                               VK_SHADER_STAGE_FRAGMENT_BIT, 128, 16, &eyePad);
        };

        VkBuffer     vb     = m_mesh.vertexBuffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, m_mesh.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        if (mode == RenderMode::Wireframe && pipeline.wireSupported()) {
            // Wireframe-only: line edges, no depth bias needed (single pass)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.wirePipeline());
            vkCmdSetDepthBias(cmd, 0.0f, 0.0f, 0.0f);
            pushAll();
            vkCmdDrawIndexed(cmd, m_mesh.indexCount(), 1, 0, 0, 0);
        } else {
            // Solid pass (used for both Solid and SolidEdges)
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.solidPipeline());
            pushAll();
            vkCmdDrawIndexed(cmd, m_mesh.indexCount(), 1, 0, 0, 0);

            // Wireframe overlay for SolidEdges
            if (mode == RenderMode::SolidEdges && pipeline.wireSupported()) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.wirePipeline());
                // Negative bias nudges edges toward the camera to avoid z-fighting
                vkCmdSetDepthBias(cmd, -2.0f, 0.0f, -1.0f);
                pushAll();
                vkCmdDrawIndexed(cmd, m_mesh.indexCount(), 1, 0, 0, 0);
            }
        }
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
}

} // namespace chisel::render
