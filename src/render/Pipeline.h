#pragma once
#include <vulkan/vulkan.h>
#include <string>

namespace chisel::render {

// ---------------------------------------------------------------------------
// Pipeline — loads compiled SPIR-V shaders and creates the mesh graphics
// pipeline.  Push constants: mat4 mvp + mat4 model (128 bytes).
// ---------------------------------------------------------------------------
class Pipeline {
public:
    void init(VkDevice device, VkRenderPass renderPass, const std::string& shaderDir);
    void destroy(VkDevice device);

    VkPipeline       pipeline()       const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_layout; }

private:
    VkShaderModule loadShader(VkDevice device, const std::string& path);

    VkPipeline       m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

} // namespace chisel::render
