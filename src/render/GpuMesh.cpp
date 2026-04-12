#include "GpuMesh.h"
#include <cstring>
#include <stdexcept>

namespace chisel::render {

#define VK_CHECK(expr) do { \
    VkResult r_ = (expr); \
    if (r_ != VK_SUCCESS) \
        throw std::runtime_error("Vulkan error in GpuMesh"); \
} while(0)

void GpuMesh::upload(VmaAllocator allocator,
                     VkDevice device,
                     VkCommandPool cmdPool,
                     VkQueue graphicsQueue,
                     const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices)
{
    destroy(allocator);
    if (vertices.empty() || indices.empty()) return;

    m_indexCount = static_cast<uint32_t>(indices.size());

    // --- Vertex buffer ---
    VkDeviceSize vbSize = vertices.size() * sizeof(Vertex);
    VkBuffer stagingVB = VK_NULL_HANDLE;
    VmaAllocation stagingVBAlloc = VK_NULL_HANDLE;
    createBuffer(allocator, vbSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VMA_MEMORY_USAGE_CPU_ONLY,
                 stagingVB, stagingVBAlloc);

    void* data = nullptr;
    vmaMapMemory(allocator, stagingVBAlloc, &data);
    std::memcpy(data, vertices.data(), static_cast<size_t>(vbSize));
    vmaUnmapMemory(allocator, stagingVBAlloc);

    createBuffer(allocator, vbSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VMA_MEMORY_USAGE_GPU_ONLY,
                 m_vertexBuffer, m_vertexAllocation);
    copyBuffer(device, cmdPool, graphicsQueue, stagingVB, m_vertexBuffer, vbSize);
    vmaDestroyBuffer(allocator, stagingVB, stagingVBAlloc);

    // --- Index buffer ---
    VkDeviceSize ibSize = indices.size() * sizeof(uint32_t);
    VkBuffer stagingIB = VK_NULL_HANDLE;
    VmaAllocation stagingIBAlloc = VK_NULL_HANDLE;
    createBuffer(allocator, ibSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VMA_MEMORY_USAGE_CPU_ONLY,
                 stagingIB, stagingIBAlloc);

    vmaMapMemory(allocator, stagingIBAlloc, &data);
    std::memcpy(data, indices.data(), static_cast<size_t>(ibSize));
    vmaUnmapMemory(allocator, stagingIBAlloc);

    createBuffer(allocator, ibSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 VMA_MEMORY_USAGE_GPU_ONLY,
                 m_indexBuffer, m_indexAllocation);
    copyBuffer(device, cmdPool, graphicsQueue, stagingIB, m_indexBuffer, ibSize);
    vmaDestroyBuffer(allocator, stagingIB, stagingIBAlloc);
}

void GpuMesh::destroy(VmaAllocator allocator) {
    if (m_vertexBuffer)    { vmaDestroyBuffer(allocator, m_vertexBuffer, m_vertexAllocation); m_vertexBuffer = VK_NULL_HANDLE; }
    if (m_indexBuffer)     { vmaDestroyBuffer(allocator, m_indexBuffer,  m_indexAllocation);  m_indexBuffer  = VK_NULL_HANDLE; }
    m_indexCount = 0;
}

void GpuMesh::createBuffer(VmaAllocator allocator,
                            VkDeviceSize size,
                            VkBufferUsageFlags usage,
                            VmaMemoryUsage memUsage,
                            VkBuffer& buffer,
                            VmaAllocation& allocation)
{
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size  = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = memUsage;

    if (vmaCreateBuffer(allocator, &bci, &aci, &buffer, &allocation, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");
}

void GpuMesh::copyBuffer(VkDevice device, VkCommandPool pool, VkQueue queue,
                          VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = pool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

} // namespace chisel::render
