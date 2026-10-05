#pragma once

#include "prisma/rhi/Caps.h"
#include "prisma/rhi/Types.h"

namespace prisma
{

class Driver
{
public:
    virtual ~Driver() = default;

    virtual DriverType type() const = 0;
    virtual const Caps& caps() const = 0;

    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc& desc) = 0;
    virtual PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual SamplerHandle createSampler(const SamplerDesc& desc) = 0;
    virtual void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) = 0;
    virtual void updateTexture(TextureHandle handle, std::uint32_t mip, std::uint32_t layer,
            const void* data) = 0;
    virtual void updateTextureRegion(TextureHandle handle, const TextureRegion& region,
            const void* data) = 0;
    virtual void generateMipmaps(TextureHandle handle) = 0;
    virtual void copyTexture(const TextureCopy& copy) = 0;
    virtual void copyBuffer(BufferHandle source, std::uint32_t sourceOffset,
            BufferHandle destination, std::uint32_t destinationOffset, std::uint32_t size) = 0;
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(PipelineHandle handle) = 0;
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(SamplerHandle handle) = 0;

    virtual QueryHandle createQuery(QueryType type) = 0;
    virtual void destroy(QueryHandle handle) = 0;

    virtual SwapchainHandle createSwapchain(const SwapchainDesc& desc) = 0;
    virtual void destroy(SwapchainHandle handle) = 0;

    virtual ReadbackHandle requestReadback(const RenderTarget& source, const Rect& rect) = 0;
    virtual bool readbackResult(ReadbackHandle handle, void* data) = 0;
    virtual ReadbackHandle requestBufferReadback(BufferHandle source, std::uint32_t offset,
            std::uint32_t size) = 0;
    virtual bool readBuffer(BufferHandle source, std::uint32_t offset, std::uint32_t size,
            void* data) = 0;
    virtual void destroy(ReadbackHandle handle) = 0;
    virtual void beginQuery(QueryHandle handle) = 0;
    virtual void endQuery(QueryHandle handle) = 0;
    virtual bool queryResult(QueryHandle handle, std::uint64_t* result) = 0;

    virtual void beginFrame() = 0;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& rect) = 0;
    virtual void setStencilReference(std::uint32_t reference) = 0;
    virtual void bindPipeline(PipelineHandle handle) = 0;
    virtual void bindVertexBuffer(std::uint32_t slot, BufferHandle handle,
            std::uint32_t offset) = 0;
    virtual void bindIndexBuffer(BufferHandle handle) = 0;
    virtual void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) = 0;
    virtual void bindTexture(std::uint32_t slot, TextureHandle texture, SamplerHandle sampler) = 0;
    virtual void draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
            std::uint32_t instanceCount = 1) = 0;
    virtual void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
            std::uint32_t instanceCount = 1) = 0;
    virtual void drawIndirect(BufferHandle arguments, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride = 0) = 0;
    virtual void drawIndexedIndirect(BufferHandle arguments, std::uint32_t offset,
            std::uint32_t drawCount, std::uint32_t stride = 0) = 0;
    virtual void endRenderPass() = 0;

    virtual void beginComputePass() = 0;
    virtual void bindStorageBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) = 0;
    virtual void bindStorageTexture(std::uint32_t slot, TextureHandle texture, std::uint32_t mip,
            StorageAccess access) = 0;
    virtual void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void dispatchIndirect(BufferHandle arguments, std::uint32_t offset) = 0;
    virtual void endComputePass() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;

    virtual bool readPixels(const RenderTarget& source, const Rect& rect, void* rgba) = 0;
};

bool isDriverSupported(DriverType type);
Driver* createDriver(const DriverDesc& desc, DriverError* error = nullptr);
void destroyDriver(Driver* driver);
const char* driverErrorText(DriverError error);

} // namespace prisma
