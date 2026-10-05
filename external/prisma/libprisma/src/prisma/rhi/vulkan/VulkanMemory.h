#pragma once

#include "prisma/rhi/vulkan/FreeRanges.h"

#include <ct/vector.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>

namespace prisma
{

struct VulkanAllocation
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    std::uint32_t block = 0;

    bool valid() const { return memory != VK_NULL_HANDLE; }
};

enum class VulkanMemoryKind : std::uint8_t
{
    Buffer,
    Image
};

class VulkanMemory
{
public:
    void init(VkPhysicalDevice physicalDevice, VkDevice device);
    void shutdown();

    bool allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
            VulkanMemoryKind kind, VulkanAllocation* allocation);
    void free(const VulkanAllocation& allocation);

    std::uint32_t blockCount() const { return static_cast<std::uint32_t>(blocks_.size()); }

private:
    struct Block
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        void* mapped;
        std::uint32_t id;
        std::uint32_t type;
        VulkanMemoryKind kind;
        bool dedicated;
    };

    std::uint32_t findType(std::uint32_t typeBits, VkMemoryPropertyFlags wanted) const;
    bool createBlock(VkDeviceSize size, std::uint32_t type, VulkanMemoryKind kind, bool dedicated);
    void destroyBlock(std::size_t index);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties properties_ = {};
    ct::Vector<Block> blocks_;
    FreeRanges ranges_;
    std::uint32_t nextId_ = 1;
};

} // namespace prisma
