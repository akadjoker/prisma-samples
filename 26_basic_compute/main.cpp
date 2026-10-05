#include "common/ZenApp.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "basic.comp.h"
#include "basic.frag.h"
#include "basic.vert.h"

namespace
{

const std::uint32_t kColumns = 64;
const std::uint32_t kCount = kColumns * kColumns;
const std::uint32_t kGroupSize = 64;

struct Element
{
    std::int32_t i;
    float f;
};

struct FrameUniforms
{
    float view[4];
    float params[4];
};

void fillInputs(ct::Vector<Element>* first, ct::Vector<Element>* second)
{
    first->resize(kCount);
    second->resize(kCount);
    for (std::uint32_t index = 0; index < kCount; ++index)
    {
        const std::uint32_t column = index % kColumns;
        const std::uint32_t row = index / kColumns;
        const float x = static_cast<float>(column);
        const float y = static_cast<float>(row);

        (*first)[index].i = static_cast<std::int32_t>(index);
        (*first)[index].f = 0.5f * (sinf(0.21f * x) + cosf(0.17f * y));

        (*second)[index].i = static_cast<std::int32_t>((column ^ row) & 63u);
        (*second)[index].f = 0.5f * (sinf(0.11f * (x + y)) + cosf(0.19f * (x - y)));
    }
}

bool verify(const ct::Vector<Element>& first, const ct::Vector<Element>& second,
        const ct::Vector<Element>& result)
{
    std::uint32_t wrong = 0;
    for (std::uint32_t index = 0; index < kCount; ++index)
    {
        const std::int32_t expectedInt = first[index].i + second[index].i;
        const float expectedFloat = first[index].f + second[index].f;
        if (result[index].i != expectedInt || result[index].f != expectedFloat)
        {
            if (wrong < 4)
            {
                log_error("basic compute: element %u is (%d, %f), expected (%d, %f)", index,
                        result[index].i, result[index].f, expectedInt, expectedFloat);
            }
            ++wrong;
        }
    }
    if (wrong == 0)
        log_info("basic compute: PASS, %u elements match the CPU", kCount);
    else
        log_error("basic compute: FAIL, %u of %u elements differ from the CPU", wrong, kCount);
    return wrong == 0;
}

} // namespace

int main(int argc, char** argv)
{
    const int maxFrames = zenapp::frameLimit(argc, argv);
    const prisma::DriverType driverType = zenapp::driverType(argc, argv);
    const bool still = zenapp::hasArgument(argc, argv, "still");

    if (!platform_init())
    {
        log_error("platform: %s", platform_get_error());
        return 1;
    }

    PlatformWindow* window = zenapp::openWindow("prisma 26 basic compute", driverType);
    if (!window)
    {
        log_error("window: %s", platform_get_error());
        platform_shutdown();
        return 1;
    }

    prisma::Driver* driver = zenapp::createDriver(window, driverType);
    if (!driver)
    {
        window_destroy(window);
        platform_shutdown();
        return 1;
    }

    if (!driver->caps().compute || !driver->caps().storageBuffersInGraphics)
    {
        log_error("basic compute: this GPU has no compute shaders or no storage buffers in "
                  "vertex shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    ct::Vector<Element> first;
    ct::Vector<Element> second;
    fillInputs(&first, &second);

    const std::uint32_t bytes = kCount * sizeof(Element);
    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = bytes;
    bufferDesc.data = first.data();
    bufferDesc.debugName = "buffer 0";
    const prisma::BufferHandle buffer0 = driver->createBuffer(bufferDesc);
    bufferDesc.data = second.data();
    bufferDesc.debugName = "buffer 1";
    const prisma::BufferHandle buffer1 = driver->createBuffer(bufferDesc);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "buffer out";
    const prisma::BufferHandle bufferOut = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle frameBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle computeShader = zenapp::createShader(driver, basic_comp);
    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, basic_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, basic_frag);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.shader = computeShader;
    computeDesc.storageBufferCount = 3;
    computeDesc.storageBuffers[0].name = "Buffer0";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.storageBuffers[1].name = "Buffer1";
    computeDesc.storageBuffers[1].slot = 1;
    computeDesc.storageBuffers[2].name = "BufferOut";
    computeDesc.storageBuffers[2].slot = 2;
    computeDesc.debugName = "basic compute";
    const prisma::PipelineHandle computePipeline = driver->createComputePipeline(computeDesc);

    prisma::PipelineDesc drawDesc;
    drawDesc.vertexShader = vertexShader;
    drawDesc.fragmentShader = fragmentShader;
    drawDesc.topology = prisma::Topology::TriangleStrip;
    drawDesc.uniformBlockCount = 1;
    drawDesc.uniformBlocks[0].name = "Frame";
    drawDesc.uniformBlocks[0].slot = 0;
    drawDesc.storageBufferCount = 1;
    drawDesc.storageBuffers[0].name = "Result";
    drawDesc.storageBuffers[0].slot = 0;
    drawDesc.depthTest = false;
    drawDesc.depthWrite = false;
    drawDesc.debugName = "basic draw";
    const prisma::PipelineHandle drawPipeline = driver->createPipeline(drawDesc);

    driver->destroy(computeShader);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = buffer0.valid() && buffer1.valid() && bufferOut.valid() &&
                       frameBuffer.valid() && computePipeline.valid() && drawPipeline.valid();
    if (!ready) log_error("basic compute: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.02f;
    pass.clearColor[1] = 0.02f;
    pass.clearColor[2] = 0.04f;

    bool passed = true;
    bool computed = false;
    const double start = time_seconds();
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;
        const float aspect = static_cast<float>(width) / static_cast<float>(height);

        const float pitch = 1.9f / static_cast<float>(kColumns);
        FrameUniforms frame;
        frame.view[0] = pitch / aspect;
        frame.view[1] = pitch;
        frame.view[2] = frame.view[3] = 0.0f;
        frame.params[0] = static_cast<float>(kColumns);
        frame.params[1] = 64.0f;
        frame.params[2] = still ? 0.0f : static_cast<float>(time_seconds() - start);
        frame.params[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(frameBuffer, 0, &frame, sizeof(frame));

        if (!computed)
        {
            driver->beginComputePass();
            driver->bindPipeline(computePipeline);
            driver->bindStorageBuffer(0, buffer0, 0, bytes);
            driver->bindStorageBuffer(1, buffer1, 0, bytes);
            driver->bindStorageBuffer(2, bufferOut, 0, bytes);
            driver->dispatch(kCount / kGroupSize, 1, 1);
            driver->endComputePass();

            ct::Vector<Element> result;
            result.resize(kCount);
            if (driver->readBuffer(bufferOut, 0, bytes, result.data()))
                passed = verify(first, second, result);
            else
            {
                log_error("basic compute: FAIL, could not read the result back");
                passed = false;
            }
            computed = true;
        }

        driver->beginRenderPass(pass);
        driver->bindPipeline(drawPipeline);
        driver->bindUniformBuffer(0, frameBuffer, 0, sizeof(FrameUniforms));
        driver->bindStorageBuffer(0, bufferOut, 0, bytes);
        driver->draw(4, 0, kCount);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(drawPipeline);
    driver->destroy(computePipeline);
    driver->destroy(frameBuffer);
    driver->destroy(bufferOut);
    driver->destroy(buffer1);
    driver->destroy(buffer0);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready && passed ? 0 : 1;
}
