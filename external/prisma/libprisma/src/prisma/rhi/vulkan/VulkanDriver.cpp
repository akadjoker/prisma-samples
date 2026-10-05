#include "prisma/rhi/Driver.h"
#include "prisma/rhi/Format.h"
#include "prisma/rhi/HandleCast.h"
#include "prisma/rhi/vulkan/VulkanMemory.h"

#include <ct/vector.hpp>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <string.h>

namespace prisma
{

namespace
{

const std::uint32_t kFramesInFlight = 2;
const std::uint64_t kStagingBudget = 64u * 1024u * 1024u;
const char* const kValidationLayer = "VK_LAYER_KHRONOS_validation";

struct Frame
{
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
};

struct SwapchainImage
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

const std::uint64_t kNeverUsed = UINT64_MAX;

struct BufferVersion
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocation allocation;
    void* mapped = nullptr;
    std::uint64_t lastUsedFrame = kNeverUsed;
};

struct VulkanBuffer
{
    enum : std::uint32_t
    {
        kMaxVersions = 8
    };

    BufferVersion versions[kMaxVersions];
    std::uint32_t versionCount = 0;
    std::uint32_t current = 0;
    std::uint32_t size = 0;
    VkBufferUsageFlags vkUsage = 0;
    bool gpuWritten = false;
    BufferUsage usage = BufferUsage::Vertex;
    IndexFormat indexFormat = IndexFormat::UInt16;
    ct::Vector<unsigned char> shadow;
};

struct VulkanShader
{
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
    std::uint32_t slotCount[4] = {};
    std::uint32_t slots[4][PipelineDesc::kMaxTextures] = {};
};

struct VulkanPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    TargetFormats targets;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    std::uint32_t vertexBufferCount = 0;
    VertexBufferLayout vertexBuffers[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t uniformCount = 0;
    std::uint32_t uniformSlots[PipelineDesc::kMaxUniformBlocks] = {};
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    std::uint32_t textureCount = 0;
    std::uint32_t textureSlots[PipelineDesc::kMaxTextures] = {};
    bool compute = false;
    VkDescriptorSetLayout storageSetLayout = VK_NULL_HANDLE;
    std::uint32_t storageCount = 0;
    std::uint32_t storageSlots[PipelineDesc::kMaxStorageBuffers] = {};
    VkDescriptorSetLayout imageSetLayout = VK_NULL_HANDLE;
    std::uint32_t imageCount = 0;
    std::uint32_t imageSlots[ComputePipelineDesc::kMaxStorageTextures] = {};
};

struct ReadSource
{
    VkImage image = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    std::int32_t slice = 0;
    bool fromTexture = false;
    bool swapRedBlue = false;
};

struct VulkanReadback
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocation allocation;
    void* mapped = nullptr;
    std::uint64_t frame = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes = 0;
    bool flip = false;
    bool swapRedBlue = false;
};

struct VulkanWindow
{
    VulkanPlatform platform;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent = { 0, 0 };
    ct::Vector<SwapchainImage> images;
    std::uint32_t imageIndex = 0;
    std::uint32_t requestedWidth = 0;
    std::uint32_t requestedHeight = 0;
    bool canRead = false;
    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VulkanAllocation depthAllocation;
    VkSemaphore imageAvailable[kFramesInFlight] = {};
    bool acquired = false;
    bool waited = false;
};

const std::uint32_t kMaxWindows = 8;

struct ImageSlot
{
    TextureHandle texture;
    std::uint32_t mip = 0;
};

struct UniformBinding
{
    BufferHandle handle;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

struct AttachmentView
{
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    VkImageView view = VK_NULL_HANDLE;
};

struct VulkanTexture
{
    enum : std::uint32_t
    {
        kMaxMipViews = 16
    };

    VkImage image = VK_NULL_HANDLE;
    VulkanAllocation allocation;
    VkImageView view = VK_NULL_HANDLE;
    TextureType type = TextureType::Texture2D;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t samples = 1;
    bool copyDestination = false;
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t usage = 0;
    ct::Vector<AttachmentView> attachmentViews;
    VkImageView mipViews[kMaxMipViews] = {};
};

struct PassTarget
{
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = 0;
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VulkanSampler
{
    VkSampler sampler = VK_NULL_HANDLE;
};

struct TextureSlot
{
    TextureHandle texture;
    SamplerHandle sampler;
};

const std::uint32_t kQuerySlots = 4;
const std::uint32_t kPoolQueries = 4096;

struct VulkanQuery
{
    QueryType type = QueryType::Occlusion;
    std::uint32_t slots[kQuerySlots] = {};
    std::uint64_t sequence[kQuerySlots] = {};
    std::uint32_t next = 0;
    std::int32_t active = -1;
};

struct Garbage
{
    std::uint64_t frame = 0;
    std::uint32_t querySlot = 0;
    std::uint8_t queryKind = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocation allocation;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout storageSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout imageSetLayout = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
};

std::uint32_t mipSize(std::uint32_t size, std::uint32_t mip)
{
    const std::uint32_t reduced = size >> mip;
    return reduced > 0 ? reduced : 1;
}

std::uint32_t imageLayers(const VulkanTexture& texture)
{
    if (texture.type == TextureType::TextureCube) return 6;
    if (texture.type == TextureType::TextureCubeArray) return texture.depth * 6;
    if (texture.type == TextureType::Texture2DArray) return texture.depth;
    return 1;
}

std::uint32_t layerCount(const VulkanTexture& texture, std::uint32_t mip)
{
    if (texture.type == TextureType::Texture3D) return mipSize(texture.depth, mip);
    return imageLayers(texture);
}

VkImageAspectFlags aspectOf(TextureFormat format)
{
    if (format == TextureFormat::Depth32F) return VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == TextureFormat::Depth24Stencil8)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

VkPipelineStageFlags2 layoutStage(VkImageLayout layout)
{
    switch (layout)
    {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                   VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        default:
            return VK_PIPELINE_STAGE_2_NONE;
    }
}

VkAccessFlags2 layoutAccess(VkImageLayout layout)
{
    switch (layout)
    {
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_2_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        default:
            return 0;
    }
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t size = width > height ? width : height;
    std::uint32_t levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

VkSamplerAddressMode toVkAddress(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::MirroredRepeat:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

bool sameTargets(const TargetFormats& a, const TargetFormats& b)
{
    if (a.window != b.window) return false;
    if (a.window) return true;
    if (a.colorCount != b.colorCount || a.depth != b.depth || a.samples != b.samples) return false;
    for (std::uint32_t i = 0; i < a.colorCount; ++i)
        if (a.colors[i] != b.colors[i]) return false;
    return true;
}

bool isCube(TextureType type)
{
    return type == TextureType::TextureCube || type == TextureType::TextureCubeArray;
}

bool validSamples(const TextureDesc& desc, std::uint32_t maxSamples)
{
    if (desc.samples == 1) return true;
    return (desc.samples == 2 || desc.samples == 4 || desc.samples == 8) &&
           desc.samples <= maxSamples && desc.type == TextureType::Texture2D &&
           desc.mipLevels == 1 && desc.usage == kTextureRenderTarget && !desc.data;
}

VkSampleCountFlagBits toSampleCount(std::uint32_t samples)
{
    if (samples == 2) return VK_SAMPLE_COUNT_2_BIT;
    if (samples == 4) return VK_SAMPLE_COUNT_4_BIT;
    if (samples == 8) return VK_SAMPLE_COUNT_8_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

VkFormat toVkFormat(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::None:
            return VK_FORMAT_UNDEFINED;
        case TextureFormat::R8:
            return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8:
            return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8Srgb:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::RGB10A2:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case TextureFormat::RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R11G11B10F:
            return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
        case TextureFormat::R16F:
            return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::RG16F:
            return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::R32F:
            return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::RG32F:
            return VK_FORMAT_R32G32_SFLOAT;
        case TextureFormat::RGBA32F:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::R32UInt:
            return VK_FORMAT_R32_UINT;
        case TextureFormat::Depth32F:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::Depth24Stencil8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::BC1:
            return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC1Srgb:
            return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case TextureFormat::BC2:
            return VK_FORMAT_BC2_UNORM_BLOCK;
        case TextureFormat::BC2Srgb:
            return VK_FORMAT_BC2_SRGB_BLOCK;
        case TextureFormat::BC3:
            return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC3Srgb:
            return VK_FORMAT_BC3_SRGB_BLOCK;
        case TextureFormat::BC4:
            return VK_FORMAT_BC4_UNORM_BLOCK;
        case TextureFormat::BC5:
            return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC6H:
            return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case TextureFormat::BC7:
            return VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::BC7Srgb:
            return VK_FORMAT_BC7_SRGB_BLOCK;
        case TextureFormat::ETC2RGB8:
            return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case TextureFormat::ETC2RGB8Srgb:
            return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case TextureFormat::ETC2RGBA8:
            return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case TextureFormat::ETC2RGBA8Srgb:
            return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case TextureFormat::EACR11:
            return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case TextureFormat::EACRG11:
            return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        case TextureFormat::ASTC4x4:
            return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case TextureFormat::ASTC4x4Srgb:
            return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case TextureFormat::ASTC6x6:
            return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case TextureFormat::ASTC6x6Srgb:
            return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case TextureFormat::ASTC8x8:
            return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case TextureFormat::ASTC8x8Srgb:
            return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat toVkFormat(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float1:
            return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Half2:
            return VK_FORMAT_R16G16_SFLOAT;
        case VertexFormat::Half4:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case VertexFormat::UByte4Norm:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Byte4Norm:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case VertexFormat::UShort2Norm:
            return VK_FORMAT_R16G16_UNORM;
        case VertexFormat::UShort4Norm:
            return VK_FORMAT_R16G16B16A16_UNORM;
        case VertexFormat::Short2Norm:
            return VK_FORMAT_R16G16_SNORM;
        case VertexFormat::Short4Norm:
            return VK_FORMAT_R16G16B16A16_SNORM;
        case VertexFormat::Int1010102Norm:
            return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
        case VertexFormat::UByte4:
            return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UShort4:
            return VK_FORMAT_R16G16B16A16_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology toVkTopology(Topology topology)
{
    switch (topology)
    {
        case Topology::Triangles:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Topology::TriangleStrip:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Topology::Lines:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Topology::LineStrip:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Topology::Points:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Topology::LinesAdjacency:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case Topology::LineStripAdjacency:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case Topology::TrianglesAdjacency:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case Topology::TriangleStripAdjacency:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        case Topology::Patches:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkStencilOp toVkStencilOp(StencilOp op)
{
    switch (op)
    {
        case StencilOp::Keep:
            return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:
            return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:
            return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrementClamp:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementClamp:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:
            return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrementWrap:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementWrap:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
}

VkBlendOp toVkBlendOp(BlendOp op)
{
    switch (op)
    {
        case BlendOp::Add:
            return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return VK_BLEND_OP_MIN;
        case BlendOp::Max:
            return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkCompareOp toVkCompare(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:
            return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkBlendFactor toVkBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

class VulkanDriver final : public Driver
{
public:
    VulkanDriver(const VulkanPlatform& platform, void (*log)(const char*))
            : platform_(platform),
              log_(log)
    {
    }

    ~VulkanDriver() override
    {
        if (device_)
        {
            vkDeviceWaitIdle(device_);
            for (VulkanWindow* extra: swapchains_) destroyWindow(extra);
            for (VulkanPipeline& pipeline: pipelines_)
            {
                vkDestroyPipeline(device_, pipeline.pipeline, nullptr);
                vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
                vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
                vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
                if (pipeline.storageSetLayout)
                    vkDestroyDescriptorSetLayout(device_, pipeline.storageSetLayout, nullptr);
                if (pipeline.imageSetLayout)
                    vkDestroyDescriptorSetLayout(device_, pipeline.imageSetLayout, nullptr);
            }
            for (VulkanReadback& readback: readbacks_)
            {
                vkDestroyBuffer(device_, readback.buffer, nullptr);
                memory_.free(readback.allocation);
            }
            for (VulkanTexture& texture: textures_)
            {
                for (size_t i = 0; i < texture.attachmentViews.size(); ++i)
                    vkDestroyImageView(device_, texture.attachmentViews[i].view, nullptr);
                for (std::uint32_t i = 0; i < VulkanTexture::kMaxMipViews; ++i)
                    if (texture.mipViews[i])
                        vkDestroyImageView(device_, texture.mipViews[i], nullptr);
                vkDestroyImageView(device_, texture.view, nullptr);
                vkDestroyImage(device_, texture.image, nullptr);
                memory_.free(texture.allocation);
            }
            for (VulkanSampler& sampler: samplers_)
                vkDestroySampler(device_, sampler.sampler, nullptr);
            for (VulkanShader& shader: shaders_)
                vkDestroyShaderModule(device_, shader.module, nullptr);
            for (VulkanBuffer& buffer: buffers_)
            {
                for (std::uint32_t i = 0; i < buffer.versionCount; ++i)
                {
                    vkDestroyBuffer(device_, buffer.versions[i].buffer, nullptr);
                    memory_.free(buffer.versions[i].allocation);
                }
            }
            collectGarbage(true);
            destroySwapchain();
            for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            {
                if (frames_[i].imageAvailable)
                    vkDestroySemaphore(device_, frames_[i].imageAvailable, nullptr);
                if (frames_[i].inFlight) vkDestroyFence(device_, frames_[i].inFlight, nullptr);
            }
            for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
                if (descriptorPools_[i])
                    vkDestroyDescriptorPool(device_, descriptorPools_[i], nullptr);
            if (occlusionPool_) vkDestroyQueryPool(device_, occlusionPool_, nullptr);
            if (timePool_) vkDestroyQueryPool(device_, timePool_, nullptr);
            if (commandPool_) vkDestroyCommandPool(device_, commandPool_, nullptr);
            memory_.shutdown();
            vkDestroyDevice(device_, nullptr);
        }
        if (window_->surface) vkDestroySurfaceKHR(instance_, window_->surface, nullptr);
        if (messenger_) destroyMessenger(instance_, messenger_);
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

    DriverError init(bool debug)
    {
        if (!createInstance(debug)) return DriverError::ContextFailed;
        mainWindow_.platform = platform_;

        std::uint64_t surface = 0;
        if (!platform_.createSurface(platform_.user, instance_, &surface))
        {
            log("Vulkan: the platform could not create a surface");
            return DriverError::ContextFailed;
        }
        window_->surface = (VkSurfaceKHR) surface;

        if (!pickPhysicalDevice()) return DriverError::VersionTooLow;
        if (!createDevice()) return DriverError::ContextFailed;
        if (!createFrames() || !createDescriptorPools() || !createQueryPools())
            return DriverError::ContextFailed;
        rebuildSwapchain();
        return DriverError::None;
    }

    DriverType type() const override { return DriverType::Vulkan; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();

        VulkanBuffer buffer;
        buffer.size = desc.size;
        buffer.usage = desc.usage;
        buffer.indexFormat = desc.indexFormat;
        switch (desc.usage)
        {
            case BufferUsage::Vertex:
                buffer.vkUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                break;
            case BufferUsage::Index:
                buffer.vkUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
                break;
            case BufferUsage::Uniform:
                buffer.vkUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                break;
            case BufferUsage::Storage:
                buffer.vkUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                break;
            case BufferUsage::Indirect:
                buffer.vkUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
                break;
        }
        buffer.vkUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        const bool visible = desc.update != BufferUpdate::Static;
        if (!createVersion(buffer, desc.debugName, visible)) return BufferHandle();
        buffer.gpuWritten = !visible;
        if (visible)
        {
            buffer.shadow.resize(desc.size);
            if (desc.data) memcpy(buffer.shadow.data(), desc.data, desc.size);
            else
                memset(buffer.shadow.data(), 0, desc.size);
            memcpy(buffer.versions[0].mapped, buffer.shadow.data(), desc.size);
        }
        if (desc.data && !visible)
        {
            const VkBuffer staging = stagingBuffer(desc.data, desc.size);
            if (staging)
            {
                VkCommandBuffer commands = transferCommands();
                VkBufferCopy region = {};
                region.size = desc.size;
                vkCmdCopyBuffer(commands, staging, buffer.versions[0].buffer, 1, &region);
                transferBarrier(commands);
                stagingUsed(desc.size);
            }
        }
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override
    {
        const VulkanShader* shader = shaders_.get(handleCast<ShaderSlot>(desc.shader));
        bool valid = shader && shader->stage == ShaderStage::Compute &&
                     desc.uniformBlockCount <= ComputePipelineDesc::kMaxUniformBlocks &&
                     desc.textureCount <= ComputePipelineDesc::kMaxTextures &&
                     desc.storageBufferCount <= ComputePipelineDesc::kMaxStorageBuffers &&
                     desc.storageTextureCount <= ComputePipelineDesc::kMaxStorageTextures;
        for (std::uint32_t i = 0; valid && i < desc.uniformBlockCount; ++i)
            if (desc.uniformBlocks[i].slot >= kMaxUniformSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.textureCount; ++i)
            if (desc.textures[i].slot >= kMaxTextureSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.storageBufferCount; ++i)
            if (desc.storageBuffers[i].slot >= ComputePipelineDesc::kMaxStorageBuffers)
                valid = false;
        for (std::uint32_t i = 0; valid && i < desc.storageTextureCount; ++i)
            if (desc.storageTextures[i].slot >= ComputePipelineDesc::kMaxStorageTextures)
                valid = false;
        if (!valid)
        {
            log("createComputePipeline: invalid shader or too many bindings");
            return PipelineHandle();
        }

        slotOverflow_ = false;
        VulkanPipeline pipeline;
        pipeline.compute = true;
        pipeline.uniformCount = desc.uniformBlockCount;
        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
            pipeline.uniformSlots[i] = desc.uniformBlocks[i].slot;
        pipeline.textureCount = desc.textureCount;
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
            pipeline.textureSlots[i] = desc.textures[i].slot;
        pipeline.storageCount = desc.storageBufferCount;
        for (std::uint32_t i = 0; i < desc.storageBufferCount; ++i)
            pipeline.storageSlots[i] = desc.storageBuffers[i].slot;
        pipeline.imageCount = desc.storageTextureCount;
        for (std::uint32_t i = 0; i < desc.storageTextureCount; ++i)
            pipeline.imageSlots[i] = desc.storageTextures[i].slot;
        mergeSlots(pipeline.uniformSlots, pipeline.uniformCount,
                ComputePipelineDesc::kMaxUniformBlocks, kMaxUniformSlots, *shader,
                BindingKind::UniformBlock);
        mergeSlots(pipeline.textureSlots, pipeline.textureCount, ComputePipelineDesc::kMaxTextures,
                kMaxTextureSlots, *shader, BindingKind::Texture);
        mergeSlots(pipeline.storageSlots, pipeline.storageCount,
                ComputePipelineDesc::kMaxStorageBuffers, ComputePipelineDesc::kMaxStorageBuffers,
                *shader, BindingKind::StorageBuffer);
        mergeSlots(pipeline.imageSlots, pipeline.imageCount,
                ComputePipelineDesc::kMaxStorageTextures, ComputePipelineDesc::kMaxStorageTextures,
                *shader, BindingKind::StorageTexture);
        if (slotOverflow_) return PipelineHandle();
        sortSlots(pipeline.uniformSlots, pipeline.uniformCount);
        sortSlots(pipeline.textureSlots, pipeline.textureCount);

        const VkShaderStageFlags stage = VK_SHADER_STAGE_COMPUTE_BIT;
        bool created =
                createSlotLayout(pipeline.uniformSlots, pipeline.uniformCount,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, stage, &pipeline.setLayout) &&
                createSlotLayout(pipeline.textureSlots, pipeline.textureCount,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stage,
                        &pipeline.textureSetLayout) &&
                createSlotLayout(pipeline.storageSlots, pipeline.storageCount,
                        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stage, &pipeline.storageSetLayout) &&
                createSlotLayout(pipeline.imageSlots, pipeline.imageCount,
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, stage, &pipeline.imageSetLayout);
        if (created)
        {
            const VkDescriptorSetLayout setLayouts[4] = { pipeline.setLayout,
                pipeline.textureSetLayout, pipeline.storageSetLayout, pipeline.imageSetLayout };
            VkPipelineLayoutCreateInfo layout = {};
            layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout.setLayoutCount = 4;
            layout.pSetLayouts = setLayouts;
            created = vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline.layout) ==
                      VK_SUCCESS;
        }
        if (created)
        {
            VkComputePipelineCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = shader->module;
            info.stage.pName = "main";
            info.layout = pipeline.layout;
            created = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                              &pipeline.pipeline) == VK_SUCCESS;
        }
        if (!created)
        {
            log("createComputePipeline: could not create the pipeline or its layouts");
            destroyLayouts(pipeline);
            return PipelineHandle();
        }
        setName(VK_OBJECT_TYPE_PIPELINE, (std::uint64_t) pipeline.pipeline, desc.debugName);
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) override
    {
        if (passActive_)
        {
            log("updateBuffer: not allowed inside a render pass");
            return;
        }
        VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || static_cast<std::uint64_t>(offset) + size > buffer->size)
        {
            log("updateBuffer: invalid buffer handle or range");
            return;
        }

        if (buffer->gpuWritten)
        {
            VkBuffer staging = stagingBuffer(data, size);
            if (!staging) return;
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            VkCommandBuffer commands = transferCommands();
            VkBufferCopy region = {};
            region.dstOffset = offset;
            region.size = size;
            vkCmdCopyBuffer(commands, staging, version.buffer, 1, &region);
            transferBarrier(commands);
            stagingUsed(size);
            return;
        }

        memcpy(buffer->shadow.data() + offset, data, size);
        const std::uint32_t previous = buffer->current;
        if (buffer->versions[previous].lastUsedFrame != kNeverUsed)
        {
            std::uint32_t next = buffer->versionCount;
            for (std::uint32_t i = 0; i < buffer->versionCount && next == buffer->versionCount; ++i)
            {
                const std::uint64_t used = buffer->versions[i].lastUsedFrame;
                if (i != previous && (used == kNeverUsed || used + kFramesInFlight < frameNumber_))
                    next = i;
            }
            if (next == buffer->versionCount && !createVersion(*buffer, nullptr))
            {
                log("updateBuffer: too many versions of this buffer are still in use");
                next = previous;
            }
            if (next != previous)
            {
                memcpy(buffer->versions[next].mapped, buffer->shadow.data(), buffer->size);
                buffer->versions[next].lastUsedFrame = kNeverUsed;
                buffer->current = next;
                return;
            }
        }
        memcpy(static_cast<char*>(buffer->versions[buffer->current].mapped) + offset, data, size);
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.spirv || desc.spirvSize == 0 || desc.spirvSize % 4 != 0)
        {
            log("createShader: the Vulkan backend needs SPIR-V in ShaderDesc::spirv");
            return ShaderHandle();
        }

        VulkanShader shader;
        shader.stage = desc.stage;
        for (std::uint32_t i = 0; i < desc.bindingCount; ++i)
        {
            const std::uint32_t kind = static_cast<std::uint32_t>(desc.bindings[i].kind);
            if (shader.slotCount[kind] >= PipelineDesc::kMaxTextures)
            {
                log("createShader: too many bindings");
                return ShaderHandle();
            }
            shader.slots[kind][shader.slotCount[kind]++] = desc.bindings[i].slot;
        }
        VkShaderModuleCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = desc.spirvSize;
        info.pCode = static_cast<const std::uint32_t*>(desc.spirv);
        if (vkCreateShaderModule(device_, &info, nullptr, &shader.module) != VK_SUCCESS)
        {
            log("createShader: could not create the shader module");
            return ShaderHandle();
        }
        setName(VK_OBJECT_TYPE_SHADER_MODULE, (std::uint64_t) shader.module, desc.debugName);
        return handleCast<ShaderHandle>(shaders_.insert(shader));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        const VulkanShader* vertex = shaders_.get(handleCast<ShaderSlot>(desc.vertexShader));
        const VulkanShader* fragment = shaders_.get(handleCast<ShaderSlot>(desc.fragmentShader));
        const VulkanShader* geometry =
                desc.geometryShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.geometryShader))
                        : nullptr;
        const VulkanShader* control =
                desc.tessControlShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.tessControlShader))
                        : nullptr;
        const VulkanShader* evaluation =
                desc.tessEvalShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.tessEvalShader))
                        : nullptr;
        const bool patches = desc.topology == Topology::Patches;
        bool valid = vertex && fragment && desc.attributeCount <= PipelineDesc::kMaxAttributes &&
                     desc.vertexBufferCount <= PipelineDesc::kMaxVertexBuffers &&
                     desc.targets.colorCount <= TargetFormats::kMaxColors &&
                     desc.uniformBlockCount <= PipelineDesc::kMaxUniformBlocks &&
                     desc.textureCount <= PipelineDesc::kMaxTextures &&
                     desc.storageBufferCount <= PipelineDesc::kMaxStorageBuffers;
        for (std::uint32_t i = 0; valid && i < desc.storageBufferCount; ++i)
            if (desc.storageBuffers[i].slot >= PipelineDesc::kMaxStorageBuffers) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.textureCount; ++i)
            if (desc.textures[i].slot >= kMaxTextureSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.uniformBlockCount; ++i)
            if (desc.uniformBlocks[i].slot >= kMaxUniformSlots) valid = false;
        for (std::uint32_t i = 0; valid && i < desc.attributeCount; ++i)
            if (desc.attributes[i].buffer >= desc.vertexBufferCount) valid = false;
        if (desc.geometryShader.valid() &&
                (!geometry || geometry->stage != ShaderStage::Geometry || !caps_.geometryShaders))
            valid = false;
        if (isAdjacency(desc.topology) && !geometry) valid = false;
        if ((desc.tessControlShader.valid() &&
                    (!control || control->stage != ShaderStage::TessControl)) ||
                (desc.tessEvalShader.valid() &&
                        (!evaluation || evaluation->stage != ShaderStage::TessEval)) ||
                patches != (control && evaluation) ||
                (control != nullptr) != (evaluation != nullptr) ||
                (patches && (desc.patchControlPoints == 0 ||
                                    desc.patchControlPoints > caps_.maxPatchControlPoints)) ||
                ((control || evaluation) && !caps_.tessellation))
            valid = false;
        if (desc.independentBlend && !caps_.independentBlend) valid = false;
        if (!valid)
        {
            log("createPipeline: invalid shader handle or vertex input");
            return PipelineHandle();
        }

        VkPipelineShaderStageCreateInfo stages[5] = {};
        std::uint32_t stageCount = 2;
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex->module;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment->module;
        stages[1].pName = "main";
        if (geometry)
        {
            stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[2].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
            stages[2].module = geometry->module;
            stages[2].pName = "main";
            stageCount = 3;
        }
        if (control)
        {
            stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[stageCount].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
            stages[stageCount].module = control->module;
            stages[stageCount].pName = "main";
            ++stageCount;
            stages[stageCount].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[stageCount].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
            stages[stageCount].module = evaluation->module;
            stages[stageCount].pName = "main";
            ++stageCount;
        }
        VkPipelineTessellationStateCreateInfo tessellation = {};
        tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
        tessellation.patchControlPoints = desc.patchControlPoints;

        VkVertexInputBindingDescription bindings[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
        {
            bindings[i].binding = i;
            bindings[i].stride = desc.vertexBuffers[i].stride;
            bindings[i].inputRate = desc.vertexBuffers[i].step == VertexStep::Instance
                                            ? VK_VERTEX_INPUT_RATE_INSTANCE
                                            : VK_VERTEX_INPUT_RATE_VERTEX;
        }
        VkVertexInputAttributeDescription attributes[PipelineDesc::kMaxAttributes] = {};
        for (std::uint32_t i = 0; i < desc.attributeCount; ++i)
        {
            attributes[i].location = desc.attributes[i].location;
            attributes[i].binding = desc.attributes[i].buffer;
            attributes[i].format = toVkFormat(desc.attributes[i].format);
            attributes[i].offset = desc.attributes[i].offset;
        }
        VkPipelineVertexInputStateCreateInfo vertexInput = {};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = desc.vertexBufferCount;
        vertexInput.pVertexBindingDescriptions = bindings;
        vertexInput.vertexAttributeDescriptionCount = desc.attributeCount;
        vertexInput.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo assembly = {};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = toVkTopology(desc.topology);
        assembly.primitiveRestartEnable = desc.topology == Topology::TriangleStrip ||
                                          desc.topology == Topology::LineStrip ||
                                          desc.topology == Topology::LineStripAdjacency ||
                                          desc.topology == Topology::TriangleStripAdjacency;

        VkPipelineViewportStateCreateInfo viewport = {};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = {};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        if (desc.wireframe && caps_.wireframe) raster.polygonMode = VK_POLYGON_MODE_LINE;
        raster.depthBiasEnable = desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f;
        raster.depthBiasConstantFactor = desc.depthBiasConstant;
        raster.depthBiasSlopeFactor = desc.depthBiasSlope;
        raster.cullMode = VK_CULL_MODE_NONE;
        if (desc.cullMode == CullMode::Front) raster.cullMode = VK_CULL_MODE_FRONT_BIT;
        if (desc.cullMode == CullMode::Back) raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = desc.frontFace == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE
                                                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisample = {};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.alphaToCoverageEnable = desc.alphaToCoverage;
        multisample.rasterizationSamples =
                desc.targets.window ? VK_SAMPLE_COUNT_1_BIT : toSampleCount(desc.targets.samples);

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = desc.depthTest;
        depthStencil.depthWriteEnable = desc.depthWrite;
        depthStencil.depthCompareOp = toVkCompare(desc.depthCompare);
        depthStencil.maxDepthBounds = 1.0f;
        depthStencil.stencilTestEnable = desc.stencilTest;
        const StencilFace* faces[2] = { &desc.stencilFront, &desc.stencilBack };
        VkStencilOpState* states[2] = { &depthStencil.front, &depthStencil.back };
        for (int face = 0; face < 2; ++face)
        {
            states[face]->failOp = toVkStencilOp(faces[face]->failOp);
            states[face]->passOp = toVkStencilOp(faces[face]->passOp);
            states[face]->depthFailOp = toVkStencilOp(faces[face]->depthFailOp);
            states[face]->compareOp = toVkCompare(faces[face]->compare);
            states[face]->compareMask = desc.stencilReadMask;
            states[face]->writeMask = desc.stencilWriteMask;
        }

        const std::uint32_t colorCount = desc.targets.window ? 1 : desc.targets.colorCount;
        VkFormat colorFormats[TargetFormats::kMaxColors] = {};
        VkPipelineColorBlendAttachmentState blends[TargetFormats::kMaxColors] = {};
        BlendState shared;
        shared.blend = desc.blend;
        shared.srcColor = desc.srcColor;
        shared.dstColor = desc.dstColor;
        shared.srcAlpha = desc.srcAlpha;
        shared.dstAlpha = desc.dstAlpha;
        shared.colorBlendOp = desc.colorBlendOp;
        shared.alphaBlendOp = desc.alphaBlendOp;
        shared.colorMask = desc.colorMask;
        for (std::uint32_t i = 0; i < colorCount; ++i)
        {
            colorFormats[i] = desc.targets.window ? format_ : toVkFormat(desc.targets.colors[i]);
            const BlendState& state = desc.independentBlend ? desc.targetBlend[i] : shared;
            blends[i].blendEnable = state.blend;
            blends[i].srcColorBlendFactor = toVkBlendFactor(state.srcColor);
            blends[i].dstColorBlendFactor = toVkBlendFactor(state.dstColor);
            blends[i].colorBlendOp = toVkBlendOp(state.colorBlendOp);
            blends[i].srcAlphaBlendFactor = toVkBlendFactor(state.srcAlpha);
            blends[i].dstAlphaBlendFactor = toVkBlendFactor(state.dstAlpha);
            blends[i].alphaBlendOp = toVkBlendOp(state.alphaBlendOp);
            blends[i].colorWriteMask = 0;
            if (state.colorMask & kColorRed) blends[i].colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
            if (state.colorMask & kColorGreen) blends[i].colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
            if (state.colorMask & kColorBlue) blends[i].colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
            if (state.colorMask & kColorAlpha) blends[i].colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo blend = {};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = colorCount;
        blend.pAttachments = blends;

        const VkDynamicState dynamicStates[4] = { VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_FRONT_FACE,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo dynamic = {};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 4;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        rendering.colorAttachmentCount = colorCount;
        rendering.pColorAttachmentFormats = colorFormats;
        rendering.depthAttachmentFormat =
                desc.targets.window ? depthFormat_ : textureFormat(desc.targets.depth);
        if (desc.targets.window || desc.targets.depth == TextureFormat::Depth24Stencil8)
            rendering.stencilAttachmentFormat = depthStencilFormat_;

        VulkanPipeline pipeline;
        pipeline.targets = desc.targets;
        pipeline.frontFace = raster.frontFace;
        pipeline.vertexBufferCount = desc.vertexBufferCount;
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
            pipeline.vertexBuffers[i] = desc.vertexBuffers[i];

        slotOverflow_ = false;
        pipeline.uniformCount = desc.uniformBlockCount;
        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
            pipeline.uniformSlots[i] = desc.uniformBlocks[i].slot;
        mergeSlots(pipeline.uniformSlots, pipeline.uniformCount, PipelineDesc::kMaxUniformBlocks,
                kMaxUniformSlots, *vertex, BindingKind::UniformBlock);
        mergeSlots(pipeline.uniformSlots, pipeline.uniformCount, PipelineDesc::kMaxUniformBlocks,
                kMaxUniformSlots, *fragment, BindingKind::UniformBlock);
        if (geometry)
            mergeSlots(pipeline.uniformSlots, pipeline.uniformCount,
                    PipelineDesc::kMaxUniformBlocks, kMaxUniformSlots, *geometry,
                    BindingKind::UniformBlock);
        if (control)
            mergeSlots(pipeline.uniformSlots, pipeline.uniformCount,
                    PipelineDesc::kMaxUniformBlocks, kMaxUniformSlots, *control,
                    BindingKind::UniformBlock);
        if (evaluation)
            mergeSlots(pipeline.uniformSlots, pipeline.uniformCount,
                    PipelineDesc::kMaxUniformBlocks, kMaxUniformSlots, *evaluation,
                    BindingKind::UniformBlock);
        if (slotOverflow_) return PipelineHandle();
        for (std::uint32_t i = 1; i < pipeline.uniformCount; ++i)
            for (std::uint32_t j = i;
                    j > 0 && pipeline.uniformSlots[j - 1] > pipeline.uniformSlots[j]; --j)
            {
                const std::uint32_t swapped = pipeline.uniformSlots[j];
                pipeline.uniformSlots[j] = pipeline.uniformSlots[j - 1];
                pipeline.uniformSlots[j - 1] = swapped;
            }

        VkDescriptorSetLayoutBinding setBindings[PipelineDesc::kMaxUniformBlocks] = {};
        for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
        {
            setBindings[i].binding = pipeline.uniformSlots[i];
            setBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            setBindings[i].descriptorCount = 1;
            setBindings[i].stageFlags = graphicsStages();
        }
        VkDescriptorSetLayoutCreateInfo setLayout = {};
        setLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayout.bindingCount = pipeline.uniformCount;
        setLayout.pBindings = setBindings;
        if (vkCreateDescriptorSetLayout(device_, &setLayout, nullptr, &pipeline.setLayout) !=
                VK_SUCCESS)
        {
            log("createPipeline: could not create the descriptor set layout");
            return PipelineHandle();
        }

        pipeline.textureCount = desc.textureCount;
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
            pipeline.textureSlots[i] = desc.textures[i].slot;
        mergeSlots(pipeline.textureSlots, pipeline.textureCount, PipelineDesc::kMaxTextures,
                kMaxTextureSlots, *vertex, BindingKind::Texture);
        mergeSlots(pipeline.textureSlots, pipeline.textureCount, PipelineDesc::kMaxTextures,
                kMaxTextureSlots, *fragment, BindingKind::Texture);
        if (geometry)
            mergeSlots(pipeline.textureSlots, pipeline.textureCount, PipelineDesc::kMaxTextures,
                    kMaxTextureSlots, *geometry, BindingKind::Texture);
        if (control)
            mergeSlots(pipeline.textureSlots, pipeline.textureCount, PipelineDesc::kMaxTextures,
                    kMaxTextureSlots, *control, BindingKind::Texture);
        if (evaluation)
            mergeSlots(pipeline.textureSlots, pipeline.textureCount, PipelineDesc::kMaxTextures,
                    kMaxTextureSlots, *evaluation, BindingKind::Texture);
        if (slotOverflow_)
        {
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            return PipelineHandle();
        }
        for (std::uint32_t i = 1; i < pipeline.textureCount; ++i)
            for (std::uint32_t j = i;
                    j > 0 && pipeline.textureSlots[j - 1] > pipeline.textureSlots[j]; --j)
            {
                const std::uint32_t swapped = pipeline.textureSlots[j];
                pipeline.textureSlots[j] = pipeline.textureSlots[j - 1];
                pipeline.textureSlots[j - 1] = swapped;
            }

        VkDescriptorSetLayoutBinding textureBindings[PipelineDesc::kMaxTextures] = {};
        for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
        {
            textureBindings[i].binding = pipeline.textureSlots[i];
            textureBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            textureBindings[i].descriptorCount = 1;
            textureBindings[i].stageFlags = graphicsStages();
        }
        VkDescriptorSetLayoutCreateInfo textureSetLayout = {};
        textureSetLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        textureSetLayout.bindingCount = pipeline.textureCount;
        textureSetLayout.pBindings = textureBindings;
        if (vkCreateDescriptorSetLayout(device_, &textureSetLayout, nullptr,
                    &pipeline.textureSetLayout) != VK_SUCCESS)
        {
            log("createPipeline: could not create the texture set layout");
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            return PipelineHandle();
        }

        pipeline.storageCount = desc.storageBufferCount;
        for (std::uint32_t i = 0; i < desc.storageBufferCount; ++i)
            pipeline.storageSlots[i] = desc.storageBuffers[i].slot;
        mergeSlots(pipeline.storageSlots, pipeline.storageCount, PipelineDesc::kMaxStorageBuffers,
                PipelineDesc::kMaxStorageBuffers, *vertex, BindingKind::StorageBuffer);
        mergeSlots(pipeline.storageSlots, pipeline.storageCount, PipelineDesc::kMaxStorageBuffers,
                PipelineDesc::kMaxStorageBuffers, *fragment, BindingKind::StorageBuffer);
        if (geometry)
            mergeSlots(pipeline.storageSlots, pipeline.storageCount,
                    PipelineDesc::kMaxStorageBuffers, PipelineDesc::kMaxStorageBuffers, *geometry,
                    BindingKind::StorageBuffer);
        if (control)
            mergeSlots(pipeline.storageSlots, pipeline.storageCount,
                    PipelineDesc::kMaxStorageBuffers, PipelineDesc::kMaxStorageBuffers, *control,
                    BindingKind::StorageBuffer);
        if (evaluation)
            mergeSlots(pipeline.storageSlots, pipeline.storageCount,
                    PipelineDesc::kMaxStorageBuffers, PipelineDesc::kMaxStorageBuffers, *evaluation,
                    BindingKind::StorageBuffer);
        if (slotOverflow_)
        {
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            return PipelineHandle();
        }
        if (!createSlotLayout(pipeline.storageSlots, pipeline.storageCount,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, graphicsStages(),
                    &pipeline.storageSetLayout))
        {
            log("createPipeline: could not create the storage set layout");
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            return PipelineHandle();
        }

        const VkDescriptorSetLayout setLayouts[3] = { pipeline.setLayout, pipeline.textureSetLayout,
            pipeline.storageSetLayout };
        VkPipelineLayoutCreateInfo layout = {};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 3;
        layout.pSetLayouts = setLayouts;
        if (vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline.layout) != VK_SUCCESS)
        {
            log("createPipeline: could not create the pipeline layout");
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.storageSetLayout, nullptr);
            return PipelineHandle();
        }

        VkGraphicsPipelineCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.pNext = &rendering;
        info.stageCount = stageCount;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        if (patches) info.pTessellationState = &tessellation;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipeline.layout;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                    &pipeline.pipeline) != VK_SUCCESS)
        {
            log("createPipeline: could not create the pipeline");
            vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
            vkDestroyDescriptorSetLayout(device_, pipeline.storageSetLayout, nullptr);
            return PipelineHandle();
        }
        setName(VK_OBJECT_TYPE_PIPELINE, (std::uint64_t) pipeline.pipeline, desc.debugName);
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    TextureHandle createTexture(const TextureDesc& desc) override
    {
        const std::uint32_t largest = desc.width > desc.height ? desc.width : desc.height;
        if (desc.format == TextureFormat::None || desc.width == 0 || desc.height == 0 ||
                desc.depth == 0 || largest > caps_.maxTextureSize ||
                (isCube(desc.type) && desc.width != desc.height) ||
                (desc.type == TextureType::TextureCubeArray && !caps_.cubeArrays))
        {
            log("createTexture: invalid format or size, or cube map arrays are not supported");
            return TextureHandle();
        }
        if (!supportedFormat(desc))
        {
            log("createTexture: compressed format not supported, or used as a render target or "
                "as a 3D texture");
            return TextureHandle();
        }
        if (!validSamples(desc, caps_.maxSamples))
        {
            log("createTexture: a multisampled texture must be a 2D render target only, with one "
                "mip, no data and a supported sample count");
            return TextureHandle();
        }

        const bool depth = isDepthFormat(desc.format);
        const bool volume = desc.type == TextureType::Texture3D;
        const bool target = (desc.usage & kTextureRenderTarget) != 0;
        VulkanTexture texture;
        texture.type = desc.type;
        texture.width = desc.width;
        texture.height = desc.height;
        texture.depth = desc.type == TextureType::Texture2D || desc.type == TextureType::TextureCube
                                ? 1
                                : desc.depth;
        texture.format = desc.format;
        texture.usage = desc.usage;
        texture.samples = desc.samples;
        const std::uint32_t deepest = volume ? texture.depth : 1;
        const std::uint32_t fullChain = fullMipCount(largest > deepest ? largest : deepest, 1);
        texture.mipLevels = desc.mipLevels == 0 ? fullChain : desc.mipLevels;
        if (texture.mipLevels > fullChain) texture.mipLevels = fullChain;
        const std::uint32_t layers = imageLayers(texture);

        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        if (isCube(desc.type)) image.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        if (volume && target) image.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        image.imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        image.format = textureFormat(desc.format);
        image.extent.width = desc.width;
        image.extent.height = desc.height;
        image.extent.depth = volume ? texture.depth : 1;
        image.mipLevels = texture.mipLevels;
        image.arrayLayers = layers;
        image.samples = toSampleCount(desc.samples);
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        if (desc.samples == 1) image.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (desc.samples == 1)
        {
            VkFormatProperties properties = {};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, image.format, &properties);
            texture.copyDestination = !depth || (properties.optimalTilingFeatures &
                                                        VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
            if (texture.copyDestination) image.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }
        if (target)
            image.usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                 : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (desc.usage & kTextureStorage)
        {
            VkFormatProperties properties = {};
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, image.format, &properties);
            if (!isStorageFormat(desc.format, false) || desc.samples != 1 ||
                    !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
            {
                log("createTexture: this format cannot be used as a storage texture");
                return TextureHandle();
            }
            image.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        }
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (desc.samples > 1)
        {
            VkImageFormatProperties supported = {};
            if (vkGetPhysicalDeviceImageFormatProperties(physicalDevice_, image.format,
                        image.imageType, image.tiling, image.usage, image.flags,
                        &supported) != VK_SUCCESS ||
                    !(supported.sampleCounts & image.samples))
            {
                log("createTexture: sample count not supported for this format");
                return TextureHandle();
            }
        }
        if (vkCreateImage(device_, &image, nullptr, &texture.image) != VK_SUCCESS)
        {
            log("createTexture: could not create the image");
            return TextureHandle();
        }

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device_, texture.image, &requirements);

        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        if (desc.type == TextureType::Texture2DArray) view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        if (desc.type == TextureType::TextureCube) view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        if (desc.type == TextureType::TextureCubeArray)
            view.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        if (volume) view.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view.format = image.format;
        view.subresourceRange.aspectMask =
                depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = texture.mipLevels;
        view.subresourceRange.layerCount = layers;

        if (!memory_.allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VulkanMemoryKind::Image, &texture.allocation) ||
                vkBindImageMemory(device_, texture.image, texture.allocation.memory,
                        texture.allocation.offset) != VK_SUCCESS ||
                vkCreateImageView(device_, &view, nullptr, &texture.view) != VK_SUCCESS)
        {
            log("createTexture: could not allocate memory or create the view");
            vkDestroyImage(device_, texture.image, nullptr);
            memory_.free(texture.allocation);
            return TextureHandle();
        }
        setName(VK_OBJECT_TYPE_IMAGE, (std::uint64_t) texture.image, desc.debugName);

        imageBarrier(transferCommands(), texture.image, aspectOf(texture.format), 0,
                texture.mipLevels, 0, layers, VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (desc.data && !depth)
        {
            const std::uint32_t images = volume ? 1 : layers;
            const std::size_t imageBytes = levelBytes(desc.format, desc.width, desc.height);
            for (std::uint32_t i = 0; i < images; ++i)
                recordUpload(texture, 0, i, static_cast<const char*>(desc.data) + i * imageBytes);
            if (desc.generateMipmaps && !isCompressedFormat(desc.format)) recordMipmaps(texture);
        }
        return handleCast<TextureHandle>(textures_.insert(texture));
    }

    void copyTexture(const TextureCopy& copy) override
    {
        const VulkanTexture* source = textures_.get(handleCast<TextureSlotHandle>(copy.source));
        const VulkanTexture* destination =
                textures_.get(handleCast<TextureSlotHandle>(copy.destination));
        if (passActive_ ||
                !validCopy(source, destination, copy,
                        source ? layerCount(*source, copy.sourceMip) : 0,
                        destination ? layerCount(*destination, copy.destinationMip) : 0) ||
                !destination->copyDestination)
        {
            log("copyTexture: invalid textures, formats or rectangles, or called inside a render "
                "pass");
            return;
        }
        const bool sourceVolume = source->type == TextureType::Texture3D;
        const bool destinationVolume = destination->type == TextureType::Texture3D;
        const std::uint32_t sourceLayer = sourceVolume ? 0 : copy.sourceLayer;
        const std::uint32_t destinationLayer = destinationVolume ? 0 : copy.destinationLayer;
        const VkImageAspectFlags aspect = aspectOf(source->format);

        VkImageCopy region = {};
        region.srcSubresource.aspectMask = aspect;
        region.srcSubresource.mipLevel = copy.sourceMip;
        region.srcSubresource.baseArrayLayer = sourceLayer;
        region.srcSubresource.layerCount = 1;
        region.srcOffset.x = static_cast<std::int32_t>(copy.sourceX);
        region.srcOffset.y = static_cast<std::int32_t>(copy.sourceY);
        region.srcOffset.z = sourceVolume ? static_cast<std::int32_t>(copy.sourceLayer) : 0;
        region.dstSubresource.aspectMask = aspect;
        region.dstSubresource.mipLevel = copy.destinationMip;
        region.dstSubresource.baseArrayLayer = destinationLayer;
        region.dstSubresource.layerCount = 1;
        region.dstOffset.x = static_cast<std::int32_t>(copy.destinationX);
        region.dstOffset.y = static_cast<std::int32_t>(copy.destinationY);
        region.dstOffset.z =
                destinationVolume ? static_cast<std::int32_t>(copy.destinationLayer) : 0;
        region.extent.width = copy.width;
        region.extent.height = copy.height;
        region.extent.depth = 1;

        VkCommandBuffer commands = transferCommands();
        imageBarrier(commands, source->image, aspect, copy.sourceMip, 1, sourceLayer, 1,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        imageBarrier(commands, destination->image, aspect, copy.destinationMip, 1, destinationLayer,
                1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                destination->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        imageBarrier(commands, source->image, aspect, copy.sourceMip, 1, sourceLayer, 1,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        imageBarrier(commands, destination->image, aspect, copy.destinationMip, 1, destinationLayer,
                1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void copyBuffer(BufferHandle sourceHandle, std::uint32_t sourceOffset,
            BufferHandle destinationHandle, std::uint32_t destinationOffset,
            std::uint32_t size) override
    {
        VulkanBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        VulkanBuffer* destination = buffers_.get(handleCast<BufferSlot>(destinationHandle));
        if (passActive_ || !source || !destination ||
                !validBufferCopy(true, source == destination, source->usage == BufferUsage::Index,
                        destination->usage == BufferUsage::Index, source->size, sourceOffset,
                        destination->size, destinationOffset, size))
        {
            log("copyBuffer: invalid buffers or ranges, index and other buffers mixed, or called "
                "inside a render pass");
            return;
        }
        BufferVersion& from = source->versions[source->current];
        BufferVersion& to = destination->versions[destination->current];
        from.lastUsedFrame = frameNumber_;
        to.lastUsedFrame = frameNumber_;
        destination->gpuWritten = true;

        VkCommandBuffer commands = transferCommands();
        VkBufferCopy region = {};
        region.srcOffset = sourceOffset;
        region.dstOffset = destinationOffset;
        region.size = size;
        vkCmdCopyBuffer(commands, from.buffer, to.buffer, 1, &region);
        transferBarrier(commands);
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        VkSamplerCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = desc.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter = desc.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = desc.mipFilter == MipFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                              : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = toVkAddress(desc.addressU);
        info.addressModeV = toVkAddress(desc.addressV);
        info.addressModeW = toVkAddress(desc.addressW);
        info.compareEnable = desc.compare;
        info.compareOp = toVkCompare(desc.compareOp);
        info.maxAnisotropy = 1.0f;
        if (desc.maxAnisotropy > 1.0f && caps_.maxAnisotropy > 1.0f)
        {
            info.anisotropyEnable = VK_TRUE;
            info.maxAnisotropy = desc.maxAnisotropy < caps_.maxAnisotropy ? desc.maxAnisotropy
                                                                          : caps_.maxAnisotropy;
        }
        info.maxLod = desc.mipFilter == MipFilter::None ? 0.25f : VK_LOD_CLAMP_NONE;

        VulkanSampler sampler;
        if (vkCreateSampler(device_, &info, nullptr, &sampler.sampler) != VK_SUCCESS)
        {
            log("createSampler: could not create the sampler");
            return SamplerHandle();
        }
        setName(VK_OBJECT_TYPE_SAMPLER, (std::uint64_t) sampler.sampler, desc.debugName);
        return handleCast<SamplerHandle>(samplers_.insert(sampler));
    }

    void updateTexture(TextureHandle handle, std::uint32_t mip, std::uint32_t layer,
            const void* data) override
    {
        const VulkanTexture* texture = textures_.get(handleCast<TextureSlotHandle>(handle));
        if (passActive_ || !texture || !data || isDepthFormat(texture->format) ||
                texture->samples > 1 || mip >= texture->mipLevels ||
                (texture->type != TextureType::Texture3D && layer >= layerCount(*texture, mip)))
        {
            log("updateTexture: invalid handle, mip or layer, or called inside a render pass");
            return;
        }
        recordUpload(*texture, mip, layer, data);
    }

    void updateTextureRegion(TextureHandle handle, const TextureRegion& region,
            const void* data) override
    {
        const VulkanTexture* texture = textures_.get(handleCast<TextureSlotHandle>(handle));
        if (passActive_ || !texture || !data || isDepthFormat(texture->format) ||
                texture->samples > 1 || region.mip >= texture->mipLevels ||
                region.layer >= layerCount(*texture, region.mip) ||
                !validRegion(texture->format, mipSize(texture->width, region.mip),
                        mipSize(texture->height, region.mip), region))
        {
            log("updateTextureRegion: invalid handle, mip, layer or rectangle, or called inside a "
                "render pass");
            return;
        }
        const bool volume = texture->type == TextureType::Texture3D;
        recordCopy(*texture, region.mip, volume ? 0 : region.layer, region.x, region.y,
                volume ? region.layer : 0, region.width, region.height, 1, data);
    }

    void generateMipmaps(TextureHandle handle) override
    {
        const VulkanTexture* texture = textures_.get(handleCast<TextureSlotHandle>(handle));
        if (passActive_ || !texture || isDepthFormat(texture->format) || texture->samples > 1 ||
                isCompressedFormat(texture->format))
        {
            log("generateMipmaps: invalid handle or format, or called inside a render pass");
            return;
        }
        recordMipmaps(*texture);
    }

    void destroy(BufferHandle handle) override
    {
        const BufferSlot slot = handleCast<BufferSlot>(handle);
        const VulkanBuffer* buffer = buffers_.get(slot);
        if (!buffer) return;
        for (std::uint32_t i = 0; i < buffer->versionCount; ++i)
        {
            Garbage item;
            item.frame = frameNumber_;
            item.buffer = buffer->versions[i].buffer;
            item.allocation = buffer->versions[i].allocation;
            garbage_.push_back(item);
        }
        buffers_.erase(slot);
    }

    void destroy(ShaderHandle handle) override
    {
        const ShaderSlot slot = handleCast<ShaderSlot>(handle);
        const VulkanShader* shader = shaders_.get(slot);
        if (!shader) return;
        vkDestroyShaderModule(device_, shader->module, nullptr);
        shaders_.erase(slot);
    }

    void destroy(PipelineHandle handle) override
    {
        const PipelineSlot slot = handleCast<PipelineSlot>(handle);
        const VulkanPipeline* pipeline = pipelines_.get(slot);
        if (!pipeline) return;
        Garbage item;
        item.frame = frameNumber_;
        item.pipeline = pipeline->pipeline;
        item.layout = pipeline->layout;
        item.setLayout = pipeline->setLayout;
        item.textureSetLayout = pipeline->textureSetLayout;
        item.storageSetLayout = pipeline->storageSetLayout;
        item.imageSetLayout = pipeline->imageSetLayout;
        garbage_.push_back(item);
        pipelines_.erase(slot);
    }

    void destroy(TextureHandle handle) override
    {
        const TextureSlotHandle slot = handleCast<TextureSlotHandle>(handle);
        const VulkanTexture* texture = textures_.get(slot);
        if (!texture) return;
        for (size_t i = 0; i < texture->attachmentViews.size(); ++i)
        {
            Garbage view;
            view.frame = frameNumber_;
            view.view = texture->attachmentViews[i].view;
            garbage_.push_back(view);
        }
        for (std::uint32_t i = 0; i < VulkanTexture::kMaxMipViews; ++i)
        {
            if (!texture->mipViews[i]) continue;
            Garbage view;
            view.frame = frameNumber_;
            view.view = texture->mipViews[i];
            garbage_.push_back(view);
        }
        Garbage item;
        item.frame = frameNumber_;
        item.image = texture->image;
        item.view = texture->view;
        item.allocation = texture->allocation;
        garbage_.push_back(item);
        textures_.erase(slot);
    }

    void destroy(SamplerHandle handle) override
    {
        const SamplerSlotHandle slot = handleCast<SamplerSlotHandle>(handle);
        const VulkanSampler* sampler = samplers_.get(slot);
        if (!sampler) return;
        Garbage item;
        item.frame = frameNumber_;
        item.sampler = sampler->sampler;
        garbage_.push_back(item);
        samplers_.erase(slot);
    }

    QueryHandle createQuery(QueryType type) override
    {
        const bool time = type == QueryType::Time;
        if ((time && !caps_.timerQueries) || (!time && !caps_.occlusionQueries))
            return QueryHandle();

        VulkanQuery query;
        query.type = type;
        ct::Vector<std::uint32_t>& freeSlots = time ? freeTime_ : freeOcclusion_;
        std::uint32_t& top = time ? timeTop_ : occlusionTop_;
        const std::uint32_t step = time ? 2 : 1;
        for (std::uint32_t i = 0; i < kQuerySlots; ++i)
        {
            if (!freeSlots.empty())
            {
                query.slots[i] = freeSlots[freeSlots.size() - 1];
                freeSlots.pop_back();
            }
            else if (top + step <= kPoolQueries)
            {
                query.slots[i] = top;
                top += step;
            }
            else
            {
                log("createQuery: no query slots left");
                for (std::uint32_t used = 0; used < i; ++used)
                    freeSlots.push_back(query.slots[used]);
                return QueryHandle();
            }
        }
        return handleCast<QueryHandle>(queries_.insert(query));
    }

    void destroy(QueryHandle handle) override
    {
        const QuerySlotHandle slot = handleCast<QuerySlotHandle>(handle);
        const VulkanQuery* query = queries_.get(slot);
        if (!query) return;
        if (query->active >= 0 && query->type == QueryType::Occlusion) endQuery(handle);
        for (std::uint32_t i = 0; i < kQuerySlots; ++i)
        {
            Garbage item;
            item.frame = frameNumber_;
            item.querySlot = query->slots[i];
            item.queryKind = query->type == QueryType::Time ? 2 : 1;
            garbage_.push_back(item);
        }
        queries_.erase(slot);
    }

    void beginQuery(QueryHandle handle) override
    {
        VulkanQuery* query = queries_.get(handleCast<QuerySlotHandle>(handle));
        if (!query || !frameReady_ || query->active >= 0 ||
                (query->type == QueryType::Occlusion && (!passActive_ || occlusionActive_)))
        {
            log("beginQuery: invalid or already open query, or an occlusion query outside a "
                "render pass");
            return;
        }
        const std::uint32_t slot = query->next;
        query->next = (slot + 1) % kQuerySlots;
        query->active = static_cast<std::int32_t>(slot);
        query->sequence[slot] = 0;

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        const std::uint32_t index = query->slots[slot];
        if (query->type == QueryType::Occlusion)
        {
            vkResetQueryPool(device_, occlusionPool_, index, 1);
            vkCmdBeginQuery(commands, occlusionPool_, index, 0);
            occlusionActive_ = true;
            openOcclusion_ = handle;
        }
        else
        {
            vkResetQueryPool(device_, timePool_, index, 2);
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timePool_, index);
        }
    }

    void endQuery(QueryHandle handle) override
    {
        VulkanQuery* query = queries_.get(handleCast<QuerySlotHandle>(handle));
        if (!query || query->active < 0 || !frameReady_)
        {
            log("endQuery: the query is not open");
            return;
        }
        const std::uint32_t slot = static_cast<std::uint32_t>(query->active);
        query->active = -1;
        query->sequence[slot] = ++querySequence_;

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        const std::uint32_t index = query->slots[slot];
        if (query->type == QueryType::Occlusion)
        {
            vkCmdEndQuery(commands, occlusionPool_, index);
            occlusionActive_ = false;
            openOcclusion_ = QueryHandle();
        }
        else
            vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timePool_,
                    index + 1);
    }

    bool queryResult(QueryHandle handle, std::uint64_t* result) override
    {
        const VulkanQuery* query = queries_.get(handleCast<QuerySlotHandle>(handle));
        if (!query || !result) return false;

        const bool time = query->type == QueryType::Time;
        const std::uint32_t count = time ? 2 : 1;
        std::uint64_t bestSequence = 0;
        bool found = false;
        for (std::uint32_t slot = 0; slot < kQuerySlots; ++slot)
        {
            if (query->sequence[slot] == 0 || query->sequence[slot] < bestSequence) continue;
            std::uint64_t data[4] = { 0, 0, 0, 0 };
            const VkResult status = vkGetQueryPoolResults(device_,
                    time ? timePool_ : occlusionPool_, query->slots[slot], count, sizeof(data),
                    data, sizeof(std::uint64_t) * 2,
                    VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (status != VK_SUCCESS && status != VK_NOT_READY) continue;
            if (data[1] == 0 || (time && data[3] == 0)) continue;

            found = true;
            bestSequence = query->sequence[slot];
            if (time)
                *result = static_cast<std::uint64_t>(
                        static_cast<double>(data[2] - data[0]) * timestampPeriod_);
            else
                *result = data[0] ? 1 : 0;
        }
        return found;
    }

    void beginFrame() override
    {
        if (frameReady_)
        {
            log("beginFrame: the previous frame was not presented");
            return;
        }
        frameSubmitted_ = false;
        for (VulkanWindow* extra: swapchains_) extra->acquired = false;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        window_->platform.framebufferSize(window_->platform.user, &width, &height);
        if (width == 0 || height == 0) return;
        if (!window_->swapchain || width != window_->requestedWidth ||
                height != window_->requestedHeight)
        {
            window_->requestedWidth = width;
            window_->requestedHeight = height;
            if (!rebuildSwapchain()) return;
        }

        Frame& frame = frames_[frameIndex_];
        vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
        waitedFrame_ = frameNumber_;
        collectGarbage(false);
        vkResetDescriptorPool(device_, descriptorPools_[frameIndex_], 0);
        lastSet_ = VK_NULL_HANDLE;
        lastTextureSet_ = VK_NULL_HANDLE;

        const VkResult acquired = vkAcquireNextImageKHR(device_, window_->swapchain, UINT64_MAX,
                frame.imageAvailable, VK_NULL_HANDLE, &window_->imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
        {
            rebuildSwapchain();
            return;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        {
            log("Vulkan: could not acquire a swapchain image");
            return;
        }

        vkResetFences(device_, 1, &frame.inFlight);
        vkResetCommandBuffer(frame.commands, 0);

        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(frame.commands, &begin);
        frameReady_ = true;
    }

    void beginRenderPass(const RenderPassDesc& desc) override
    {
        passActive_ = false;
        passOffscreen_ = desc.colorCount > 0 || desc.depth.texture.valid();
        passTargetCount_ = 0;
        if (computeActive_)
        {
            log("beginRenderPass: a compute pass is still open");
            return;
        }
        if (!frameReady_ && !passOffscreen_) return;
        if (!frameReady_)
        {
            log("beginRenderPass: no frame in progress");
            return;
        }

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        VkRenderingAttachmentInfo colors[RenderPassDesc::kMaxColorTargets] = {};
        VkRenderingAttachmentInfo depth = {};
        VkRenderingInfo rendering = {};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.layerCount = 1;
        TargetFormats formats;
        bool hasStencil = !passOffscreen_;

        if (passOffscreen_)
        {
            if (desc.colorCount > RenderPassDesc::kMaxColorTargets)
            {
                log("beginRenderPass: invalid or unsupported render targets");
                return;
            }
            VkImageView views[RenderPassDesc::kMaxColorTargets + 1] = {};
            VkImageView resolveViews[RenderPassDesc::kMaxColorTargets + 1] = {};
            formats.window = false;
            formats.colorCount = desc.colorCount;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            for (std::uint32_t i = 0; i <= desc.colorCount; ++i)
            {
                const bool isDepth = i == desc.colorCount;
                const RenderTarget& target = isDepth ? desc.depth : desc.colors[i];
                if (isDepth && !target.texture.valid()) continue;

                VulkanTexture* texture =
                        textures_.get(handleCast<TextureSlotHandle>(target.texture));
                if (!texture || !(texture->usage & kTextureRenderTarget) ||
                        isDepthFormat(texture->format) != isDepth ||
                        target.mip >= texture->mipLevels ||
                        target.layer >= layerCount(*texture, target.mip) ||
                        (passTargetCount_ > 0 && texture->samples != formats.samples))
                {
                    log("beginRenderPass: invalid or unsupported render targets");
                    passTargetCount_ = 0;
                    return;
                }
                formats.samples = texture->samples;
                views[i] = attachmentView(*texture, target.mip, target.layer);
                if (!views[i])
                {
                    log("beginRenderPass: could not create a view of the render target");
                    passTargetCount_ = 0;
                    return;
                }
                width = mipSize(texture->width, target.mip);
                height = mipSize(texture->height, target.mip);
                if (isDepth) formats.depth = texture->format;
                else
                    formats.colors[i] = texture->format;

                PassTarget& pass = passTargets_[passTargetCount_++];
                pass.image = texture->image;
                pass.aspect = aspectOf(texture->format);
                pass.mip = target.mip;
                pass.layer = texture->type == TextureType::Texture3D ? 0 : target.layer;
                pass.layout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                      : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            for (std::uint32_t i = 0; i <= desc.colorCount; ++i)
            {
                const bool isDepth = i == desc.colorCount;
                const RenderTarget& target = isDepth ? desc.depthResolve : desc.resolves[i];
                if (!target.texture.valid()) continue;

                VulkanTexture* texture =
                        textures_.get(handleCast<TextureSlotHandle>(target.texture));
                const TextureFormat format = isDepth ? formats.depth : formats.colors[i];
                if (!texture || !(texture->usage & kTextureRenderTarget) || formats.samples < 2 ||
                        texture->samples != 1 || texture->format != format ||
                        target.mip >= texture->mipLevels ||
                        target.layer >= layerCount(*texture, target.mip) ||
                        mipSize(texture->width, target.mip) != width ||
                        mipSize(texture->height, target.mip) != height)
                {
                    log("beginRenderPass: invalid resolve targets");
                    passTargetCount_ = 0;
                    return;
                }
                resolveViews[i] = attachmentView(*texture, target.mip, target.layer);
                if (!resolveViews[i])
                {
                    log("beginRenderPass: could not create a view of the render target");
                    passTargetCount_ = 0;
                    return;
                }
                PassTarget& pass = passTargets_[passTargetCount_++];
                pass.image = texture->image;
                pass.aspect = aspectOf(texture->format);
                pass.mip = target.mip;
                pass.layer = texture->type == TextureType::Texture3D ? 0 : target.layer;
                pass.layout = isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                      : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
            for (std::uint32_t i = 0; i < passTargetCount_; ++i)
                imageBarrier(commands, passTargets_[i].image, passTargets_[i].aspect,
                        passTargets_[i].mip, 1, passTargets_[i].layer, 1,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, passTargets_[i].layout);

            for (std::uint32_t i = 0; i < desc.colorCount; ++i)
            {
                colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colors[i].imageView = views[i];
                colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colors[i].loadOp = toLoadOp(desc.colorLoad);
                colors[i].storeOp = desc.colorStore == StoreOp::Store
                                            ? VK_ATTACHMENT_STORE_OP_STORE
                                            : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                if (resolveViews[i])
                {
                    colors[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                    colors[i].resolveImageView = resolveViews[i];
                    colors[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                }
                for (int c = 0; c < 4; ++c)
                    colors[i].clearValue.color.float32[c] = desc.clearColor[c];
            }
            rendering.colorAttachmentCount = desc.colorCount;
            rendering.pColorAttachments = colors;
            if (desc.depth.texture.valid())
            {
                depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depth.imageView = views[desc.colorCount];
                depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depth.loadOp = toLoadOp(desc.depthLoad);
                depth.storeOp = desc.depthStore == StoreOp::Store
                                        ? VK_ATTACHMENT_STORE_OP_STORE
                                        : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depth.clearValue.depthStencil.depth = desc.clearDepth;
                if (resolveViews[desc.colorCount])
                {
                    depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                    depth.resolveImageView = resolveViews[desc.colorCount];
                    depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                }
                rendering.pDepthAttachment = &depth;
                hasStencil = formats.depth == TextureFormat::Depth24Stencil8;
            }
            passWidth_ = width;
            passHeight_ = height;
        }
        else
        {
            if (desc.swapchain.valid() && !useSwapchain(desc.swapchain)) return;
            SwapchainImage& target = window_->images[window_->imageIndex];
            if (target.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
                transition(commands, target,
                        desc.colorLoad == LoadOp::Load ? target.layout : VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            transitionDepth(commands);

            colors[0].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[0].imageView = target.view;
            colors[0].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colors[0].loadOp = toLoadOp(desc.colorLoad);
            colors[0].storeOp = desc.colorStore == StoreOp::Store
                                        ? VK_ATTACHMENT_STORE_OP_STORE
                                        : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            for (int c = 0; c < 4; ++c) colors[0].clearValue.color.float32[c] = desc.clearColor[c];

            depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depth.imageView = window_->depthView;
            depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depth.loadOp = toLoadOp(desc.depthLoad);
            depth.storeOp = desc.depthStore == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                                              : VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth.clearValue.depthStencil.depth = desc.clearDepth;

            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachments = colors;
            rendering.pDepthAttachment = &depth;
            passWidth_ = window_->extent.width;
            passHeight_ = window_->extent.height;
            window_ = &mainWindow_;
        }

        VkRenderingAttachmentInfo stencil = depth;
        stencil.loadOp = toLoadOp(desc.stencilLoad);
        stencil.clearValue.depthStencil.stencil = desc.clearStencil;
        if (hasStencil) rendering.pStencilAttachment = &stencil;

        rendering.renderArea.extent.width = passWidth_;
        rendering.renderArea.extent.height = passHeight_;
        vkCmdBeginRendering(commands, &rendering);

        vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, 0);

        passActive_ = true;
        passFormats_ = formats;
        pipeline_ = PipelineHandle();
        boundPipeline_ = VK_NULL_HANDLE;
        indexBuffer_ = BufferHandle();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxVertexBuffers; ++i)
        {
            vertexBuffers_[i] = BufferHandle();
            vertexOffsets_[i] = 0;
        }
        for (std::uint32_t i = 0; i < kMaxUniformSlots; ++i) uniformBindings_[i] = UniformBinding();
        for (std::uint32_t i = 0; i < kMaxTextureSlots; ++i) textureBindings_[i] = TextureSlot();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxStorageBuffers; ++i)
            storageBindings_[i] = UniformBinding();
        storageDirty_ = true;
        vertexDirty_ = true;
        indexDirty_ = true;
        uniformDirty_ = true;
        textureDirty_ = true;

        Viewport viewport;
        viewport.width = static_cast<float>(passWidth_);
        viewport.height = static_cast<float>(passHeight_);
        setViewport(viewport);
        Rect scissor;
        scissor.width = passWidth_;
        scissor.height = passHeight_;
        setScissor(scissor);
    }

    void setViewport(const Viewport& viewport) override
    {
        if (!passActive_) return;
        VkViewport converted;
        converted.x = viewport.x;
        converted.width = viewport.width;
        converted.minDepth = viewport.minDepth;
        converted.maxDepth = viewport.maxDepth;
        if (passOffscreen_)
        {
            converted.y = static_cast<float>(passHeight_) - (viewport.y + viewport.height);
            converted.height = viewport.height;
        }
        else
        {
            converted.y = viewport.y + viewport.height;
            converted.height = -viewport.height;
        }
        vkCmdSetViewport(frames_[frameIndex_].commands, 0, 1, &converted);
    }

    void setScissor(const Rect& rect) override
    {
        if (!passActive_) return;
        std::int64_t left = rect.x < 0 ? 0 : rect.x;
        std::int64_t top = rect.y < 0 ? 0 : rect.y;
        std::int64_t right = static_cast<std::int64_t>(rect.x) + rect.width;
        std::int64_t bottom = static_cast<std::int64_t>(rect.y) + rect.height;
        if (right > passWidth_) right = passWidth_;
        if (bottom > passHeight_) bottom = passHeight_;
        if (right < left) right = left;
        if (bottom < top) bottom = top;

        VkRect2D scissor;
        scissor.offset.x = static_cast<std::int32_t>(left);
        scissor.offset.y = static_cast<std::int32_t>(passOffscreen_ ? passHeight_ - bottom : top);
        scissor.extent.width = static_cast<std::uint32_t>(right - left);
        scissor.extent.height = static_cast<std::uint32_t>(bottom - top);
        vkCmdSetScissor(frames_[frameIndex_].commands, 0, 1, &scissor);
    }

    void setStencilReference(std::uint32_t reference) override
    {
        if (!passActive_) return;
        vkCmdSetStencilReference(frames_[frameIndex_].commands, VK_STENCIL_FACE_FRONT_AND_BACK,
                reference);
    }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
    }

    void bindVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset) override
    {
        if (slot >= PipelineDesc::kMaxVertexBuffers)
        {
            log("bindVertexBuffer: slot out of range");
            return;
        }
        vertexBuffers_[slot] = handle;
        vertexOffsets_[slot] = offset;
        vertexDirty_ = true;
    }

    void bindIndexBuffer(BufferHandle handle) override
    {
        indexBuffer_ = handle;
        indexDirty_ = true;
    }

    void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Uniform || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= kMaxUniformSlots || offset % caps_.uniformBufferOffsetAlignment != 0)
        {
            log("bindUniformBuffer: invalid buffer handle, range, alignment or slot");
            return;
        }
        uniformBindings_[slot].handle = handle;
        uniformBindings_[slot].offset = offset;
        uniformBindings_[slot].size = size;
        uniformDirty_ = true;
    }

    void bindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) override
    {
        const VulkanTexture* bound = textures_.get(handleCast<TextureSlotHandle>(texture));
        if (slot >= kMaxTextureSlots || !bound || bound->samples > 1 ||
                !samplers_.contains(handleCast<SamplerSlotHandle>(sampler)))
        {
            log("bindTexture: invalid texture handle, sampler handle or slot");
            return;
        }
        textureBindings_[slot].texture = texture;
        textureBindings_[slot].sampler = sampler;
        textureDirty_ = true;
    }

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
            std::uint32_t instanceCount) override
    {
        if (!prepareDraw(static_cast<std::uint64_t>(firstVertex) + vertexCount, instanceCount))
            return;
        vkCmdDraw(frames_[frameIndex_].commands, vertexCount, instanceCount, firstVertex, 0);
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
            std::uint32_t instanceCount) override
    {
        VulkanBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexed: no valid index buffer bound");
            return;
        }
        const bool wide = indices->indexFormat == IndexFormat::UInt32;
        const std::uint64_t indexSize = wide ? 4 : 2;
        if ((static_cast<std::uint64_t>(firstIndex) + indexCount) * indexSize > indices->size)
        {
            log("drawIndexed: index range is outside the index buffer");
            return;
        }
        if (!prepareDraw(0, instanceCount)) return;

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        BufferVersion& version = indices->versions[indices->current];
        version.lastUsedFrame = frameNumber_;
        if (indexDirty_)
        {
            vkCmdBindIndexBuffer(commands, version.buffer, 0,
                    wide ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            indexDirty_ = false;
        }
        vkCmdDrawIndexed(commands, indexCount, instanceCount, firstIndex, 0, 0);
    }

    void drawIndirect(BufferHandle handle, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride) override
    {
        VulkanBuffer* arguments =
                indirectBuffer(handle, offset, drawCount, stride, sizeof(DrawIndirectCommand));
        if (!arguments || !prepareDraw(0, 1)) return;

        const std::uint32_t step = stride ? stride : sizeof(DrawIndirectCommand);
        BufferVersion& version = arguments->versions[arguments->current];
        version.lastUsedFrame = frameNumber_;
        VkCommandBuffer commands = frames_[frameIndex_].commands;
        if (multiDrawIndirect_ || drawCount == 1)
            vkCmdDrawIndirect(commands, version.buffer, offset, drawCount, step);
        else
            for (std::uint32_t i = 0; i < drawCount; ++i)
                vkCmdDrawIndirect(commands, version.buffer, offset + i * step, 1, step);
    }

    void drawIndexedIndirect(BufferHandle handle, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride) override
    {
        VulkanBuffer* arguments = indirectBuffer(handle, offset, drawCount, stride,
                sizeof(DrawIndexedIndirectCommand));
        if (!arguments) return;
        VulkanBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexedIndirect: no valid index buffer bound");
            return;
        }
        if (!prepareDraw(0, 1)) return;

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        BufferVersion& indexVersion = indices->versions[indices->current];
        indexVersion.lastUsedFrame = frameNumber_;
        if (indexDirty_)
        {
            vkCmdBindIndexBuffer(commands, indexVersion.buffer, 0,
                    indices->indexFormat == IndexFormat::UInt32 ? VK_INDEX_TYPE_UINT32
                                                                : VK_INDEX_TYPE_UINT16);
            indexDirty_ = false;
        }
        const std::uint32_t step = stride ? stride : sizeof(DrawIndexedIndirectCommand);
        BufferVersion& version = arguments->versions[arguments->current];
        version.lastUsedFrame = frameNumber_;
        if (multiDrawIndirect_ || drawCount == 1)
            vkCmdDrawIndexedIndirect(commands, version.buffer, offset, drawCount, step);
        else
            for (std::uint32_t i = 0; i < drawCount; ++i)
                vkCmdDrawIndexedIndirect(commands, version.buffer, offset + i * step, 1, step);
    }

    void beginComputePass() override
    {
        if (passActive_ || computeActive_ || !frameReady_)
        {
            log("beginComputePass: needs a frame in progress and no other pass open");
            return;
        }
        computeActive_ = true;
        pipeline_ = PipelineHandle();
        boundPipeline_ = VK_NULL_HANDLE;
        for (std::uint32_t i = 0; i < kMaxUniformSlots; ++i) uniformBindings_[i] = UniformBinding();
        for (std::uint32_t i = 0; i < kMaxTextureSlots; ++i) textureBindings_[i] = TextureSlot();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxStorageBuffers; ++i)
            storageBindings_[i] = UniformBinding();
        for (std::uint32_t i = 0; i < ComputePipelineDesc::kMaxStorageTextures; ++i)
            imageBindings_[i] = ImageSlot();
        uniformDirty_ = true;
        textureDirty_ = true;
        storageDirty_ = true;
        imageDirty_ = true;
    }

    void bindStorageBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Storage || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= PipelineDesc::kMaxStorageBuffers ||
                offset % caps_.storageBufferOffsetAlignment != 0)
        {
            log("bindStorageBuffer: invalid buffer handle, range, alignment or slot");
            return;
        }
        storageBindings_[slot].handle = handle;
        storageBindings_[slot].offset = offset;
        storageBindings_[slot].size = size;
        storageDirty_ = true;
        if (passActive_) storageUsedInPass_ = true;
    }

    void bindStorageTexture(std::uint32_t slot, TextureHandle handle, std::uint32_t mip,
            StorageAccess) override
    {
        const VulkanTexture* texture = textures_.get(handleCast<TextureSlotHandle>(handle));
        if (!texture || !(texture->usage & kTextureStorage) || mip >= texture->mipLevels ||
                slot >= ComputePipelineDesc::kMaxStorageTextures)
        {
            log("bindStorageTexture: invalid slot or mip, or the texture was not created with "
                "kTextureStorage");
            return;
        }
        imageBindings_[slot].texture = handle;
        imageBindings_[slot].mip = mip;
        imageDirty_ = true;
    }

    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override
    {
        if (!computeActive_)
        {
            log("dispatch: no active compute pass or no compute pipeline bound in this pass");
            return;
        }
        VkCommandBuffer commands = frames_[frameIndex_].commands;
        const VulkanPipeline* pipeline = prepareDispatch(commands);
        if (!pipeline) return;
        vkCmdDispatch(commands, x, y, z);
        finishDispatch(*pipeline, commands);
    }

    void dispatchIndirect(BufferHandle handle, std::uint32_t offset) override
    {
        VulkanBuffer* arguments = buffers_.get(handleCast<BufferSlot>(handle));
        if (!computeActive_ || !arguments ||
                (arguments->usage != BufferUsage::Indirect &&
                        arguments->usage != BufferUsage::Storage) ||
                offset % 4 != 0 ||
                static_cast<std::uint64_t>(offset) + sizeof(DispatchIndirectCommand) >
                        arguments->size)
        {
            log("dispatchIndirect: invalid buffer or offset, or no active compute pass");
            return;
        }
        VkCommandBuffer commands = frames_[frameIndex_].commands;
        const VulkanPipeline* pipeline = prepareDispatch(commands);
        if (!pipeline) return;
        BufferVersion& version = arguments->versions[arguments->current];
        version.lastUsedFrame = frameNumber_;
        vkCmdDispatchIndirect(commands, version.buffer, offset);
        finishDispatch(*pipeline, commands);
    }

    void endComputePass() override { computeActive_ = false; }

    void endRenderPass() override
    {
        if (!passActive_) return;
        if (occlusionActive_)
        {
            log("endRenderPass: an occlusion query is still open");
            endQuery(openOcclusion_);
        }
        passActive_ = false;
        VkCommandBuffer commands = frames_[frameIndex_].commands;
        vkCmdEndRendering(commands);
        if (storageUsedInPass_)
        {
            VkMemoryBarrier2 barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            barrier.srcStageMask =
                    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            VkDependencyInfo dependency = {};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.memoryBarrierCount = 1;
            dependency.pMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commands, &dependency);
            storageUsedInPass_ = false;
        }
        for (std::uint32_t i = 0; i < passTargetCount_; ++i)
            imageBarrier(commands, passTargets_[i].image, passTargets_[i].aspect,
                    passTargets_[i].mip, 1, passTargets_[i].layer, 1, passTargets_[i].layout,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        passTargetCount_ = 0;
    }

    void endFrame() override
    {
        if (!frameReady_) return;

        Frame& frame = frames_[frameIndex_];
        VkSemaphore waits[kMaxWindows];
        VkPipelineStageFlags stages[kMaxWindows];
        VkSemaphore signals[kMaxWindows];
        std::uint32_t signalCount = 0;
        std::uint32_t waitCount = pendingWaits(waits, stages);
        SwapchainImage& target = mainWindow_.images[mainWindow_.imageIndex];
        transition(frame.commands, target, target.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        signals[signalCount++] = target.renderFinished;
        for (VulkanWindow* extra: swapchains_)
        {
            if (!extra->acquired) continue;
            SwapchainImage& image = extra->images[extra->imageIndex];
            transition(frame.commands, image, image.layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            signals[signalCount++] = image.renderFinished;
        }
        vkEndCommandBuffer(frame.commands);

        VkCommandBuffer buffers[2];
        std::uint32_t count = 0;
        if (pendingTransfer_)
        {
            vkEndCommandBuffer(pendingTransfer_);
            buffers[count++] = pendingTransfer_;
        }
        buffers[count++] = frame.commands;

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = waitCount;
        submit.pWaitSemaphores = waits;
        submit.pWaitDstStageMask = stages;
        submit.commandBufferCount = count;
        submit.pCommandBuffers = buffers;
        submit.signalSemaphoreCount = signalCount;
        submit.pSignalSemaphores = signals;
        if (vkQueueSubmit(queue_, 1, &submit, frame.inFlight) != VK_SUCCESS)
            log("Vulkan: could not submit the frame");
        stagingBytes_ = 0;

        if (pendingTransfer_)
        {
            Garbage item;
            item.frame = frameNumber_;
            item.commands = pendingTransfer_;
            garbage_.push_back(item);
            pendingTransfer_ = VK_NULL_HANDLE;
        }
    }

    void present() override
    {
        if (!frameReady_) return;
        frameReady_ = false;

        VulkanWindow* windows[kMaxWindows];
        VkSwapchainKHR chains[kMaxWindows];
        std::uint32_t indices[kMaxWindows];
        VkSemaphore finished[kMaxWindows];
        VkResult results[kMaxWindows];
        std::uint32_t count = 0;
        windows[count++] = &mainWindow_;
        for (VulkanWindow* extra: swapchains_)
            if (extra->acquired && count < kMaxWindows) windows[count++] = extra;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            chains[i] = windows[i]->swapchain;
            indices[i] = windows[i]->imageIndex;
            finished[i] = windows[i]->images[windows[i]->imageIndex].renderFinished;
            results[i] = VK_SUCCESS;
            windows[i]->acquired = false;
        }

        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = count;
        info.pWaitSemaphores = finished;
        info.swapchainCount = count;
        info.pSwapchains = chains;
        info.pImageIndices = indices;
        info.pResults = results;
        vkQueuePresentKHR(queue_, &info);
        frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
        ++frameNumber_;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (results[i] != VK_ERROR_OUT_OF_DATE_KHR) continue;
            window_ = windows[i];
            rebuildSwapchain();
        }
        window_ = &mainWindow_;
    }

    SwapchainHandle createSwapchain(const SwapchainDesc& desc) override
    {
        const VulkanPlatform& platform = desc.vulkan;
        if (!caps_.multipleWindows || !platform.createSurface || !platform.framebufferSize ||
                swapchains_.size() + 2 > kMaxWindows)
        {
            log("createSwapchain: several windows are not supported, too many windows, or "
                "platform functions are missing");
            return SwapchainHandle();
        }
        std::uint64_t surface = 0;
        if (!platform.createSurface(platform.user, instance_, &surface))
        {
            log("createSwapchain: the platform could not create a surface");
            return SwapchainHandle();
        }
        VkBool32 presents = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, queueFamily_, (VkSurfaceKHR) surface,
                &presents);
        if (!presents)
        {
            vkDestroySurfaceKHR(instance_, (VkSurfaceKHR) surface, nullptr);
            log("createSwapchain: the device cannot present to this window");
            return SwapchainHandle();
        }

        VulkanWindow* window = new VulkanWindow;
        window->platform = platform;
        window->surface = (VkSurfaceKHR) surface;
        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            vkCreateSemaphore(device_, &semaphore, nullptr, &window->imageAvailable[i]);
        return handleCast<SwapchainHandle>(swapchains_.insert(window));
    }

    void destroy(SwapchainHandle handle) override
    {
        const SwapchainSlot slot = handleCast<SwapchainSlot>(handle);
        VulkanWindow** window = swapchains_.get(slot);
        if (!window) return;
        if ((*window)->acquired)
        {
            log("destroy: the swapchain was drawn to in this frame; destroy it after present");
            return;
        }
        vkDeviceWaitIdle(device_);
        destroyWindow(*window);
        swapchains_.erase(slot);
    }

    void destroyWindow(VulkanWindow* window)
    {
        window_ = window;
        destroySwapchain();
        window_ = &mainWindow_;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            if (window->imageAvailable[i])
                vkDestroySemaphore(device_, window->imageAvailable[i], nullptr);
        vkDestroySurfaceKHR(instance_, window->surface, nullptr);
        delete window;
    }

    std::uint32_t pendingWaits(VkSemaphore* waits, VkPipelineStageFlags* stages)
    {
        std::uint32_t count = 0;
        if (!frameSubmitted_)
        {
            waits[count] = frames_[frameIndex_].imageAvailable;
            stages[count++] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        for (VulkanWindow* extra: swapchains_)
        {
            if (!extra->acquired || extra->waited || count >= kMaxWindows) continue;
            extra->waited = true;
            waits[count] = extra->imageAvailable[frameIndex_];
            stages[count++] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        return count;
    }

    bool useSwapchain(SwapchainHandle handle)
    {
        VulkanWindow** slot = swapchains_.get(handleCast<SwapchainSlot>(handle));
        if (!slot)
        {
            log("beginRenderPass: invalid swapchain");
            return false;
        }
        window_ = *slot;
        if (window_->acquired) return true;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        window_->platform.framebufferSize(window_->platform.user, &width, &height);
        bool ready = width > 0 && height > 0;
        if (ready && (!window_->swapchain || width != window_->requestedWidth ||
                             height != window_->requestedHeight))
        {
            window_->requestedWidth = width;
            window_->requestedHeight = height;
            ready = rebuildSwapchain();
        }
        for (int attempt = 0; ready && attempt < 2; ++attempt)
        {
            const VkResult acquired = vkAcquireNextImageKHR(device_, window_->swapchain, UINT64_MAX,
                    window_->imageAvailable[frameIndex_], VK_NULL_HANDLE, &window_->imageIndex);
            if (acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR)
            {
                window_->acquired = true;
                window_->waited = false;
                return true;
            }
            ready = acquired == VK_ERROR_OUT_OF_DATE_KHR && rebuildSwapchain();
        }
        window_ = &mainWindow_;
        log("beginRenderPass: the swapchain is not ready");
        return false;
    }

    bool readPixels(const RenderTarget& source, const Rect& rect, void* rgba) override
    {
        ReadSource read;
        if (!rgba || !readSource(source, rect, read)) return false;

        flush();

        VulkanBuffer staging;
        staging.size = rect.width * rect.height * 4;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!createVersion(staging, nullptr)) return false;

        bool done = beginImmediate();
        if (done)
        {
            recordRead(immediateCommands_, read, rect, staging.versions[0].buffer);
            done = endImmediate();
        }
        if (done)
            copyRows(static_cast<const unsigned char*>(staging.versions[0].mapped), rect.width,
                    rect.height, read.fromTexture, read.swapRedBlue, rgba);
        vkDestroyBuffer(device_, staging.versions[0].buffer, nullptr);
        memory_.free(staging.versions[0].allocation);
        return done;
    }

    ReadbackHandle requestReadback(const RenderTarget& source, const Rect& rect) override
    {
        ReadSource read;
        if (!readSource(source, rect, read)) return ReadbackHandle();

        VulkanBuffer staging;
        staging.size = rect.width * rect.height * 4;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!createVersion(staging, nullptr))
        {
            log("requestReadback: could not create the buffer");
            return ReadbackHandle();
        }
        recordRead(transferCommands(), read, rect, staging.versions[0].buffer);

        VulkanReadback readback;
        readback.buffer = staging.versions[0].buffer;
        readback.allocation = staging.versions[0].allocation;
        readback.mapped = staging.versions[0].mapped;
        readback.frame = frameNumber_;
        readback.width = rect.width;
        readback.height = rect.height;
        readback.flip = read.fromTexture;
        readback.swapRedBlue = read.swapRedBlue;
        return handleCast<ReadbackHandle>(readbacks_.insert(readback));
    }

    bool readbackResult(ReadbackHandle handle, void* rgba) override
    {
        const VulkanReadback* readback = readbacks_.get(handleCast<ReadbackSlotHandle>(handle));
        if (!readback || !rgba) return false;

        bool ready = readback->frame + kFramesInFlight <= waitedFrame_;
        if (!ready && frameNumber_ > readback->frame &&
                frameNumber_ < readback->frame + kFramesInFlight)
            ready = vkGetFenceStatus(device_,
                            frames_[readback->frame % kFramesInFlight].inFlight) == VK_SUCCESS;
        if (!ready) return false;
        if (readback->bytes) memcpy(rgba, readback->mapped, readback->bytes);
        else
            copyRows(static_cast<const unsigned char*>(readback->mapped), readback->width,
                    readback->height, readback->flip, readback->swapRedBlue, rgba);
        return true;
    }

    ReadbackHandle requestBufferReadback(BufferHandle sourceHandle, std::uint32_t offset,
            std::uint32_t size) override
    {
        VulkanBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        if (passActive_ || !source || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > source->size)
        {
            log("requestBufferReadback: invalid buffer or range, or called inside a render pass");
            return ReadbackHandle();
        }
        VulkanBuffer staging;
        staging.size = size;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!createVersion(staging, nullptr))
        {
            log("requestBufferReadback: could not create the buffer");
            return ReadbackHandle();
        }
        BufferVersion& version = source->versions[source->current];
        version.lastUsedFrame = frameNumber_;
        VkBufferCopy region = {};
        region.srcOffset = offset;
        region.size = size;
        vkCmdCopyBuffer(transferCommands(), version.buffer, staging.versions[0].buffer, 1, &region);

        VulkanReadback readback;
        readback.buffer = staging.versions[0].buffer;
        readback.allocation = staging.versions[0].allocation;
        readback.mapped = staging.versions[0].mapped;
        readback.frame = frameNumber_;
        readback.bytes = size;
        return handleCast<ReadbackHandle>(readbacks_.insert(readback));
    }

    bool readBuffer(BufferHandle sourceHandle, std::uint32_t offset, std::uint32_t size,
            void* data) override
    {
        VulkanBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        if (passActive_ || !source || !data || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > source->size)
        {
            log("readBuffer: invalid buffer or range, or called inside a render pass");
            return false;
        }
        flush();

        VulkanBuffer staging;
        staging.size = size;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (!createVersion(staging, nullptr)) return false;
        bool done = beginImmediate();
        if (done)
        {
            VkBufferCopy region = {};
            region.srcOffset = offset;
            region.size = size;
            vkCmdCopyBuffer(immediateCommands_, source->versions[source->current].buffer,
                    staging.versions[0].buffer, 1, &region);
            done = endImmediate();
        }
        if (done) memcpy(data, staging.versions[0].mapped, size);
        vkDestroyBuffer(device_, staging.versions[0].buffer, nullptr);
        memory_.free(staging.versions[0].allocation);
        return done;
    }

    void destroy(ReadbackHandle handle) override
    {
        const ReadbackSlotHandle slot = handleCast<ReadbackSlotHandle>(handle);
        const VulkanReadback* readback = readbacks_.get(slot);
        if (!readback) return;
        Garbage item;
        item.frame = frameNumber_;
        item.buffer = readback->buffer;
        item.allocation = readback->allocation;
        garbage_.push_back(item);
        readbacks_.erase(slot);
    }

    bool readSource(const RenderTarget& source, const Rect& rect, ReadSource& read)
    {
        if (passActive_ || rect.width == 0 || rect.height == 0 || rect.x < 0 || rect.y < 0)
        {
            log("readPixels: invalid rectangle, or called inside a render pass");
            return false;
        }
        if (!source.texture.valid())
        {
            const VulkanWindow* window = &mainWindow_;
            if (source.swapchain.valid())
            {
                VulkanWindow** slot = swapchains_.get(handleCast<SwapchainSlot>(source.swapchain));
                window = slot && (*slot)->acquired ? *slot : nullptr;
            }
            if (!frameReady_ || !window || !window->canRead ||
                    window->images[window->imageIndex].layout == VK_IMAGE_LAYOUT_UNDEFINED)
            {
                log("readPixels: the window can only be read during a frame, after drawing to it");
                return false;
            }
            read.image = window->images[window->imageIndex].image;
            read.layout = window->images[window->imageIndex].layout;
            read.width = window->extent.width;
            read.height = window->extent.height;
            read.swapRedBlue =
                    format_ == VK_FORMAT_B8G8R8A8_UNORM || format_ == VK_FORMAT_B8G8R8A8_SRGB;
        }
        else
        {
            const VulkanTexture* texture =
                    textures_.get(handleCast<TextureSlotHandle>(source.texture));
            if (!texture || texture->samples > 1 ||
                    (texture->format != TextureFormat::RGBA8 &&
                            texture->format != TextureFormat::RGBA8Srgb) ||
                    source.mip >= texture->mipLevels ||
                    source.layer >= layerCount(*texture, source.mip))
            {
                log("readPixels: the texture must be RGBA8 and the mip and layer must exist");
                return false;
            }
            read.fromTexture = true;
            read.image = texture->image;
            read.mip = source.mip;
            read.width = mipSize(texture->width, read.mip);
            read.height = mipSize(texture->height, read.mip);
            if (texture->type == TextureType::Texture3D)
                read.slice = static_cast<std::int32_t>(source.layer);
            else
                read.layer = source.layer;
        }
        if (static_cast<std::uint64_t>(rect.x) + rect.width > read.width ||
                static_cast<std::uint64_t>(rect.y) + rect.height > read.height)
        {
            log("readPixels: the rectangle is outside the source");
            return false;
        }
        return true;
    }

    static void recordRead(VkCommandBuffer commands, const ReadSource& read, const Rect& rect,
            VkBuffer buffer)
    {
        imageBarrier(commands, read.image, VK_IMAGE_ASPECT_COLOR_BIT, read.mip, 1, read.layer, 1,
                read.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = read.mip;
        copy.imageSubresource.baseArrayLayer = read.layer;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset.x = rect.x;
        copy.imageOffset.y = read.fromTexture
                                     ? static_cast<std::int32_t>(read.height) -
                                               (rect.y + static_cast<std::int32_t>(rect.height))
                                     : rect.y;
        copy.imageOffset.z = read.slice;
        copy.imageExtent.width = rect.width;
        copy.imageExtent.height = rect.height;
        copy.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer(commands, read.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer,
                1, &copy);
        imageBarrier(commands, read.image, VK_IMAGE_ASPECT_COLOR_BIT, read.mip, 1, read.layer, 1,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, read.layout);
    }

    static void copyRows(const unsigned char* pixels, std::uint32_t width, std::uint32_t height,
            bool flip, bool swapRedBlue, void* rgba)
    {
        unsigned char* out = static_cast<unsigned char*>(rgba);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
        for (std::uint32_t row = 0; row < height; ++row)
        {
            const unsigned char* from = pixels + (flip ? height - 1 - row : row) * rowBytes;
            unsigned char* to = out + row * rowBytes;
            for (std::uint32_t x = 0; x < width; ++x)
            {
                to[x * 4 + 0] = from[x * 4 + (swapRedBlue ? 2 : 0)];
                to[x * 4 + 1] = from[x * 4 + 1];
                to[x * 4 + 2] = from[x * 4 + (swapRedBlue ? 0 : 2)];
                to[x * 4 + 3] = from[x * 4 + 3];
            }
        }
    }

private:
    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    using BufferSlot = ct::Handle32<VulkanBuffer>;
    using ShaderSlot = ct::Handle32<VulkanShader>;
    using PipelineSlot = ct::Handle32<VulkanPipeline>;
    using TextureSlotHandle = ct::Handle32<VulkanTexture>;
    using SamplerSlotHandle = ct::Handle32<VulkanSampler>;

    void setName(VkObjectType type, std::uint64_t handle, const char* name) const
    {
        if (!setObjectName_ || !name) return;
        VkDebugUtilsObjectNameInfoEXT info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = name;
        setObjectName_(device_, &info);
    }

    bool createVersion(VulkanBuffer& buffer, const char* name, bool visible = true)
    {
        if (buffer.versionCount >= VulkanBuffer::kMaxVersions) return false;
        BufferVersion version;

        VkBufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = buffer.size;
        info.usage = buffer.vkUsage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &info, nullptr, &version.buffer) != VK_SUCCESS)
        {
            log("Vulkan: could not create a buffer");
            return false;
        }

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, version.buffer, &requirements);
        const VkMemoryPropertyFlags wanted =
                visible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (!memory_.allocate(requirements, wanted, VulkanMemoryKind::Buffer,
                    &version.allocation) ||
                vkBindBufferMemory(device_, version.buffer, version.allocation.memory,
                        version.allocation.offset) != VK_SUCCESS)
        {
            log("Vulkan: could not allocate buffer memory");
            vkDestroyBuffer(device_, version.buffer, nullptr);
            memory_.free(version.allocation);
            return false;
        }
        version.mapped = visible ? version.allocation.mapped : nullptr;
        setName(VK_OBJECT_TYPE_BUFFER, (std::uint64_t) version.buffer, name);
        buffer.versions[buffer.versionCount++] = version;
        return true;
    }

    void collectGarbage(bool all)
    {
        std::size_t kept = 0;
        for (std::size_t i = 0; i < garbage_.size(); ++i)
        {
            const Garbage& item = garbage_[i];
            if (!all && item.frame + kFramesInFlight > frameNumber_)
            {
                garbage_[kept++] = item;
                continue;
            }
            if (item.pipeline) vkDestroyPipeline(device_, item.pipeline, nullptr);
            if (item.layout) vkDestroyPipelineLayout(device_, item.layout, nullptr);
            if (item.setLayout) vkDestroyDescriptorSetLayout(device_, item.setLayout, nullptr);
            if (item.textureSetLayout)
                vkDestroyDescriptorSetLayout(device_, item.textureSetLayout, nullptr);
            if (item.storageSetLayout)
                vkDestroyDescriptorSetLayout(device_, item.storageSetLayout, nullptr);
            if (item.imageSetLayout)
                vkDestroyDescriptorSetLayout(device_, item.imageSetLayout, nullptr);
            if (item.buffer) vkDestroyBuffer(device_, item.buffer, nullptr);
            if (item.view) vkDestroyImageView(device_, item.view, nullptr);
            if (item.image) vkDestroyImage(device_, item.image, nullptr);
            if (item.sampler) vkDestroySampler(device_, item.sampler, nullptr);
            memory_.free(item.allocation);
            if (item.commands) vkFreeCommandBuffers(device_, commandPool_, 1, &item.commands);
            if (item.queryKind == 1) freeOcclusion_.push_back(item.querySlot);
            if (item.queryKind == 2) freeTime_.push_back(item.querySlot);
        }
        garbage_.resize(kept);
    }

    VkFormat textureFormat(TextureFormat format) const
    {
        return format == TextureFormat::Depth24Stencil8 ? depthStencilFormat_ : toVkFormat(format);
    }

    static void imageBarrier(VkCommandBuffer commands, VkImage image, VkImageAspectFlags aspect,
            std::uint32_t firstMip, std::uint32_t mipCount, std::uint32_t firstLayer,
            std::uint32_t layers, VkImageLayout from, VkImageLayout to)
    {
        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = layoutStage(from);
        barrier.srcAccessMask = layoutAccess(from);
        barrier.dstStageMask = layoutStage(to);
        barrier.dstAccessMask = layoutAccess(to);
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspect;
        barrier.subresourceRange.baseMipLevel = firstMip;
        barrier.subresourceRange.levelCount = mipCount;
        barrier.subresourceRange.baseArrayLayer = firstLayer;
        barrier.subresourceRange.layerCount = layers;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
    }

    VkCommandBuffer allocateCommands()
    {
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = commandPool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        vkAllocateCommandBuffers(device_, &allocate, &commands);
        return commands;
    }

    static void beginCommands(VkCommandBuffer commands)
    {
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
    }

    VkBuffer stagingBuffer(const void* data, std::uint32_t size)
    {
        VulkanBuffer staging;
        staging.size = size;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (!createVersion(staging, nullptr))
        {
            log("Vulkan: could not create a temporary upload buffer");
            return VK_NULL_HANDLE;
        }
        memcpy(staging.versions[0].mapped, data, size);
        Garbage item;
        item.frame = frameNumber_;
        item.buffer = staging.versions[0].buffer;
        item.allocation = staging.versions[0].allocation;
        garbage_.push_back(item);
        return staging.versions[0].buffer;
    }

    static void transferBarrier(VkCommandBuffer commands)
    {
        VkMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.memoryBarrierCount = 1;
        info.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &info);
    }

    VkCommandBuffer transferCommands()
    {
        if (frameReady_ && !passActive_) return frames_[frameIndex_].commands;
        if (!pendingTransfer_)
        {
            pendingTransfer_ = allocateCommands();
            beginCommands(pendingTransfer_);
        }
        return pendingTransfer_;
    }

    bool beginImmediate()
    {
        if (!immediateCommands_) immediateCommands_ = allocateCommands();
        if (!immediateCommands_) return false;
        vkResetCommandBuffer(immediateCommands_, 0);
        beginCommands(immediateCommands_);
        return true;
    }

    bool endImmediate()
    {
        vkEndCommandBuffer(immediateCommands_);
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &immediateCommands_;
        const bool submitted = vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
        vkQueueWaitIdle(queue_);
        return submitted;
    }

    void flush()
    {
        VkCommandBuffer buffers[2];
        std::uint32_t count = 0;
        if (pendingTransfer_)
        {
            vkEndCommandBuffer(pendingTransfer_);
            buffers[count++] = pendingTransfer_;
        }
        if (frameReady_)
        {
            vkEndCommandBuffer(frames_[frameIndex_].commands);
            buffers[count++] = frames_[frameIndex_].commands;
        }
        if (count == 0) return;
        stagingBytes_ = 0;

        VkSemaphore waits[kMaxWindows];
        VkPipelineStageFlags stages[kMaxWindows];
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = frameReady_ ? pendingWaits(waits, stages) : 0;
        submit.pWaitSemaphores = waits;
        submit.pWaitDstStageMask = stages;
        submit.commandBufferCount = count;
        submit.pCommandBuffers = buffers;
        if (vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
            log("Vulkan: could not submit the pending commands");
        vkQueueWaitIdle(queue_);

        if (pendingTransfer_)
        {
            vkFreeCommandBuffers(device_, commandPool_, 1, &pendingTransfer_);
            pendingTransfer_ = VK_NULL_HANDLE;
        }
        if (frameReady_)
        {
            frameSubmitted_ = true;
            vkResetCommandBuffer(frames_[frameIndex_].commands, 0);
            beginCommands(frames_[frameIndex_].commands);
        }
    }

    bool supportedFormat(const TextureDesc& desc) const
    {
        const FormatFamily family = formatFamily(desc.format);
        if (family == FormatFamily::Plain) return true;
        if ((desc.usage & kTextureRenderTarget) || desc.type == TextureType::Texture3D)
            return false;
        if (family == FormatFamily::BC) return caps_.textureBC;
        if (family == FormatFamily::ETC2) return caps_.textureETC2;
        return caps_.textureASTC;
    }

    VkImageView attachmentView(VulkanTexture& texture, std::uint32_t mip, std::uint32_t layer)
    {
        for (size_t i = 0; i < texture.attachmentViews.size(); ++i)
            if (texture.attachmentViews[i].mip == mip && texture.attachmentViews[i].layer == layer)
                return texture.attachmentViews[i].view;

        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = texture.image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = textureFormat(texture.format);
        info.subresourceRange.aspectMask = aspectOf(texture.format);
        info.subresourceRange.baseMipLevel = mip;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = layer;
        info.subresourceRange.layerCount = 1;

        AttachmentView entry;
        if (vkCreateImageView(device_, &info, nullptr, &entry.view) != VK_SUCCESS)
            return VK_NULL_HANDLE;
        entry.mip = mip;
        entry.layer = layer;
        texture.attachmentViews.push_back(entry);
        return entry.view;
    }

    void recordUpload(const VulkanTexture& texture, std::uint32_t mip, std::uint32_t layer,
            const void* data)
    {
        const bool volume = texture.type == TextureType::Texture3D;
        recordCopy(texture, mip, volume ? 0 : layer, 0, 0, 0, mipSize(texture.width, mip),
                mipSize(texture.height, mip), volume ? mipSize(texture.depth, mip) : 1, data);
    }

    void recordCopy(const VulkanTexture& texture, std::uint32_t mip, std::uint32_t firstLayer,
            std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t width,
            std::uint32_t height, std::uint32_t depth, const void* data)
    {
        VulkanBuffer staging;
        staging.size = levelBytes(texture.format, width, height) * depth;
        staging.vkUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (!createVersion(staging, nullptr))
        {
            log("Vulkan: could not create the upload buffer of a texture");
            return;
        }
        memcpy(staging.versions[0].mapped, data, staging.size);
        Garbage item;
        item.frame = frameNumber_;
        item.buffer = staging.versions[0].buffer;
        item.allocation = staging.versions[0].allocation;
        garbage_.push_back(item);

        VkCommandBuffer commands = transferCommands();
        imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, firstLayer, 1,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = mip;
        copy.imageSubresource.baseArrayLayer = firstLayer;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset.x = static_cast<std::int32_t>(x);
        copy.imageOffset.y = static_cast<std::int32_t>(y);
        copy.imageOffset.z = static_cast<std::int32_t>(z);
        copy.imageExtent.width = width;
        copy.imageExtent.height = height;
        copy.imageExtent.depth = depth;
        vkCmdCopyBufferToImage(commands, staging.versions[0].buffer, texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, firstLayer, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        stagingUsed(staging.size);
    }

    void stagingUsed(std::uint64_t bytes)
    {
        stagingBytes_ += bytes;
        if (frameReady_ || stagingBytes_ < kStagingBudget) return;
        flush();
        collectGarbage(true);
    }

    void recordMipmaps(const VulkanTexture& texture)
    {
        const bool volume = texture.type == TextureType::Texture3D;
        const std::uint32_t layers = imageLayers(texture);
        VkCommandBuffer commands = transferCommands();
        for (std::uint32_t mip = 1; mip < texture.mipLevels; ++mip)
        {
            imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, layers,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            VkImageBlit blit = {};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = mip - 1;
            blit.srcSubresource.layerCount = layers;
            blit.srcOffsets[1].x = static_cast<std::int32_t>(mipSize(texture.width, mip - 1));
            blit.srcOffsets[1].y = static_cast<std::int32_t>(mipSize(texture.height, mip - 1));
            blit.srcOffsets[1].z =
                    volume ? static_cast<std::int32_t>(mipSize(texture.depth, mip - 1)) : 1;
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = mip;
            blit.dstSubresource.layerCount = layers;
            blit.dstOffsets[1].x = static_cast<std::int32_t>(mipSize(texture.width, mip));
            blit.dstOffsets[1].y = static_cast<std::int32_t>(mipSize(texture.height, mip));
            blit.dstOffsets[1].z =
                    volume ? static_cast<std::int32_t>(mipSize(texture.depth, mip)) : 1;
            vkCmdBlitImage(commands, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                    VK_FILTER_LINEAR);

            imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, layers,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            imageBarrier(commands, texture.image, VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, layers,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    bool bindTextures(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkImageView views[PipelineDesc::kMaxTextures] = {};
        VkSampler samplers[PipelineDesc::kMaxTextures] = {};
        for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
        {
            const TextureSlot& binding = textureBindings_[pipeline.textureSlots[i]];
            const VulkanTexture* texture =
                    textures_.get(handleCast<TextureSlotHandle>(binding.texture));
            const VulkanSampler* sampler =
                    samplers_.get(handleCast<SamplerSlotHandle>(binding.sampler));
            if (!texture || !sampler)
            {
                log("draw: a texture the pipeline needs is not bound");
                return false;
            }
            views[i] = texture->view;
            samplers[i] = sampler->sampler;
        }

        bool sameSet = lastTextureSet_ != VK_NULL_HANDLE &&
                       lastTextureSetLayout_ == pipeline.textureSetLayout;
        for (std::uint32_t i = 0; sameSet && i < pipeline.textureCount; ++i)
            if (lastViews_[i] != views[i] || lastSamplers_[i] != samplers[i]) sameSet = false;

        if (!sameSet)
        {
            VkDescriptorSetAllocateInfo allocate = {};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptorPools_[frameIndex_];
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &pipeline.textureSetLayout;
            if (vkAllocateDescriptorSets(device_, &allocate, &lastTextureSet_) != VK_SUCCESS)
            {
                lastTextureSet_ = VK_NULL_HANDLE;
                log("draw: out of descriptor sets for this frame");
                return false;
            }

            VkDescriptorImageInfo infos[PipelineDesc::kMaxTextures] = {};
            VkWriteDescriptorSet writes[PipelineDesc::kMaxTextures] = {};
            for (std::uint32_t i = 0; i < pipeline.textureCount; ++i)
            {
                infos[i].sampler = samplers[i];
                infos[i].imageView = views[i];
                infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = lastTextureSet_;
                writes[i].dstBinding = pipeline.textureSlots[i];
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[i].pImageInfo = &infos[i];
                lastViews_[i] = views[i];
                lastSamplers_[i] = samplers[i];
            }
            vkUpdateDescriptorSets(device_, pipeline.textureCount, writes, 0, nullptr);
            lastTextureSetLayout_ = pipeline.textureSetLayout;
        }
        vkCmdBindDescriptorSets(commands, bindPoint(pipeline), pipeline.layout, 1, 1,
                &lastTextureSet_, 0, nullptr);
        return true;
    }

    bool bindUniforms(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkBuffer buffers[PipelineDesc::kMaxUniformBlocks] = {};
        std::uint32_t ranges[PipelineDesc::kMaxUniformBlocks] = {};
        std::uint32_t offsets[PipelineDesc::kMaxUniformBlocks] = {};
        for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
        {
            const UniformBinding& binding = uniformBindings_[pipeline.uniformSlots[i]];
            VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(binding.handle));
            if (!buffer)
            {
                log("draw: a uniform buffer the pipeline needs is not bound");
                return false;
            }
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            buffers[i] = version.buffer;
            ranges[i] = binding.size;
            offsets[i] = binding.offset;
        }

        bool sameSet = lastSet_ != VK_NULL_HANDLE && lastSetLayout_ == pipeline.setLayout;
        for (std::uint32_t i = 0; sameSet && i < pipeline.uniformCount; ++i)
            if (lastSetBuffers_[i] != buffers[i] || lastSetRanges_[i] != ranges[i]) sameSet = false;

        if (!sameSet)
        {
            VkDescriptorSetAllocateInfo allocate = {};
            allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocate.descriptorPool = descriptorPools_[frameIndex_];
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &pipeline.setLayout;
            if (vkAllocateDescriptorSets(device_, &allocate, &lastSet_) != VK_SUCCESS)
            {
                lastSet_ = VK_NULL_HANDLE;
                log("draw: out of descriptor sets for this frame");
                return false;
            }

            VkDescriptorBufferInfo infos[PipelineDesc::kMaxUniformBlocks] = {};
            VkWriteDescriptorSet writes[PipelineDesc::kMaxUniformBlocks] = {};
            for (std::uint32_t i = 0; i < pipeline.uniformCount; ++i)
            {
                infos[i].buffer = buffers[i];
                infos[i].range = ranges[i];
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = lastSet_;
                writes[i].dstBinding = pipeline.uniformSlots[i];
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                writes[i].pBufferInfo = &infos[i];
                lastSetBuffers_[i] = buffers[i];
                lastSetRanges_[i] = ranges[i];
            }
            vkUpdateDescriptorSets(device_, pipeline.uniformCount, writes, 0, nullptr);
            lastSetLayout_ = pipeline.setLayout;
        }
        vkCmdBindDescriptorSets(commands, bindPoint(pipeline), pipeline.layout, 0, 1, &lastSet_,
                pipeline.uniformCount, offsets);
        return true;
    }

    const VulkanPipeline* prepareDraw(std::uint64_t vertexEnd, std::uint32_t instanceCount)
    {
        const VulkanPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!passActive_ || !pipeline || pipeline->compute || instanceCount == 0)
        {
            log("draw: no active render pass, no pipeline bound in this pass or no instances");
            return nullptr;
        }
        if (!sameTargets(pipeline->targets, passFormats_))
        {
            log("draw: the pipeline was created for different render targets than this pass");
            return nullptr;
        }

        VkBuffer buffers[PipelineDesc::kMaxVertexBuffers] = {};
        VkDeviceSize offsets[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < pipeline->vertexBufferCount; ++i)
        {
            VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(vertexBuffers_[i]));
            if (!buffer ||
                    (buffer->usage != BufferUsage::Vertex && buffer->usage != BufferUsage::Storage))
            {
                log("draw: a vertex buffer the pipeline needs is not bound");
                return nullptr;
            }
            const VertexBufferLayout& layout = pipeline->vertexBuffers[i];
            const std::uint64_t count =
                    layout.step == VertexStep::Instance ? instanceCount : vertexEnd;
            if (vertexOffsets_[i] + count * layout.stride > buffer->size)
            {
                log("draw: vertex or instance range is outside the vertex buffer");
                return nullptr;
            }
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            buffers[i] = version.buffer;
            offsets[i] = vertexOffsets_[i];
        }

        VkCommandBuffer commands = frames_[frameIndex_].commands;
        if (boundPipeline_ != pipeline->pipeline)
        {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
            boundPipeline_ = pipeline->pipeline;
            uniformDirty_ = true;
            textureDirty_ = true;
            storageDirty_ = true;

            VkFrontFace frontFace = pipeline->frontFace;
            if (passOffscreen_)
                frontFace = frontFace == VK_FRONT_FACE_CLOCKWISE ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                 : VK_FRONT_FACE_CLOCKWISE;
            vkCmdSetFrontFace(commands, frontFace);
        }
        if (vertexDirty_ && pipeline->vertexBufferCount > 0)
        {
            vkCmdBindVertexBuffers(commands, 0, pipeline->vertexBufferCount, buffers, offsets);
            vertexDirty_ = false;
        }
        if (uniformDirty_ && pipeline->uniformCount > 0)
        {
            if (!bindUniforms(*pipeline, commands)) return nullptr;
            uniformDirty_ = false;
        }
        if (textureDirty_ && pipeline->textureCount > 0)
        {
            if (!bindTextures(*pipeline, commands)) return nullptr;
            textureDirty_ = false;
        }
        if (storageDirty_ && pipeline->storageCount > 0)
        {
            if (!bindStorageBuffers(*pipeline, commands)) return nullptr;
            storageDirty_ = false;
        }
        return pipeline;
    }

    VkShaderStageFlags graphicsStages() const
    {
        VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        if (caps_.geometryShaders) stages |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (caps_.tessellation)
            stages |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                      VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        return stages;
    }

    static VkPipelineBindPoint bindPoint(const VulkanPipeline& pipeline)
    {
        return pipeline.compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

    void mergeSlots(std::uint32_t* slots, std::uint32_t& count, std::uint32_t capacity,
            std::uint32_t slotLimit, const VulkanShader& shader, BindingKind kind)
    {
        const std::uint32_t index = static_cast<std::uint32_t>(kind);
        for (std::uint32_t i = 0; i < shader.slotCount[index]; ++i)
        {
            const std::uint32_t slot = shader.slots[index][i];
            bool present = false;
            for (std::uint32_t j = 0; j < count; ++j)
                if (slots[j] == slot) present = true;
            if (present) continue;
            if (count >= capacity || slot >= slotLimit)
            {
                log("createPipeline: a shader uses too many bindings or a slot out of range");
                slotOverflow_ = true;
                continue;
            }
            slots[count++] = slot;
        }
    }

    static void sortSlots(std::uint32_t* slots, std::uint32_t count)
    {
        for (std::uint32_t i = 1; i < count; ++i)
            for (std::uint32_t j = i; j > 0 && slots[j - 1] > slots[j]; --j)
            {
                const std::uint32_t swapped = slots[j];
                slots[j] = slots[j - 1];
                slots[j - 1] = swapped;
            }
    }

    bool createSlotLayout(const std::uint32_t* slots, std::uint32_t count, VkDescriptorType type,
            VkShaderStageFlags stages, VkDescriptorSetLayout* layout)
    {
        VkDescriptorSetLayoutBinding bindings[PipelineDesc::kMaxTextures] = {};
        for (std::uint32_t i = 0; i < count; ++i)
        {
            bindings[i].binding = slots[i];
            bindings[i].descriptorType = type;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = stages;
        }
        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = count;
        info.pBindings = bindings;
        return vkCreateDescriptorSetLayout(device_, &info, nullptr, layout) == VK_SUCCESS;
    }

    void destroyLayouts(VulkanPipeline& pipeline)
    {
        if (pipeline.layout) vkDestroyPipelineLayout(device_, pipeline.layout, nullptr);
        if (pipeline.setLayout) vkDestroyDescriptorSetLayout(device_, pipeline.setLayout, nullptr);
        if (pipeline.textureSetLayout)
            vkDestroyDescriptorSetLayout(device_, pipeline.textureSetLayout, nullptr);
        if (pipeline.storageSetLayout)
            vkDestroyDescriptorSetLayout(device_, pipeline.storageSetLayout, nullptr);
        if (pipeline.imageSetLayout)
            vkDestroyDescriptorSetLayout(device_, pipeline.imageSetLayout, nullptr);
    }

    VkDescriptorSet allocateSet(VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocate.descriptorPool = descriptorPools_[frameIndex_];
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(device_, &allocate, &set) != VK_SUCCESS)
        {
            log("draw: out of descriptor sets for this frame");
            return VK_NULL_HANDLE;
        }
        return set;
    }

    bool bindStorageBuffers(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkDescriptorBufferInfo infos[PipelineDesc::kMaxStorageBuffers] = {};
        VkWriteDescriptorSet writes[PipelineDesc::kMaxStorageBuffers] = {};
        for (std::uint32_t i = 0; i < pipeline.storageCount; ++i)
        {
            const UniformBinding& binding = storageBindings_[pipeline.storageSlots[i]];
            VulkanBuffer* buffer = buffers_.get(handleCast<BufferSlot>(binding.handle));
            if (!buffer)
            {
                log("draw: a storage buffer the pipeline needs is not bound");
                return false;
            }
            BufferVersion& version = buffer->versions[buffer->current];
            version.lastUsedFrame = frameNumber_;
            buffer->gpuWritten = true;
            infos[i].buffer = version.buffer;
            infos[i].offset = binding.offset;
            infos[i].range = binding.size;
        }
        const VkDescriptorSet set = allocateSet(pipeline.storageSetLayout);
        if (!set) return false;
        for (std::uint32_t i = 0; i < pipeline.storageCount; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = pipeline.storageSlots[i];
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, pipeline.storageCount, writes, 0, nullptr);
        vkCmdBindDescriptorSets(commands, bindPoint(pipeline), pipeline.layout, 2, 1, &set, 0,
                nullptr);
        return true;
    }

    VkImageView storageView(VulkanTexture& texture, std::uint32_t mip)
    {
        if (mip >= VulkanTexture::kMaxMipViews) return VK_NULL_HANDLE;
        if (texture.mipViews[mip]) return texture.mipViews[mip];

        VkImageViewCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = texture.image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        if (texture.type == TextureType::Texture2DArray)
            info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        if (texture.type == TextureType::TextureCube) info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        if (texture.type == TextureType::TextureCubeArray)
            info.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        if (texture.type == TextureType::Texture3D) info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        info.format = textureFormat(texture.format);
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = mip;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = imageLayers(texture);
        if (vkCreateImageView(device_, &info, nullptr, &texture.mipViews[mip]) != VK_SUCCESS)
            texture.mipViews[mip] = VK_NULL_HANDLE;
        return texture.mipViews[mip];
    }

    bool bindStorageImages(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        VkDescriptorImageInfo infos[ComputePipelineDesc::kMaxStorageTextures] = {};
        VkWriteDescriptorSet writes[ComputePipelineDesc::kMaxStorageTextures] = {};
        for (std::uint32_t i = 0; i < pipeline.imageCount; ++i)
        {
            const ImageSlot& binding = imageBindings_[pipeline.imageSlots[i]];
            VulkanTexture* texture = textures_.get(handleCast<TextureSlotHandle>(binding.texture));
            infos[i].imageView = texture ? storageView(*texture, binding.mip) : VK_NULL_HANDLE;
            infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (!infos[i].imageView)
            {
                log("dispatch: a storage texture the pipeline needs is not bound");
                return false;
            }
        }
        const VkDescriptorSet set = allocateSet(pipeline.imageSetLayout);
        if (!set) return false;
        for (std::uint32_t i = 0; i < pipeline.imageCount; ++i)
        {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = pipeline.imageSlots[i];
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device_, pipeline.imageCount, writes, 0, nullptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout, 3, 1,
                &set, 0, nullptr);
        return true;
    }

    void transitionStorageImages(const VulkanPipeline& pipeline, VkCommandBuffer commands,
            bool toGeneral)
    {
        for (std::uint32_t i = 0; i < pipeline.imageCount; ++i)
        {
            const ImageSlot& binding = imageBindings_[pipeline.imageSlots[i]];
            const VulkanTexture* texture =
                    textures_.get(handleCast<TextureSlotHandle>(binding.texture));
            if (!texture) continue;
            imageBarrier(commands, texture->image, VK_IMAGE_ASPECT_COLOR_BIT, binding.mip, 1, 0,
                    imageLayers(*texture),
                    toGeneral ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
                    toGeneral ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    const VulkanPipeline* prepareDispatch(VkCommandBuffer commands)
    {
        const VulkanPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!computeActive_ || !pipeline || !pipeline->compute)
        {
            log("dispatch: no active compute pass or no compute pipeline bound in this pass");
            return nullptr;
        }
        if (boundPipeline_ != pipeline->pipeline)
        {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
            boundPipeline_ = pipeline->pipeline;
            uniformDirty_ = true;
            textureDirty_ = true;
            storageDirty_ = true;
            imageDirty_ = true;
        }
        if (uniformDirty_ && pipeline->uniformCount > 0)
        {
            if (!bindUniforms(*pipeline, commands)) return nullptr;
            uniformDirty_ = false;
        }
        if (textureDirty_ && pipeline->textureCount > 0)
        {
            if (!bindTextures(*pipeline, commands)) return nullptr;
            textureDirty_ = false;
        }
        if (storageDirty_ && pipeline->storageCount > 0)
        {
            if (!bindStorageBuffers(*pipeline, commands)) return nullptr;
            storageDirty_ = false;
        }
        if (imageDirty_ && pipeline->imageCount > 0)
        {
            if (!bindStorageImages(*pipeline, commands)) return nullptr;
            imageDirty_ = false;
        }
        transitionStorageImages(*pipeline, commands, true);
        return pipeline;
    }

    void finishDispatch(const VulkanPipeline& pipeline, VkCommandBuffer commands)
    {
        transitionStorageImages(pipeline, commands, false);
        VkMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        info.memoryBarrierCount = 1;
        info.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &info);
    }

    VulkanBuffer* indirectBuffer(BufferHandle handle, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride, std::uint32_t commandSize)
    {
        VulkanBuffer* arguments = buffers_.get(handleCast<BufferSlot>(handle));
        const bool usable = arguments && (arguments->usage == BufferUsage::Indirect ||
                                                 arguments->usage == BufferUsage::Storage);
        if (!validIndirect(usable, usable ? arguments->size : 0, offset, drawCount, stride,
                    commandSize))
        {
            log("drawIndirect: indirect draws not supported, or invalid buffer, offset, count or "
                "stride");
            return nullptr;
        }
        return arguments;
    }

    static VkAttachmentLoadOp toLoadOp(LoadOp op)
    {
        switch (op)
        {
            case LoadOp::Load:
                return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:
                return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare:
                return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
    {
        const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
        char text[1200];
        snprintf(text, sizeof(text), "Vulkan %s: %s", error ? "error" : "warning", data->pMessage);
        static_cast<const VulkanDriver*>(user)->log(text);
        return VK_FALSE;
    }

    static void destroyMessenger(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
    {
        const PFN_vkDestroyDebugUtilsMessengerEXT destroy =
                reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }

    static bool hasInstanceExtension(const char* layer, const char* name)
    {
        std::uint32_t count = 0;
        vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
        ct::Vector<VkExtensionProperties> found;
        found.resize(count);
        vkEnumerateInstanceExtensionProperties(layer, &count, found.data());
        for (std::uint32_t i = 0; i < count; ++i)
            if (strcmp(found[i].extensionName, name) == 0) return true;
        return false;
    }

    bool createInstance(bool debug)
    {
        std::uint32_t version = VK_API_VERSION_1_0;
        vkEnumerateInstanceVersion(&version);
        if (version < VK_API_VERSION_1_3)
        {
            log("Vulkan: the loader is older than 1.3");
            return false;
        }

        std::uint32_t platformCount = 0;
        const char* const* platformExtensions =
                platform_.instanceExtensions(platform_.user, &platformCount);
        ct::Vector<const char*> extensions;
        for (std::uint32_t i = 0; i < platformCount; ++i)
            extensions.push_back(platformExtensions[i]);

        bool validation = false;
        bool utils = false;
        if (debug)
        {
            std::uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
            ct::Vector<VkLayerProperties> layers;
            layers.resize(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
            for (std::uint32_t i = 0; i < layerCount; ++i)
                if (strcmp(layers[i].layerName, kValidationLayer) == 0) validation = true;
            utils = hasInstanceExtension(nullptr, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) ||
                    (validation &&
                            hasInstanceExtension(kValidationLayer, VK_EXT_DEBUG_UTILS_EXTENSION_NAME));
            if (utils) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkApplicationInfo application = {};
        application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application.pEngineName = "prisma";
        application.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &application;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        if (validation)
        {
            info.enabledLayerCount = 1;
            info.ppEnabledLayerNames = &kValidationLayer;
        }
        if (vkCreateInstance(&info, nullptr, &instance_) != VK_SUCCESS)
        {
            log("Vulkan: could not create the instance");
            return false;
        }

        if (utils)
        {
            VkDebugUtilsMessengerCreateInfoEXT messenger = {};
            messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messenger.pfnUserCallback = &VulkanDriver::debugMessage;
            messenger.pUserData = this;
            const PFN_vkCreateDebugUtilsMessengerEXT create =
                    reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
            if (create && create(instance_, &messenger, nullptr, &messenger_) == VK_SUCCESS)
                caps_.debugOutput = validation;
        }
        return true;
    }

    bool pickPhysicalDevice()
    {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        ct::Vector<VkPhysicalDevice> devices;
        devices.resize(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        int bestScore = -1;
        for (std::uint32_t d = 0; d < count; ++d)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(devices[d], &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) continue;

            VkPhysicalDeviceVulkan13Features features13 = {};
            features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
            VkPhysicalDeviceVulkan12Features features12 = {};
            features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            features13.pNext = &features12;
            VkPhysicalDeviceFeatures2 features = {};
            features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(devices[d], &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;

            std::uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, nullptr);
            ct::Vector<VkQueueFamilyProperties> families;
            families.resize(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &familyCount, families.data());

            std::uint32_t family = familyCount;
            for (std::uint32_t f = 0; f < familyCount && family == familyCount; ++f)
            {
                VkBool32 presents = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, window_->surface, &presents);
                if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presents) family = f;
            }
            if (family == familyCount) continue;

            int score = 0;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 2;
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 1;
            if (score > bestScore)
            {
                bestScore = score;
                physicalDevice_ = devices[d];
                queueFamily_ = family;
                caps_.versionMajor = VK_API_VERSION_MAJOR(properties.apiVersion);
                caps_.versionMinor = VK_API_VERSION_MINOR(properties.apiVersion);
                caps_.maxTextureSize = properties.limits.maxImageDimension2D;
                caps_.maxColorTargets = properties.limits.maxColorAttachments;
                const VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts &
                                                  properties.limits.framebufferDepthSampleCounts &
                                                  properties.limits.framebufferStencilSampleCounts;
                caps_.maxSamples = (counts & VK_SAMPLE_COUNT_8_BIT)   ? 8
                                   : (counts & VK_SAMPLE_COUNT_4_BIT) ? 4
                                   : (counts & VK_SAMPLE_COUNT_2_BIT) ? 2
                                                                      : 1;
                caps_.wireframe = features.features.fillModeNonSolid;
                caps_.textureBC = features.features.textureCompressionBC;
                caps_.textureETC2 = features.features.textureCompressionETC2;
                caps_.textureASTC = features.features.textureCompressionASTC_LDR;
                caps_.cubeArrays = features.features.imageCubeArray;
                VkFormatProperties floatProperties = {};
                vkGetPhysicalDeviceFormatProperties(devices[d], VK_FORMAT_R32G32B32A32_SFLOAT,
                        &floatProperties);
                caps_.floatLinearFiltering =
                        (floatProperties.optimalTilingFeatures &
                                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
                caps_.geometryShaders = features.features.geometryShader;
                caps_.independentBlend = features.features.independentBlend;
                fragmentStores_ = features.features.fragmentStoresAndAtomics;
                vertexStores_ = features.features.vertexPipelineStoresAndAtomics;
                caps_.tessellation = features.features.tessellationShader;
                caps_.maxPatchControlPoints = properties.limits.maxTessellationPatchSize;
                multiDrawIndirect_ = features.features.multiDrawIndirect;
                caps_.storageBufferOffsetAlignment = static_cast<std::uint32_t>(
                        properties.limits.minStorageBufferOffsetAlignment);
                caps_.compressedTextureCopy = true;
                caps_.occlusionQueries = features12.hostQueryReset;
                caps_.timerQueries =
                        features12.hostQueryReset && properties.limits.timestampComputeAndGraphics;
                timestampPeriod_ = properties.limits.timestampPeriod;
                caps_.maxAnisotropy = features.features.samplerAnisotropy
                                              ? properties.limits.maxSamplerAnisotropy
                                              : 1.0f;
                caps_.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(
                        properties.limits.minUniformBufferOffsetAlignment);
            }
        }
        if (bestScore < 0)
        {
            log("Vulkan: no device with Vulkan 1.3, dynamic rendering and presentation");
            return false;
        }
        caps_.compute = true;
        caps_.indirectDraw = true;
#ifndef __ANDROID__
        caps_.multipleWindows = true;
#endif
        caps_.storageBuffersInGraphics = true;
        caps_.storageWritesInGraphics = fragmentStores_;
        caps_.floatColorTargets = true;
        return true;
    }

    bool createDevice()
    {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue = {};
        queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex = queueFamily_;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features features13 = {};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        features13.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceVulkan12Features features12 = {};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.hostQueryReset = caps_.occlusionQueries;
        features13.pNext = &features12;
        features13.synchronization2 = VK_TRUE;

        VkPhysicalDeviceFeatures enabled = {};
        enabled.samplerAnisotropy = caps_.maxAnisotropy > 1.0f;
        enabled.fillModeNonSolid = caps_.wireframe;
        enabled.textureCompressionBC = caps_.textureBC;
        enabled.textureCompressionETC2 = caps_.textureETC2;
        enabled.textureCompressionASTC_LDR = caps_.textureASTC;
        enabled.imageCubeArray = caps_.cubeArrays;
        enabled.geometryShader = caps_.geometryShaders;
        enabled.tessellationShader = caps_.tessellation;
        enabled.multiDrawIndirect = multiDrawIndirect_;
        enabled.independentBlend = caps_.independentBlend;
        enabled.fragmentStoresAndAtomics = fragmentStores_;
        enabled.vertexPipelineStoresAndAtomics = vertexStores_;

        const char* const extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.pNext = &features13;
        info.pEnabledFeatures = &enabled;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = &extension;
        if (vkCreateDevice(physicalDevice_, &info, nullptr, &device_) != VK_SUCCESS)
        {
            log("Vulkan: could not create the device");
            return false;
        }
        vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
        memory_.init(physicalDevice_, device_);
        VkFormatProperties depthProperties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, VK_FORMAT_D24_UNORM_S8_UINT,
                &depthProperties);
        depthStencilFormat_ = (depthProperties.optimalTilingFeatures &
                                      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                                      ? VK_FORMAT_D24_UNORM_S8_UINT
                                      : VK_FORMAT_D32_SFLOAT_S8_UINT;
        depthFormat_ = depthStencilFormat_;
        if (messenger_)
            setObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
                    vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
        return true;
    }

    bool createFrames()
    {
        VkCommandPoolCreateInfo pool = {};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily_;
        if (vkCreateCommandPool(device_, &pool, nullptr, &commandPool_) != VK_SUCCESS) return false;

        VkCommandBuffer commands[kFramesInFlight];
        VkCommandBufferAllocateInfo allocate = {};
        allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool = commandPool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = kFramesInFlight;
        if (vkAllocateCommandBuffers(device_, &allocate, commands) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence = {};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            frames_[i].commands = commands[i];
            if (vkCreateSemaphore(device_, &semaphore, nullptr, &frames_[i].imageAvailable) !=
                            VK_SUCCESS ||
                    vkCreateFence(device_, &fence, nullptr, &frames_[i].inFlight) != VK_SUCCESS)
                return false;
        }
        return true;
    }

    bool createQueryPools()
    {
        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryCount = kPoolQueries;
        if (caps_.occlusionQueries)
        {
            info.queryType = VK_QUERY_TYPE_OCCLUSION;
            if (vkCreateQueryPool(device_, &info, nullptr, &occlusionPool_) != VK_SUCCESS)
                return false;
        }
        if (caps_.timerQueries)
        {
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            if (vkCreateQueryPool(device_, &info, nullptr, &timePool_) != VK_SUCCESS) return false;
        }
        return true;
    }

    bool createDescriptorPools()
    {
        const VkDescriptorPoolSize sizes[4] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 8192 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024 },
        };
        VkDescriptorPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = 2048;
        info.poolSizeCount = 4;
        info.pPoolSizes = sizes;
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
            if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptorPools_[i]) != VK_SUCCESS)
                return false;
        return true;
    }

    void destroyDepth()
    {
        if (window_->depthView) vkDestroyImageView(device_, window_->depthView, nullptr);
        if (window_->depthImage) vkDestroyImage(device_, window_->depthImage, nullptr);
        memory_.free(window_->depthAllocation);
        window_->depthView = VK_NULL_HANDLE;
        window_->depthImage = VK_NULL_HANDLE;
        window_->depthAllocation = VulkanAllocation();
        window_->depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    bool createDepth()
    {
        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat_;
        image.extent.width = window_->extent.width;
        image.extent.height = window_->extent.height;
        image.extent.depth = 1;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &image, nullptr, &window_->depthImage) != VK_SUCCESS)
            return false;

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device_, window_->depthImage, &requirements);
        if (!memory_.allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VulkanMemoryKind::Image, &window_->depthAllocation) ||
                vkBindImageMemory(device_, window_->depthImage, window_->depthAllocation.memory,
                        window_->depthAllocation.offset) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = window_->depthImage;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = depthFormat_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        return vkCreateImageView(device_, &view, nullptr, &window_->depthView) == VK_SUCCESS;
    }

    void transitionDepth(VkCommandBuffer commands)
    {
        if (window_->depthLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) return;

        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstStageMask = barrier.srcStageMask;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = window_->depthImage;
        barrier.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
        window_->depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    void destroySwapchain()
    {
        destroyDepth();
        for (std::size_t i = 0; i < window_->images.size(); ++i)
        {
            vkDestroyImageView(device_, window_->images[i].view, nullptr);
            vkDestroySemaphore(device_, window_->images[i].renderFinished, nullptr);
        }
        window_->images.clear();
        if (window_->swapchain) vkDestroySwapchainKHR(device_, window_->swapchain, nullptr);
        window_->swapchain = VK_NULL_HANDLE;
    }

    bool rebuildSwapchain()
    {
        vkDeviceWaitIdle(device_);

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, window_->surface, &capabilities);

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            window_->platform.framebufferSize(window_->platform.user, &extent.width,
                    &extent.height);
            if (extent.width < capabilities.minImageExtent.width)
                extent.width = capabilities.minImageExtent.width;
            if (extent.width > capabilities.maxImageExtent.width)
                extent.width = capabilities.maxImageExtent.width;
            if (extent.height < capabilities.minImageExtent.height)
                extent.height = capabilities.minImageExtent.height;
            if (extent.height > capabilities.maxImageExtent.height)
                extent.height = capabilities.maxImageExtent.height;
        }
        if (extent.width == 0 || extent.height == 0) return false;

        std::uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, window_->surface, &formatCount,
                nullptr);
        ct::Vector<VkSurfaceFormatKHR> formats;
        formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, window_->surface, &formatCount,
                formats.data());
        if (formatCount == 0) return false;
        VkSurfaceFormatKHR chosen = formats[0];
        for (std::uint32_t i = 0; i < formatCount; ++i)
        {
            if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                    formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
            {
                chosen = formats[i];
                break;
            }
        }

        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
            imageCount = capabilities.maxImageCount;

        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(capabilities.supportedCompositeAlpha & alpha))
            alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

        const VkSwapchainKHR old = window_->swapchain;
        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = window_->surface;
        info.minImageCount = imageCount;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        window_->canRead =
                (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        if (window_->canRead) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform =
                (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                        : capabilities.currentTransform;
        info.compositeAlpha = alpha;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old;

        VkSwapchainKHR created = VK_NULL_HANDLE;
        const VkResult result = vkCreateSwapchainKHR(device_, &info, nullptr, &created);
        destroySwapchain();
        if (result != VK_SUCCESS)
        {
            log("Vulkan: could not create the swapchain");
            return false;
        }
        window_->swapchain = created;
        window_->extent = extent;
        format_ = chosen.format;

        vkGetSwapchainImagesKHR(device_, window_->swapchain, &imageCount, nullptr);
        ct::Vector<VkImage> images;
        images.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, window_->swapchain, &imageCount, images.data());

        VkSemaphoreCreateInfo semaphore = {};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (std::uint32_t i = 0; i < imageCount; ++i)
        {
            SwapchainImage image;
            image.image = images[i];

            VkImageViewCreateInfo view = {};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format_;
            view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view.subresourceRange.levelCount = 1;
            view.subresourceRange.layerCount = 1;
            vkCreateImageView(device_, &view, nullptr, &image.view);
            vkCreateSemaphore(device_, &semaphore, nullptr, &image.renderFinished);
            window_->images.push_back(image);
        }
        if (!createDepth())
        {
            log("Vulkan: could not create the depth buffer of the window");
            return false;
        }
        return true;
    }

    static void transition(VkCommandBuffer commands, SwapchainImage& target, VkImageLayout from,
            VkImageLayout to)
    {
        if (from == to) return;

        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                        ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                        : 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                        ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                                        : 0;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency = {};
        dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commands, &dependency);
        target.layout = to;
    }

    VulkanPlatform platform_;
    void (*log_)(const char*);
    Caps caps_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queueFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;

    VkFormat format_ = VK_FORMAT_UNDEFINED;

    Frame frames_[kFramesInFlight];
    std::uint32_t frameIndex_ = 0;
    bool slotOverflow_ = false;
    bool frameReady_ = false;
    bool passActive_ = false;
    bool computeActive_ = false;
    bool storageUsedInPass_ = false;
    bool fragmentStores_ = false;
    bool vertexStores_ = false;
    bool multiDrawIndirect_ = false;
    bool storageDirty_ = true;
    bool imageDirty_ = true;
    UniformBinding storageBindings_[PipelineDesc::kMaxStorageBuffers];
    ImageSlot imageBindings_[ComputePipelineDesc::kMaxStorageTextures];
    TargetFormats passFormats_;
    std::uint32_t passWidth_ = 0;
    std::uint32_t passHeight_ = 0;
    std::uint64_t frameNumber_ = 0;
    std::uint64_t stagingBytes_ = 0;

    PFN_vkSetDebugUtilsObjectNameEXT setObjectName_ = nullptr;

    ct::SlotMap32<VulkanBuffer> buffers_;
    ct::SlotMap32<VulkanShader> shaders_;
    ct::SlotMap32<VulkanPipeline> pipelines_;
    ct::SlotMap32<VulkanTexture> textures_;
    ct::SlotMap32<VulkanSampler> samplers_;
    ct::Vector<Garbage> garbage_;

    PipelineHandle pipeline_;
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    BufferHandle vertexBuffers_[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexOffsets_[PipelineDesc::kMaxVertexBuffers] = {};
    BufferHandle indexBuffer_;
    bool vertexDirty_ = true;
    bool indexDirty_ = true;
    bool uniformDirty_ = true;

    enum : std::uint32_t
    {
        kMaxUniformSlots = 16
    };
    UniformBinding uniformBindings_[kMaxUniformSlots];

    VkDescriptorPool descriptorPools_[kFramesInFlight] = {};
    VkDescriptorSet lastSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lastSetLayout_ = VK_NULL_HANDLE;
    VkBuffer lastSetBuffers_[PipelineDesc::kMaxUniformBlocks] = {};
    std::uint32_t lastSetRanges_[PipelineDesc::kMaxUniformBlocks] = {};

    enum : std::uint32_t
    {
        kMaxTextureSlots = 16
    };
    TextureSlot textureBindings_[kMaxTextureSlots];
    bool textureDirty_ = true;
    VkDescriptorSet lastTextureSet_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout lastTextureSetLayout_ = VK_NULL_HANDLE;
    VkImageView lastViews_[PipelineDesc::kMaxTextures] = {};
    VkSampler lastSamplers_[PipelineDesc::kMaxTextures] = {};
    using QuerySlotHandle = ct::Handle32<VulkanQuery>;
    ct::SlotMap32<VulkanQuery> queries_;
    using ReadbackSlotHandle = ct::Handle32<VulkanReadback>;
    ct::SlotMap32<VulkanReadback> readbacks_;
    std::uint64_t waitedFrame_ = 0;
    VkQueryPool occlusionPool_ = VK_NULL_HANDLE;
    VkQueryPool timePool_ = VK_NULL_HANDLE;
    ct::Vector<std::uint32_t> freeOcclusion_;
    ct::Vector<std::uint32_t> freeTime_;
    std::uint32_t occlusionTop_ = 0;
    std::uint32_t timeTop_ = 0;
    std::uint64_t querySequence_ = 0;
    float timestampPeriod_ = 1.0f;
    bool occlusionActive_ = false;
    QueryHandle openOcclusion_;

    VkCommandBuffer immediateCommands_ = VK_NULL_HANDLE;
    VkCommandBuffer pendingTransfer_ = VK_NULL_HANDLE;
    bool frameSubmitted_ = false;
    bool passOffscreen_ = false;
    PassTarget passTargets_[RenderPassDesc::kMaxColorTargets * 2 + 2];
    std::uint32_t passTargetCount_ = 0;
    VkFormat depthStencilFormat_ = VK_FORMAT_D24_UNORM_S8_UINT;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VulkanWindow mainWindow_;
    VulkanWindow* window_ = &mainWindow_;
    using SwapchainSlot = ct::Handle32<VulkanWindow*>;
    ct::SlotMap32<VulkanWindow*> swapchains_;
    VulkanMemory memory_;
};

} // namespace

Driver* createVulkanDriver(const DriverDesc& desc, DriverError* error)
{
    const VulkanPlatform* vulkan = desc.vulkan;
    if (!vulkan || !vulkan->instanceExtensions || !vulkan->createSurface ||
            !vulkan->framebufferSize)
    {
        *error = DriverError::MissingPlatform;
        return nullptr;
    }

    VulkanDriver* driver = new VulkanDriver(*vulkan, desc.log);
    *error = driver->init(desc.debug);
    if (*error != DriverError::None)
    {
        delete driver;
        return nullptr;
    }
    return driver;
}

} // namespace prisma
