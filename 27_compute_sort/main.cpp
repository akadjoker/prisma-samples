#include "common/ZenApp.h"

#include <ct/sort.hpp>
#include <ct/vector.hpp>

#include <string.h>

#include "sort.frag.h"
#include "sort.vert.h"
#include "sort_bitonic.comp.h"
#include "sort_transpose.comp.h"

namespace
{

const std::uint32_t kCount = 65536;
const std::uint32_t kBlock = 256;
const std::uint32_t kWidth = kBlock;
const std::uint32_t kHeight = kCount / kBlock;
const std::uint32_t kTile = 16;
const double kStepSeconds = 0.25;
const double kHoldSeconds = 2.5;

struct Step
{
    bool transpose;
    std::uint32_t params[4];
    int source;
    int target;
};

struct PlotUniforms
{
    float params[4];
};

void buildSteps(ct::Vector<Step>* steps)
{
    steps->clear();
    for (std::uint32_t level = 2; level <= kBlock; level *= 2)
    {
        Step sort = {};
        sort.params[0] = level;
        sort.params[1] = level;
        sort.params[2] = kHeight;
        sort.params[3] = kWidth;
        steps->push_back(sort);
    }
    for (std::uint32_t level = kBlock * 2; level <= kCount; level *= 2)
    {
        Step down = {};
        down.transpose = true;
        down.params[0] = level / kBlock;
        down.params[1] = (level & ~kCount) / kBlock;
        down.params[2] = kWidth;
        down.params[3] = kHeight;
        down.source = 0;
        down.target = 1;
        steps->push_back(down);
        Step columns = down;
        columns.transpose = false;
        columns.source = 1;
        steps->push_back(columns);

        Step up = {};
        up.transpose = true;
        up.params[0] = kBlock;
        up.params[1] = level;
        up.params[2] = kHeight;
        up.params[3] = kWidth;
        up.source = 1;
        up.target = 0;
        steps->push_back(up);
        Step rows = up;
        rows.transpose = false;
        rows.source = 0;
        steps->push_back(rows);
    }
}

void fillValues(ct::Vector<std::uint32_t>* values, std::uint32_t seed)
{
    values->resize(kCount);
    std::uint32_t state = seed * 2654435761u + 12345u;
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        (*values)[i] = state;
    }
}

struct Sorter
{
    prisma::Driver* driver;
    prisma::PipelineHandle bitonic;
    prisma::PipelineHandle transpose;
    prisma::BufferHandle work[2];
    prisma::BufferHandle params;
    std::uint32_t stride;
    std::uint32_t bytes;
    ct::Vector<Step> steps;
    ct::Vector<std::uint32_t> expected;
    std::uint32_t next;
    int shown;
};

void run(Sorter* sorter, std::uint32_t count)
{
    prisma::Driver* driver = sorter->driver;
    driver->beginComputePass();
    for (std::uint32_t n = 0; n < count && sorter->next < sorter->steps.size(); ++n)
    {
        const std::uint32_t index = sorter->next++;
        const Step& step = sorter->steps[index];
        driver->bindPipeline(step.transpose ? sorter->transpose : sorter->bitonic);
        driver->bindUniformBuffer(0, sorter->params, index * sorter->stride,
                sizeof(step.params));
        driver->bindStorageBuffer(0, sorter->work[step.source], 0, sorter->bytes);
        if (step.transpose)
        {
            driver->bindStorageBuffer(1, sorter->work[step.target], 0, sorter->bytes);
            driver->dispatch(step.params[2] / kTile, step.params[3] / kTile, 1);
            sorter->shown = step.target;
        }
        else
        {
            driver->dispatch(kCount / kBlock, 1, 1);
            sorter->shown = step.source;
        }
    }
    driver->endComputePass();
}

bool verify(Sorter* sorter)
{
    ct::Vector<std::uint32_t> result;
    result.resize(kCount);
    if (!sorter->driver->readBuffer(sorter->work[0], 0, sorter->bytes, result.data()))
    {
        log_error("compute sort: FAIL, could not read the result back");
        return false;
    }
    std::uint32_t wrong = 0;
    for (std::uint32_t i = 0; i < kCount; ++i)
    {
        if (result[i] != sorter->expected[i])
        {
            if (wrong < 4)
            {
                log_error("compute sort: element %u is %u, expected %u", i, result[i],
                        sorter->expected[i]);
            }
            ++wrong;
        }
    }
    if (wrong == 0)
        log_info("compute sort: PASS, %u values sorted in %u dispatches match the CPU sort", kCount,
                static_cast<std::uint32_t>(sorter->steps.size()));
    else
        log_error("compute sort: FAIL, %u of %u values differ from the CPU sort", wrong, kCount);
    return wrong == 0;
}

void restart(Sorter* sorter, prisma::BufferHandle original, std::uint32_t seed)
{
    ct::Vector<std::uint32_t> values;
    fillValues(&values, seed);
    sorter->expected = values;
    ct::sort(sorter->expected.data(), sorter->expected.data() + sorter->expected.size());
    sorter->driver->updateBuffer(sorter->work[0], 0, values.data(), sorter->bytes);
    sorter->driver->updateBuffer(original, 0, values.data(), sorter->bytes);
    sorter->next = 0;
    sorter->shown = 0;
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

    PlatformWindow* window = zenapp::openWindow("prisma 27 compute sort", driverType);
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
        log_error("compute sort: this GPU has no compute shaders or no storage buffers in "
                  "vertex shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    Sorter sorter;
    sorter.driver = driver;
    sorter.bytes = kCount * sizeof(std::uint32_t);
    buildSteps(&sorter.steps);

    ct::Vector<std::uint32_t> values;
    fillValues(&values, 1);

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = sorter.bytes;
    bufferDesc.data = values.data();
    bufferDesc.debugName = "work 0";
    sorter.work[0] = driver->createBuffer(bufferDesc);
    bufferDesc.debugName = "original";
    const prisma::BufferHandle original = driver->createBuffer(bufferDesc);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "work 1";
    sorter.work[1] = driver->createBuffer(bufferDesc);

    const std::uint32_t alignment = driver->caps().uniformBufferOffsetAlignment;
    sorter.stride = (static_cast<std::uint32_t>(sizeof(Step::params)) + alignment - 1) /
                    alignment * alignment;
    ct::Vector<unsigned char> paramBytes;
    paramBytes.resize(static_cast<size_t>(sorter.stride) * sorter.steps.size());
    memset(paramBytes.data(), 0, paramBytes.size());
    for (size_t i = 0; i < sorter.steps.size(); ++i)
        memcpy(paramBytes.data() + i * sorter.stride, sorter.steps[i].params,
                sizeof(Step::params));
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(paramBytes.size());
    bufferDesc.data = paramBytes.data();
    bufferDesc.debugName = "sort params";
    sorter.params = driver->createBuffer(bufferDesc);

    bufferDesc.size = sizeof(PlotUniforms);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "plot left";
    const prisma::BufferHandle plotLeft = driver->createBuffer(bufferDesc);
    bufferDesc.debugName = "plot right";
    const prisma::BufferHandle plotRight = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle bitonicShader = zenapp::createShader(driver, sort_bitonic_comp);
    const prisma::ShaderHandle transposeShader =
            zenapp::createShader(driver, sort_transpose_comp);
    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, sort_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, sort_frag);

    prisma::ComputePipelineDesc computeDesc;
    computeDesc.shader = bitonicShader;
    computeDesc.uniformBlockCount = 1;
    computeDesc.uniformBlocks[0].name = "Params";
    computeDesc.uniformBlocks[0].slot = 0;
    computeDesc.storageBufferCount = 1;
    computeDesc.storageBuffers[0].name = "Data";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.debugName = "bitonic sort";
    sorter.bitonic = driver->createComputePipeline(computeDesc);

    computeDesc.shader = transposeShader;
    computeDesc.storageBufferCount = 2;
    computeDesc.storageBuffers[0].name = "Input";
    computeDesc.storageBuffers[0].slot = 0;
    computeDesc.storageBuffers[1].name = "Data";
    computeDesc.storageBuffers[1].slot = 1;
    computeDesc.debugName = "matrix transpose";
    sorter.transpose = driver->createComputePipeline(computeDesc);

    prisma::PipelineDesc drawDesc;
    drawDesc.vertexShader = vertexShader;
    drawDesc.fragmentShader = fragmentShader;
    drawDesc.topology = prisma::Topology::TriangleStrip;
    drawDesc.uniformBlockCount = 1;
    drawDesc.uniformBlocks[0].name = "Frame";
    drawDesc.uniformBlocks[0].slot = 0;
    drawDesc.storageBufferCount = 1;
    drawDesc.storageBuffers[0].name = "Values";
    drawDesc.storageBuffers[0].slot = 0;
    drawDesc.depthTest = false;
    drawDesc.depthWrite = false;
    drawDesc.debugName = "sort plot";
    const prisma::PipelineHandle drawPipeline = driver->createPipeline(drawDesc);

    driver->destroy(bitonicShader);
    driver->destroy(transposeShader);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    const bool ready = sorter.work[0].valid() && sorter.work[1].valid() && original.valid() &&
                       sorter.params.valid() && plotLeft.valid() && plotRight.valid() &&
                       sorter.bitonic.valid() && sorter.transpose.valid() &&
                       drawPipeline.valid();
    if (!ready) log_error("compute sort: resource creation failed");

    sorter.expected = values;
    ct::sort(sorter.expected.data(), sorter.expected.data() + sorter.expected.size());
    sorter.next = 0;
    sorter.shown = 0;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.02f;
    pass.clearColor[1] = 0.02f;
    pass.clearColor[2] = 0.04f;

    bool passed = true;
    bool verified = false;
    std::uint32_t seed = 1;
    double previous = time_seconds();
    double clock = 0.0;
    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;

        const double now = time_seconds();
        double dt = now - previous;
        previous = now;
        if (dt > 0.1) dt = 0.1;
        clock += dt;

        const float margin = 12.0f;
        const float panelWidth = static_cast<float>(width) * 0.5f - 1.5f * margin;
        const float panelHeight = static_cast<float>(height) - 2.0f * margin;
        PlotUniforms plot;
        plot.params[0] = static_cast<float>(kCount);
        plot.params[1] = 2.2f / panelWidth;
        plot.params[2] = 2.2f / panelHeight;
        plot.params[3] = 0.45f;

        driver->beginFrame();
        driver->updateBuffer(plotLeft, 0, &plot, sizeof(plot));
        plot.params[3] = 1.0f;
        driver->updateBuffer(plotRight, 0, &plot, sizeof(plot));

        std::uint32_t count = 0;
        if (still)
            count = 1;
        else if (sorter.next < sorter.steps.size())
            count = static_cast<std::uint32_t>(clock / kStepSeconds);
        if (count > 0 && !(still && verified))
        {
            run(&sorter, count);
            clock -= static_cast<double>(count) * kStepSeconds;
            if (sorter.next == sorter.steps.size() && !verified)
            {
                passed = verify(&sorter) && passed;
                verified = true;
                clock = 0.0;
            }
        }
        if (!still && verified && clock > kHoldSeconds)
        {
            restart(&sorter, original, ++seed);
            verified = false;
            clock = 0.0;
        }

        driver->beginRenderPass(pass);
        driver->bindPipeline(drawPipeline);

        prisma::Viewport viewport;
        viewport.y = margin;
        viewport.height = panelHeight;
        viewport.width = panelWidth;
        viewport.x = margin;
        driver->setViewport(viewport);
        driver->bindUniformBuffer(0, plotLeft, 0, sizeof(PlotUniforms));
        driver->bindStorageBuffer(0, original, 0, sorter.bytes);
        driver->draw(4, 0, kCount);

        viewport.x = static_cast<float>(width) * 0.5f + 0.5f * margin;
        driver->setViewport(viewport);
        driver->bindUniformBuffer(0, plotRight, 0, sizeof(PlotUniforms));
        driver->bindStorageBuffer(0, sorter.work[sorter.shown], 0, sorter.bytes);
        driver->draw(4, 0, kCount);
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    if (ready && !verified)
    {
        driver->beginFrame();
        run(&sorter, static_cast<std::uint32_t>(sorter.steps.size()));
        passed = verify(&sorter) && passed;
        driver->endFrame();
    }

    driver->destroy(drawPipeline);
    driver->destroy(sorter.transpose);
    driver->destroy(sorter.bitonic);
    driver->destroy(plotRight);
    driver->destroy(plotLeft);
    driver->destroy(sorter.params);
    driver->destroy(sorter.work[1]);
    driver->destroy(original);
    driver->destroy(sorter.work[0]);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready && passed ? 0 : 1;
}
