#pragma once
#include <vulkan/vulkan.h>
#include <string>

namespace chisel::render {

enum class RenderMode { Solid, Wireframe, SolidEdges };

// ---------------------------------------------------------------------------
// Pipeline — loads compiled SPIR-V shaders and creates the mesh graphics
// pipelines.  Two mesh variants: solid (fill) and wireframe (line).
// One background pipeline: fullscreen gradient, no vertex input, no depth.
//
// Push constants: mat4 mvp + mat4 model (128 bytes, vertex stage)
//                 vec4 eyePos (16 bytes at offset 128, fragment stage)
// ---------------------------------------------------------------------------
class Pipeline {
public:
    void init(VkDevice device, VkRenderPass renderPass,
              const std::string& shaderDir, bool wireSupported = true);
    void destroy(VkDevice device);

    VkPipeline       solidPipeline()  const { return m_solidPipeline; }
    VkPipeline       wirePipeline()   const { return m_wirePipeline; }
    VkPipeline       bgPipeline()     const { return m_bgPipeline; }
    VkPipelineLayout pipelineLayout() const { return m_layout; }
    bool             wireSupported()  const { return m_wirePipeline != VK_NULL_HANDLE; }

    VkPipeline pipeline() const { return m_solidPipeline; }

private:
    VkShaderModule loadShader(VkDevice device, const std::string& path);

    VkPipeline buildPipeline(VkDevice device, VkRenderPass renderPass,
                             VkShaderModule vertMod, VkShaderModule fragMod,
                             VkPolygonMode polyMode, VkCullModeFlags cullMode,
                             bool depthBias);

    VkPipeline buildBackgroundPipeline(VkDevice device, VkRenderPass renderPass,
                                        VkShaderModule vertMod, VkShaderModule fragMod);

    VkPipeline       m_solidPipeline = VK_NULL_HANDLE;
    VkPipeline       m_wirePipeline  = VK_NULL_HANDLE;
    VkPipeline       m_bgPipeline    = VK_NULL_HANDLE;
    VkPipelineLayout m_layout        = VK_NULL_HANDLE;
    VkPipelineLayout m_bgLayout      = VK_NULL_HANDLE;
};

} // namespace chisel::render
