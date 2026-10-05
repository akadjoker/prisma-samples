#include "prisma/rhi/vulkan/VulkanMemory.h"

namespace prisma
{

namespace
{

const VkDeviceSize kBlockSize = 64u * 1024u * 1024u;
const VkDeviceSize kSmallestBlock = 4u * 1024u * 1024u;

} // namespace

void VulkanMemory::init(VkPhysicalDevice physicalDevice, VkDevice device)
{
    device_ = device;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties_);
}

void VulkanMemory::shutdown()
{
    while (!blocks_.empty()) destroyBlock(blocks_.size() - 1);
    device_ = VK_NULL_HANDLE;
}

std::uint32_t VulkanMemory::findType(std::uint32_t typeBits, VkMemoryPropertyFlags wanted) const
{
    for (std::uint32_t i = 0; i < properties_.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (properties_.memoryTypes[i].propertyFlags & wanted) == wanted)
            return i;
    return UINT32_MAX;
}

bool VulkanMemory::createBlock(VkDeviceSize size, std::uint32_t type, VulkanMemoryKind kind,
        bool dedicated)
{
    Block block;
    block.memory = VK_NULL_HANDLE;
    block.size = size;
    block.mapped = nullptr;
    block.id = nextId_++;
    block.type = type;
    block.kind = kind;
    block.dedicated = dedicated;

    VkMemoryAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = size;
    info.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &info, nullptr, &block.memory) != VK_SUCCESS) return false;

    const bool visible = (properties_.memoryTypes[type].propertyFlags &
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    if (visible &&
            vkMapMemory(device_, block.memory, 0, VK_WHOLE_SIZE, 0, &block.mapped) != VK_SUCCESS)
    {
        vkFreeMemory(device_, block.memory, nullptr);
        return false;
    }
    blocks_.push_back(block);
    ranges_.add(block.id, size);
    return true;
}

void VulkanMemory::destroyBlock(std::size_t index)
{
    const Block block = blocks_[index];
    if (block.mapped) vkUnmapMemory(device_, block.memory);
    vkFreeMemory(device_, block.memory, nullptr);
    ranges_.remove(block.id);
    blocks_[index] = blocks_[blocks_.size() - 1];
    blocks_.pop_back();
}

bool VulkanMemory::allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted,
        VulkanMemoryKind kind, VulkanAllocation* allocation)
{
    const std::uint32_t type = findType(requirements.memoryTypeBits, wanted);
    if (type == UINT32_MAX) return false;

    const bool dedicated = requirements.size > kBlockSize / 2;
    std::size_t index = blocks_.size();
    std::uint64_t offset = 0;
    for (std::size_t i = 0; !dedicated && i < blocks_.size() && index == blocks_.size(); ++i)
    {
        const Block& block = blocks_[i];
        if (block.dedicated || block.type != type || block.kind != kind) continue;
        if (ranges_.take(block.id, requirements.size, requirements.alignment, &offset)) index = i;
    }
    if (index == blocks_.size())
    {
        const VkDeviceSize heap =
                properties_.memoryHeaps[properties_.memoryTypes[type].heapIndex].size;
        VkDeviceSize size = dedicated ? requirements.size : kBlockSize;
        while (!dedicated && size > kSmallestBlock && size > heap / 8) size /= 2;
        if (size < requirements.size) size = requirements.size;
        bool created = createBlock(size, type, kind, dedicated);
        while (!created && !dedicated && size / 2 >= requirements.size)
        {
            size /= 2;
            created = createBlock(size, type, kind, dedicated);
        }
        if (!created) return false;
        index = blocks_.size() - 1;
        if (!ranges_.take(blocks_[index].id, requirements.size, requirements.alignment, &offset))
            return false;
    }

    const Block& block = blocks_[index];
    allocation->memory = block.memory;
    allocation->offset = offset;
    allocation->size = requirements.size;
    allocation->mapped = block.mapped ? static_cast<char*>(block.mapped) + offset : nullptr;
    allocation->block = block.id;
    return true;
}

void VulkanMemory::free(const VulkanAllocation& allocation)
{
    if (!allocation.valid()) return;
    std::size_t index = blocks_.size();
    for (std::size_t i = 0; i < blocks_.size(); ++i)
        if (blocks_[i].id == allocation.block) index = i;
    if (index == blocks_.size()) return;

    const Block block = blocks_[index];
    ranges_.give(block.id, allocation.offset, allocation.size);
    if (!ranges_.whole(block.id, block.size)) return;
    if (block.dedicated)
    {
        destroyBlock(index);
        return;
    }
    for (std::size_t i = 0; i < blocks_.size(); ++i)
    {
        const Block& other = blocks_[i];
        if (i == index || other.dedicated || other.type != block.type || other.kind != block.kind)
            continue;
        destroyBlock(index);
        return;
    }
}

} // namespace prisma
