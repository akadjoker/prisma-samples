#include "prisma/rhi/Driver.h"
#include "prisma/rhi/HandleCast.h"

namespace prisma
{

namespace
{

class NullDriver final : public Driver
{
public:
    DriverType type() const override { return DriverType::Null; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();
        return handleCast<BufferHandle>(buffers_.insert(desc.size));
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.source) return ShaderHandle();
        return handleCast<ShaderHandle>(shaders_.insert(0));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        if (!shaders_.contains(handleCast<Slot>(desc.vertexShader)) ||
                !shaders_.contains(handleCast<Slot>(desc.fragmentShader)))
            return PipelineHandle();
        return handleCast<PipelineHandle>(pipelines_.insert(0));
    }

    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override
    {
        if (!shaders_.contains(handleCast<Slot>(desc.shader))) return PipelineHandle();
        return handleCast<PipelineHandle>(pipelines_.insert(0));
    }

    void updateBuffer(BufferHandle, std::uint32_t, const void*, std::uint32_t) override {}
    void updateTexture(TextureHandle, std::uint32_t, std::uint32_t, const void*) override {}
    void updateTextureRegion(TextureHandle, const TextureRegion&, const void*) override {}
    void generateMipmaps(TextureHandle) override {}
    void copyTexture(const TextureCopy&) override {}
    void copyBuffer(BufferHandle, std::uint32_t, BufferHandle, std::uint32_t,
            std::uint32_t) override
    {
    }
    void destroy(BufferHandle handle) override { buffers_.erase(handleCast<Slot>(handle)); }
    void destroy(ShaderHandle handle) override { shaders_.erase(handleCast<Slot>(handle)); }
    TextureHandle createTexture(const TextureDesc& desc) override
    {
        if (desc.width == 0 || desc.height == 0) return TextureHandle();
        return handleCast<TextureHandle>(textures_.insert(0));
    }

    SamplerHandle createSampler(const SamplerDesc&) override
    {
        return handleCast<SamplerHandle>(samplers_.insert(0));
    }

    void destroy(PipelineHandle handle) override { pipelines_.erase(handleCast<Slot>(handle)); }
    void destroy(TextureHandle handle) override { textures_.erase(handleCast<Slot>(handle)); }
    void destroy(SamplerHandle handle) override { samplers_.erase(handleCast<Slot>(handle)); }

    QueryHandle createQuery(QueryType) override
    {
        return handleCast<QueryHandle>(queries_.insert(0));
    }
    void destroy(QueryHandle handle) override { queries_.erase(handleCast<Slot>(handle)); }
    SwapchainHandle createSwapchain(const SwapchainDesc&) override
    {
        return handleCast<SwapchainHandle>(swapchains_.insert(0));
    }
    void destroy(SwapchainHandle handle) override { swapchains_.erase(handleCast<Slot>(handle)); }
    ReadbackHandle requestReadback(const RenderTarget&, const Rect&) override
    {
        return handleCast<ReadbackHandle>(readbacks_.insert(0));
    }
    bool readbackResult(ReadbackHandle, void*) override { return false; }
    ReadbackHandle requestBufferReadback(BufferHandle, std::uint32_t, std::uint32_t) override
    {
        return handleCast<ReadbackHandle>(readbacks_.insert(0));
    }
    bool readBuffer(BufferHandle, std::uint32_t, std::uint32_t, void*) override { return false; }
    void destroy(ReadbackHandle handle) override { readbacks_.erase(handleCast<Slot>(handle)); }
    void beginQuery(QueryHandle) override {}
    void endQuery(QueryHandle) override {}
    bool queryResult(QueryHandle, std::uint64_t*) override { return false; }

    void beginFrame() override {}
    void beginRenderPass(const RenderPassDesc&) override {}
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void setStencilReference(std::uint32_t) override {}
    void bindPipeline(PipelineHandle) override {}
    void bindVertexBuffer(std::uint32_t, BufferHandle, std::uint32_t) override {}
    void bindIndexBuffer(BufferHandle) override {}
    void bindUniformBuffer(std::uint32_t, BufferHandle, std::uint32_t, std::uint32_t) override {}
    void bindTexture(std::uint32_t, TextureHandle, SamplerHandle) override {}
    void draw(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void drawIndirect(BufferHandle, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void drawIndexedIndirect(BufferHandle, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void endRenderPass() override {}
    void beginComputePass() override {}
    void bindStorageBuffer(std::uint32_t, BufferHandle, std::uint32_t, std::uint32_t) override {}
    void bindStorageTexture(std::uint32_t, TextureHandle, std::uint32_t, StorageAccess) override {}
    void dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void dispatchIndirect(BufferHandle, std::uint32_t) override {}
    void endComputePass() override {}
    void endFrame() override {}
    void present() override {}
    bool readPixels(const RenderTarget&, const Rect&, void*) override { return false; }

private:
    using Slot = ct::Handle32<std::uint32_t>;

    Caps caps_;
    ct::SlotMap32<std::uint32_t> buffers_;
    ct::SlotMap32<std::uint32_t> shaders_;
    ct::SlotMap32<std::uint32_t> pipelines_;
    ct::SlotMap32<std::uint32_t> textures_;
    ct::SlotMap32<std::uint32_t> samplers_;
    ct::SlotMap32<std::uint32_t> queries_;
    ct::SlotMap32<std::uint32_t> readbacks_;
    ct::SlotMap32<std::uint32_t> swapchains_;
};

} // namespace

Driver* createNullDriver(const DriverDesc&) { return new NullDriver(); }

} // namespace prisma
