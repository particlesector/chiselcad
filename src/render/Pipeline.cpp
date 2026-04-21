#include "Pipeline.h"
#include "GpuMesh.h"
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace chisel::render {

#define VK_CHECK(expr) do { \
    VkResult r_ = (expr); \
    if (r_ != VK_SUCCESS) \
        throw std::runtime_error("Vulkan error in Pipeline"); \
} while(0)

// ---------------------------------------------------------------------------
// Shader loading
// ---------------------------------------------------------------------------
VkShaderModule Pipeline::loadShader(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        throw std::runtime_error("Cannot open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<char> code(size);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

// ---------------------------------------------------------------------------
// buildPipeline — shared helper for solid and wireframe variants
// ---------------------------------------------------------------------------
VkPipeline Pipeline::buildPipeline(VkDevice device, VkRenderPass renderPass,
                                    VkShaderModule vertMod, VkShaderModule fragMod,
                                    VkPolygonMode polyMode, VkCullModeFlags cullMode,
                                    bool depthBias)
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attribs{};
    attribs[0].location = 0;
    attribs[0].binding  = 0;
    attribs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset   = offsetof(Vertex, pos);
    attribs[1].location = 1;
    attribs[1].binding  = 0;
    attribs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[1].offset   = offsetof(Vertex, normal);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribs.size());
    vertexInput.pVertexAttributeDescriptions    = attribs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode      = polyMode;
    raster.cullMode         = cullMode;
    raster.frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth        = 1.0f;
    raster.depthBiasEnable  = depthBias ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable  = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL; // LESS_OR_EQUAL for overlay pass

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    // Dynamic states: always viewport + scissor; add depth bias for wire pipeline
    std::vector<VkDynamicState> dynStates = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
    };
    if (depthBias)
        dynStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);

    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamic.pDynamicStates    = dynStates.data();

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = static_cast<uint32_t>(stages.size());
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vertexInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState      = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &msaa;
    pci.pDepthStencilState  = &depth;
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dynamic;
    pci.layout              = m_layout;
    pci.renderPass          = renderPass;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe));
    return pipe;
}

// ---------------------------------------------------------------------------
// buildBackgroundPipeline — fullscreen gradient, no vertex input, no depth
// ---------------------------------------------------------------------------
VkPipeline Pipeline::buildBackgroundPipeline(VkDevice device, VkRenderPass renderPass,
                                              VkShaderModule vertMod, VkShaderModule fragMod)
{
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode    = VK_CULL_MODE_NONE;
    raster.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth test or write — background always sits behind the mesh
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType           = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable  = VK_FALSE;
    depth.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments    = &blendAtt;

    std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamic.pDynamicStates    = dynStates.data();

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = static_cast<uint32_t>(stages.size());
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vertexInput;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState      = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState   = &msaa;
    pci.pDepthStencilState  = &depth;
    pci.pColorBlendState    = &blend;
    pci.pDynamicState       = &dynamic;
    pci.layout              = m_bgLayout;
    pci.renderPass          = renderPass;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipe));
    return pipe;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void Pipeline::init(VkDevice device, VkRenderPass renderPass,
                    const std::string& shaderDir, bool wireSupported)
{
    // Push constants: vert=[mvp(64)+model(64)], frag=[eyePos vec4(16)] => 144 bytes
    std::array<VkPushConstantRange, 2> pcRanges{};
    pcRanges[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRanges[0].offset     = 0;
    pcRanges[0].size       = 128;
    pcRanges[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRanges[1].offset     = 128;
    pcRanges[1].size       = 16;

    VkPipelineLayoutCreateInfo layoutCI{};
    layoutCI.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCI.pushConstantRangeCount = static_cast<uint32_t>(pcRanges.size());
    layoutCI.pPushConstantRanges    = pcRanges.data();
    VK_CHECK(vkCreatePipelineLayout(device, &layoutCI, nullptr, &m_layout));

    auto vertMod = loadShader(device, shaderDir + "mesh.vert.spv");
    auto fragMod = loadShader(device, shaderDir + "mesh.frag.spv");

    // Solid pipeline: filled triangles, back-face culling, no depth bias
    m_solidPipeline = buildPipeline(device, renderPass, vertMod, fragMod,
                                    VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT,
                                    /*depthBias=*/false);

    // Wireframe pipeline: line edges, no culling, depth bias for overlay pass
    if (wireSupported) {
        m_wirePipeline = buildPipeline(device, renderPass, vertMod, fragMod,
                                       VK_POLYGON_MODE_LINE, VK_CULL_MODE_NONE,
                                       /*depthBias=*/true);
    }

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);

    // Background pipeline — empty layout, no push constants
    VkPipelineLayoutCreateInfo bgLayoutCI{};
    bgLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VK_CHECK(vkCreatePipelineLayout(device, &bgLayoutCI, nullptr, &m_bgLayout));

    auto bgVert = loadShader(device, shaderDir + "background.vert.spv");
    auto bgFrag = loadShader(device, shaderDir + "background.frag.spv");
    m_bgPipeline = buildBackgroundPipeline(device, renderPass, bgVert, bgFrag);
    vkDestroyShaderModule(device, bgVert, nullptr);
    vkDestroyShaderModule(device, bgFrag, nullptr);
}

void Pipeline::destroy(VkDevice device) {
    if (m_bgPipeline)    { vkDestroyPipeline(device, m_bgPipeline,    nullptr); m_bgPipeline    = VK_NULL_HANDLE; }
    if (m_wirePipeline)  { vkDestroyPipeline(device, m_wirePipeline,  nullptr); m_wirePipeline  = VK_NULL_HANDLE; }
    if (m_solidPipeline) { vkDestroyPipeline(device, m_solidPipeline, nullptr); m_solidPipeline = VK_NULL_HANDLE; }
    if (m_bgLayout)      { vkDestroyPipelineLayout(device, m_bgLayout, nullptr); m_bgLayout      = VK_NULL_HANDLE; }
    if (m_layout)        { vkDestroyPipelineLayout(device, m_layout,   nullptr); m_layout        = VK_NULL_HANDLE; }
}

} // namespace chisel::render
