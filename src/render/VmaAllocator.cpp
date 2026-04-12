// VMA implementation — must appear in exactly one translation unit
#define VMA_IMPLEMENTATION
#include "VmaAllocator.h"
#include <stdexcept>

namespace chisel::render {

void VmaWrapper::init(VkInstance instance, VkPhysicalDevice physDevice, VkDevice device) {
    VmaAllocatorCreateInfo ci{};
    ci.instance         = instance;
    ci.physicalDevice   = physDevice;
    ci.device           = device;
    ci.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&ci, &m_allocator) != VK_SUCCESS)
        throw std::runtime_error("Failed to create VMA allocator");
}

void VmaWrapper::destroy() {
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

} // namespace chisel::render
