#pragma once
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace chisel::render {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
};

// ---------------------------------------------------------------------------
// GpuMesh — vertex + index buffers allocated via VMA.
// Upload flat-shaded geometry from Manifold's MeshGL triangle soup.
// ---------------------------------------------------------------------------
class GpuMesh {
public:
    void upload(VmaAllocator allocator,
                VkDevice device,
                VkCommandPool cmdPool,
                VkQueue graphicsQueue,
                const std::vector<Vertex>& vertices,
                const std::vector<uint32_t>& indices);

    void destroy(VmaAllocator allocator);

    VkBuffer     vertexBuffer() const { return m_vertexBuffer; }
    VkBuffer     indexBuffer()  const { return m_indexBuffer; }
    uint32_t     indexCount()   const { return m_indexCount; }
    bool         valid()        const { return m_indexCount > 0; }

private:
    void createBuffer(VmaAllocator allocator,
                      VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VmaMemoryUsage memUsage,
                      VkBuffer& buffer,
                      VmaAllocation& allocation);

    void copyBuffer(VkDevice device, VkCommandPool pool, VkQueue queue,
                    VkBuffer src, VkBuffer dst, VkDeviceSize size);

    VkBuffer      m_vertexBuffer     = VK_NULL_HANDLE;
    VmaAllocation m_vertexAllocation = VK_NULL_HANDLE;
    VkBuffer      m_indexBuffer      = VK_NULL_HANDLE;
    VmaAllocation m_indexAllocation  = VK_NULL_HANDLE;
    uint32_t      m_indexCount       = 0;
};

} // namespace chisel::render
