#pragma once
#include <vk_mem_alloc.h>

namespace chisel::render {

// ---------------------------------------------------------------------------
// VmaAllocator — thin RAII wrapper around the VMA allocator handle.
// ---------------------------------------------------------------------------
class VmaWrapper {
public:
    void init(VkInstance instance, VkPhysicalDevice physDevice, VkDevice device);
    void destroy();

    VmaAllocator handle() const { return m_allocator; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace chisel::render
