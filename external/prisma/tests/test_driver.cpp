#include "Check.h"
#include "Headless.h"
#include "platform.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"
#include "prisma/rhi/gl/GL.h"

#include <string.h>
#include <time.h>

#include "adjacency.geom.h"
#include "array.frag.h"
#include "count.frag.h"
#include "cube.frag.h"
#include "cube_array.frag.h"
#include "fill.comp.h"
#include "flat.frag.h"
#include "flat.vert.h"
#include "instanced.frag.h"
#include "instanced.vert.h"
#include "lod.frag.h"
#include <ct/vector.hpp>

#include "blocks.frag.h"
#include "no_buffer.vert.h"
#include "notch.tesc.h"
#include "notch.tese.h"
#include "plain.vert.h"
#include "points.geom.h"
#include "red.frag.h"
#include "scaled.frag.h"
#include "shadow.frag.h"
#include "storage.frag.h"
#include "textured.frag.h"
#include "textured.vert.h"
#include "two_targets.frag.h"
#include "vertices.comp.h"
#include "volume.frag.h"


namespace
{

int messages = 0;
char lastMessage[256];

void captureLog(const char* message)
{
    ++messages;
    strncpy(lastMessage, message, sizeof(lastMessage) - 1);
    lastMessage[sizeof(lastMessage) - 1] = '\0';
}

PlatformWindow* contextWindow = nullptr;

bool makeCurrentOn(void* user)
{
    window_make_current_on(static_cast<PlatformWindow*>(user), contextWindow);
    return true;
}

bool makeCurrent(void* user)
{
    window_make_current(static_cast<PlatformWindow*>(user));
    return true;
}

void swapBuffers(void* user) { window_swap(static_cast<PlatformWindow*>(user)); }

double nowSeconds()
{
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) * 1e-9;
}

void breathe()
{
    timespec pause = { 0, 1000000 };
    nanosleep(&pause, nullptr);
}

void pump(PlatformWindow* window)
{
    if (window) window_begin_frame(window);
}

bool hasArgument(int argc, char** argv, const char* name)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], name) == 0) return true;
    return false;
}

const char* const* instanceExtensions(void*, std::uint32_t* count)
{
    return vulkan_instance_extensions(count);
}

bool createSurface(void* user, void* instance, std::uint64_t* surface)
{
    return vulkan_create_surface(static_cast<PlatformWindow*>(user), instance, nullptr, surface);
}

void framebufferSize(void* user, std::uint32_t* width, std::uint32_t* height)
{
    int w = 0;
    int h = 0;
    window_get_framebuffer_size(static_cast<PlatformWindow*>(user), &w, &h);
    *width = static_cast<std::uint32_t>(w);
    *height = static_cast<std::uint32_t>(h);
}

const float kCoveringTriangle[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };

const float kLeftHalfQuad[8] = { -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, -1.0f, 1.0f };
const std::uint16_t kQuadIndices[6] = { 0, 1, 2, 0, 2, 3 };

struct Instance
{
    float place[4];
    unsigned char color[4];
};

const Instance kInstances[2] = {
    { { -0.5f, 0.0f, 0.25f, 0.0f }, { 255, 0, 0, 255 } },
    { { 0.5f, 0.0f, 0.25f, 0.0f }, { 0, 0, 255, 255 } },
};

const unsigned char kFourTexels[16] = {
    255,
    0,
    0,
    255,
    0,
    255,
    0,
    255,
    0,
    0,
    255,
    255,
    255,
    255,
    255,
    255,
};

const unsigned char kBlueTexels[16] = { 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    255 };

struct Params
{
    float color[4];
    float place[4];
};

enum ParamIndex
{
    kParamGreen,
    kParamNearBlue,
    kParamFarRed,
    kParamBehindNear,
    kParamBeyondFar,
    kParamHalfWhite,
    kParamRed,
    kParamNearBright,
    kParamFarGreen,
    kParamHalf,
    kParamSliceZero,
    kParamSliceOne,
    kParamSliceQuarter,
    kParamSliceThreeQuarters,
    kParamCubePositiveX,
    kParamCubeNegativeY,
    kParamCubeNegativeZ,
    kParamCubeEdge,
    kParamWhite,
    kParamMaxColor,
    kParamQuarterGrey,
    kParamBlueMid,
    kParamRedMid,
    kParamGreenMid,
    kParamCubeOneNegativeY,
    kParamQuad,
    kParamLevelOne,
    kParamCoverage,
    kParamLevelTwo,
    kParamCount
};

enum
{
    kMaxParamStride = 1024
};

void setParams(unsigned char* bytes, unsigned int stride, int index, const Params& value)
{
    memcpy(bytes + stride * index, &value, sizeof(Params));
}

prisma::Driver* readDriver = nullptr;

prisma::RenderPassDesc keep(const prisma::RenderPassDesc& pass)
{
    prisma::RenderPassDesc next = pass;
    next.colorLoad = prisma::LoadOp::Load;
    next.depthLoad = prisma::LoadOp::Load;
    return next;
}

prisma::ShaderHandle makeShader(prisma::Driver* driver, const prisma::ShaderBlob& blob)
{
    return driver->createShader(prisma::shaderDesc(blob, driver->caps()));
}

bool pixelIs(int x, int y, int r, int g, int b, int tolerance = 1)
{
    unsigned char pixel[4] = { 0, 0, 0, 0 };
    prisma::Rect rect;
    rect.x = x;
    rect.y = 240 - 1 - y;
    rect.width = 1;
    rect.height = 1;
    if (!readDriver->readPixels(prisma::RenderTarget(), rect, pixel))
    {
        printf("pixel (%d,%d) could not be read\n", x, y);
        return false;
    }
    const int dr = pixel[0] - r;
    const int dg = pixel[1] - g;
    const int db = pixel[2] - b;
    const bool same = dr >= -tolerance && dr <= tolerance && dg >= -tolerance && dg <= tolerance &&
                      db >= -tolerance && db <= tolerance;
    if (!same) printf("pixel (%d,%d) is %d,%d,%d\n", x, y, pixel[0], pixel[1], pixel[2]);
    return same;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace prisma;

    static unsigned char paramBytes[kMaxParamStride * kParamCount];

    const bool useVulkan = hasArgument(argc, argv, "vulkan");
    const bool headlessMode = hasArgument(argc, argv, "headless");

    WindowConfig config = {};
    config.title = "prisma test";
    config.width = 320;
    config.height = 240;
    config.x = WINDOW_POS_CENTERED;
    config.y = WINDOW_POS_CENTERED;
    config.monitor = MONITOR_CURRENT;

    PlatformWindow* window = nullptr;
    headless::Context headlessContext;
    headless::Surface headlessMain;
    GLPlatform gl;
    VulkanPlatform vulkan;
    if (headlessMode)
    {
        headlessMain.width = 320;
        headlessMain.height = 240;
        if (!useVulkan)
        {
#ifdef PRISMA_GLES
            const bool es = true;
#else
            const bool es = false;
#endif
            if (!headless::openContext(&headlessContext, es, true) ||
                    !headless::openSurface(&headlessContext, 320, 240, &headlessMain))
            {
                printf("headless OpenGL context could not be created\n");
                return 1;
            }
        }
#ifdef PRISMA_TEST_VULKAN
        gl = headless::glPlatform(&headlessMain);
        vulkan = headless::vulkanPlatform(&headlessMain);
#else
        gl = headless::glPlatform(&headlessMain);
#endif
    }
    else
    {
        if (!platform_init())
        {
            printf("platform: %s\n", platform_get_error());
            return 1;
        }
        if (useVulkan)
        {
            if (!vulkan_supported())
            {
                printf("vulkan is not supported\n");
                platform_shutdown();
                return 1;
            }
            config.render = RENDER_VULKAN;
            window = window_create(&config);
        }
        else
        {
            config.render = RENDER_GL;
            config.gl.debug = true;
#ifdef PRISMA_GLES
            config.gl.profile = GL_PROFILE_ES;
            config.gl.major = 3;
            for (int minor = 2; minor >= 0 && !window; --minor)
            {
                config.gl.minor = minor;
                window = window_create(&config);
            }
#else
            config.gl.profile = GL_PROFILE_CORE;
            config.gl.major = 4;
            config.gl.minor = 6;
            window = window_create(&config);
#endif
        }
        if (!window)
        {
            printf("window: %s\n", platform_get_error());
            platform_shutdown();
            return 1;
        }

        gl.user = window;
        gl.makeCurrent = makeCurrent;
        gl.swapBuffers = swapBuffers;
        gl.framebufferSize = framebufferSize;
        gl.getProcAddress = gl_proc_address;

        vulkan.user = window;
        vulkan.instanceExtensions = instanceExtensions;
        vulkan.createSurface = createSurface;
        vulkan.framebufferSize = framebufferSize;
    }

    DriverDesc desc;
    desc.type = useVulkan ? DriverType::Vulkan : DriverType::OpenGL;
    desc.gl = &gl;
    desc.vulkan = &vulkan;
    desc.log = captureLog;
    desc.debug = true;

    DriverError error = DriverError::None;
    Driver* driver = createDriver(desc, &error);
    CHECK(driver != nullptr);
    CHECK(error == DriverError::None);

    if (driver)
    {
        readDriver = driver;
        CHECK(driver->type() == desc.type);
        CHECK(driver->caps().maxTextureSize >= 2048);
        CHECK(driver->caps().maxColorTargets >= 4);

        if (driver->type() == DriverType::OpenGL)
        {
#ifdef PRISMA_GLES
            CHECK(driver->caps().gles);
            CHECK(driver->caps().versionMajor == 3);
#else
            CHECK(!driver->caps().gles);
            CHECK(driver->caps().versionMajor == 4 && driver->caps().versionMinor == 6);
#endif
            if (driver->caps().debugOutput)
            {
                messages = 0;
                glEnable(0xDEAD);
                CHECK(messages == 1);
                CHECK(strstr(lastMessage, "GL error") != nullptr);
            }
        }
        else
        {
            CHECK(!driver->caps().gles);
            CHECK(driver->caps().versionMajor == 1 && driver->caps().versionMinor >= 3);
        }

        messages = 0;
        ShaderDesc broken;
        broken.source = "this is not a shader";
        CHECK(!driver->createShader(broken).valid());
        CHECK(messages >= 1);

        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(kCoveringTriangle);
        bufferDesc.data = kCoveringTriangle;
        bufferDesc.debugName = "test vertices";
        const BufferHandle buffer = driver->createBuffer(bufferDesc);

        const ShaderHandle vertexShader = makeShader(driver, plain_vert);
        const ShaderHandle fragmentShader = makeShader(driver, red_frag);

        PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.fragmentShader = fragmentShader;
        pipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        pipelineDesc.vertexBufferCount = 1;
        pipelineDesc.attributeCount = 1;
        pipelineDesc.attributes[0].format = VertexFormat::Float2;
        pipelineDesc.debugName = "test pipeline";
        const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
        CHECK(buffer.valid());
        CHECK(pipeline.valid());

        RenderPassDesc pass;
        pass.clearColor[0] = 0.10f;
        pass.clearColor[1] = 0.25f;
        pass.clearColor[2] = 0.45f;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(pass);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 26, 64, 115));
        driver->beginRenderPass(keep(pass));
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        BufferDesc quadDesc;
        quadDesc.size = sizeof(kLeftHalfQuad);
        quadDesc.data = kLeftHalfQuad;
        const BufferHandle quad = driver->createBuffer(quadDesc);

        BufferDesc indexDesc;
        indexDesc.usage = BufferUsage::Index;
        indexDesc.size = sizeof(kQuadIndices);
        indexDesc.data = kQuadIndices;
        const BufferHandle quadIndices = driver->createBuffer(indexDesc);

        const unsigned int alignment = driver->caps().uniformBufferOffsetAlignment;
        const unsigned int stride = (sizeof(Params) + alignment - 1) / alignment * alignment;
        CHECK(alignment >= 1);
        CHECK(stride * kParamCount <= sizeof(paramBytes));

        const Params green = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamGreen, green);
        const Params nearBlue = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.2f, 0.0f } };
        setParams(paramBytes, stride, kParamNearBlue, nearBlue);
        const Params farRed = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.8f, 0.0f } };
        setParams(paramBytes, stride, kParamFarRed, farRed);
        const Params behindNear = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBehindNear, behindNear);
        const Params beyondFar = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBeyondFar, beyondFar);
        const Params halfWhite = { { 1.0f, 1.0f, 1.0f, 0.5f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamHalfWhite, halfWhite);
        const Params red = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamRed, red);
        const Params nearBright = { { 4.0f, 2.0f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.2f, 0.0f } };
        setParams(paramBytes, stride, kParamNearBright, nearBright);
        const Params farGreen = { { 0.0f, 9.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.8f, 0.0f } };
        setParams(paramBytes, stride, kParamFarGreen, farGreen);
        const Params half = { { 0.5f, 0.5f, 0.5f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamHalf, half);

        const Params sliceZero = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamSliceZero, sliceZero);
        const Params sliceOne = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } };
        setParams(paramBytes, stride, kParamSliceOne, sliceOne);
        const Params sliceQuarter = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.25f } };
        setParams(paramBytes, stride, kParamSliceQuarter, sliceQuarter);
        const Params sliceThreeQuarters = { { 0.0f, 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f, 0.75f } };
        setParams(paramBytes, stride, kParamSliceThreeQuarters, sliceThreeQuarters);
        const Params cubePositiveX = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubePositiveX, cubePositiveX);
        const Params cubeNegativeY = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubeNegativeY, cubeNegativeY);
        const Params cubeOneNegativeY = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, -1.0f, 0.0f, 1.0f } };
        setParams(paramBytes, stride, kParamCubeOneNegativeY, cubeOneNegativeY);
        const Params quadParams = { { 1.0f, 0.0f, 1.0f, 1.0f }, { 0.5f, 0.0f, 0.25f, 0.0f } };
        setParams(paramBytes, stride, kParamQuad, quadParams);
        const Params levelOne = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.25f, 0.0f } };
        setParams(paramBytes, stride, kParamLevelOne, levelOne);
        const Params coverage = { { 1.0f, 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamCoverage, coverage);
        const Params levelTwo = { { 1.0f, 1.0f, 0.0f, 1.0f }, { 2.0f, 0.0f, 0.25f, 0.0f } };
        setParams(paramBytes, stride, kParamLevelTwo, levelTwo);
        const Params cubeNegativeZ = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubeNegativeZ, cubeNegativeZ);
        const Params cubeEdge = { { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamCubeEdge, cubeEdge);

        const Params white = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamWhite, white);
        const Params maxColor = { { 0.2f, 0.8f, 0.2f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamMaxColor, maxColor);
        const Params quarterGrey = { { 0.25f, 0.25f, 0.25f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } };
        setParams(paramBytes, stride, kParamQuarterGrey, quarterGrey);
        const Params blueMid = { { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamBlueMid, blueMid);
        const Params redMid = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamRedMid, redMid);
        const Params greenMid = { { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.5f, 0.0f } };
        setParams(paramBytes, stride, kParamGreenMid, greenMid);

        BufferDesc paramsDesc;
        paramsDesc.usage = BufferUsage::Uniform;
        paramsDesc.size = stride * kParamCount;
        paramsDesc.data = paramBytes;
        paramsDesc.update = BufferUpdate::Dynamic;
        const BufferHandle params = driver->createBuffer(paramsDesc);
        CHECK(quad.valid());
        CHECK(quadIndices.valid());
        CHECK(params.valid());

        const ShaderHandle flatVertex = makeShader(driver, flat_vert);
        const ShaderHandle flatFragment = makeShader(driver, flat_frag);

        PipelineDesc flatPipelineDesc;
        flatPipelineDesc.vertexShader = flatVertex;
        flatPipelineDesc.fragmentShader = flatFragment;
        flatPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        flatPipelineDesc.vertexBufferCount = 1;
        flatPipelineDesc.attributeCount = 1;
        flatPipelineDesc.attributes[0].format = VertexFormat::Float2;
        flatPipelineDesc.uniformBlockCount = 1;
        flatPipelineDesc.uniformBlocks[0].name = "Params";
        flatPipelineDesc.uniformBlocks[0].slot = 2;
        const PipelineHandle flat = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.depthTest = true;
        const PipelineHandle flatDepth = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.blend = true;
        flatPipelineDesc.srcColor = BlendFactor::SrcAlpha;
        flatPipelineDesc.dstColor = BlendFactor::OneMinusSrcAlpha;
        const PipelineHandle flatBlend = driver->createPipeline(flatPipelineDesc);
        CHECK(flat.valid());
        CHECK(flatDepth.valid());
        CHECK(flatBlend.valid());

        RenderPassDesc black;
        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(12, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->drawIndexed(6, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 0, 255, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamNearBlue * stride, sizeof(Params));
        driver->bindPipeline(flatDepth);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamFarRed * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamBehindNear * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamBeyondFar * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamHalfWhite * stride, sizeof(Params));
        driver->bindPipeline(flatBlend);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 255, 2));

        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);

        Rect topLeft;
        topLeft.width = 160;
        topLeft.height = 120;
        driver->setScissor(topLeft);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        Rect whole;
        whole.width = 320;
        whole.height = 240;
        driver->setScissor(whole);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        Viewport bottomRight;
        bottomRight.x = 160.0f;
        bottomRight.y = 120.0f;
        bottomRight.width = 160.0f;
        bottomRight.height = 120.0f;
        driver->setViewport(bottomRight);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 255, 0, 0));
        CHECK(pixelIs(240, 180, 0, 0, 0));
        CHECK(pixelIs(80, 60, 0, 0, 0));

        driver->beginRenderPass(black);
        driver->endRenderPass();
        CHECK(pixelIs(80, 180, 0, 0, 0));
        CHECK(pixelIs(240, 60, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        BufferDesc instanceDesc;
        instanceDesc.size = sizeof(kInstances);
        instanceDesc.data = kInstances;
        const BufferHandle instanceBuffer = driver->createBuffer(instanceDesc);

        const ShaderHandle instancedVertex = makeShader(driver, instanced_vert);
        const ShaderHandle instancedFragment = makeShader(driver, instanced_frag);

        PipelineDesc instancedPipelineDesc;
        instancedPipelineDesc.vertexShader = instancedVertex;
        instancedPipelineDesc.fragmentShader = instancedFragment;
        instancedPipelineDesc.vertexBufferCount = 2;
        instancedPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        instancedPipelineDesc.vertexBuffers[1].stride = sizeof(Instance);
        instancedPipelineDesc.vertexBuffers[1].step = VertexStep::Instance;
        instancedPipelineDesc.attributeCount = 3;
        instancedPipelineDesc.attributes[0].location = 0;
        instancedPipelineDesc.attributes[0].format = VertexFormat::Float2;
        instancedPipelineDesc.attributes[1].location = 1;
        instancedPipelineDesc.attributes[1].format = VertexFormat::Float4;
        instancedPipelineDesc.attributes[1].buffer = 1;
        instancedPipelineDesc.attributes[2].location = 2;
        instancedPipelineDesc.attributes[2].format = VertexFormat::UByte4Norm;
        instancedPipelineDesc.attributes[2].offset = sizeof(float) * 4;
        instancedPipelineDesc.attributes[2].buffer = 1;
        const PipelineHandle instanced = driver->createPipeline(instancedPipelineDesc);
        CHECK(instanceBuffer.valid());
        CHECK(instanced.valid());

        messages = 0;
        instancedPipelineDesc.attributes[2].buffer = 2;
        CHECK(!driver->createPipeline(instancedPipelineDesc).valid());
        CHECK(messages == 1);

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, 0);
        driver->draw(3, 0, 2);
        driver->endRenderPass();
        CHECK(pixelIs(64, 108, 255, 0, 0));
        CHECK(pixelIs(224, 108, 0, 0, 255));

        driver->beginRenderPass(black);
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, sizeof(Instance));
        driver->draw(3, 0, 1);
        driver->endRenderPass();
        CHECK(pixelIs(224, 108, 0, 0, 255));
        CHECK(pixelIs(64, 108, 0, 0, 0));
        CHECK(messages == 0);

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(instanced);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindVertexBuffer(1, instanceBuffer, sizeof(Instance));
        driver->draw(3, 0, 2);
        CHECK(messages == 1);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        driver->destroy(instanced);
        driver->destroy(instancedVertex);
        driver->destroy(instancedFragment);
        driver->destroy(instanceBuffer);

        const ShaderHandle noBufferVertex = makeShader(driver, no_buffer_vert);
        PipelineDesc noBufferPipelineDesc;
        noBufferPipelineDesc.vertexShader = noBufferVertex;
        noBufferPipelineDesc.fragmentShader = fragmentShader;
        const PipelineHandle noBuffer = driver->createPipeline(noBufferPipelineDesc);
        CHECK(noBuffer.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->draw(3, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->bindPipeline(noBuffer);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        CHECK(pixelIs(2, 2, 255, 0, 0));
        CHECK(messages == 0);
        driver->beginRenderPass(keep(black));
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(1000, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->updateBuffer(params, 0, &green, sizeof(green));
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        driver->updateBuffer(params, 0xFFFFFFF0u, &green, 0x20u);
        CHECK(messages == 1);
        driver->destroy(noBuffer);
        driver->destroy(noBufferVertex);

        TextureDesc textureDesc;
        textureDesc.width = 2;
        textureDesc.height = 2;
        textureDesc.data = kFourTexels;
        textureDesc.debugName = "test texture";
        const TextureHandle texture = driver->createTexture(textureDesc);
        CHECK(texture.valid());
        messages = 0;
        CHECK(!driver->createTexture(TextureDesc()).valid());
        CHECK(messages == 1);

        static unsigned char grey[16 * 16 * 4];
        TextureDesc mippedDesc;
        mippedDesc.width = 16;
        mippedDesc.height = 16;
        mippedDesc.mipLevels = 0;
        mippedDesc.data = grey;
        mippedDesc.generateMipmaps = true;
        const TextureHandle mipped = driver->createTexture(mippedDesc);
        CHECK(mipped.valid());

        SamplerDesc samplerDesc;
        samplerDesc.minFilter = Filter::Nearest;
        samplerDesc.magFilter = Filter::Nearest;
        samplerDesc.mipFilter = MipFilter::None;
        samplerDesc.addressU = AddressMode::ClampToEdge;
        samplerDesc.addressV = AddressMode::ClampToEdge;
        const SamplerHandle nearest = driver->createSampler(samplerDesc);
        CHECK(nearest.valid());

        const ShaderHandle texturedVertex = makeShader(driver, textured_vert);
        const ShaderHandle texturedFragment = makeShader(driver, textured_frag);

        PipelineDesc texturedPipelineDesc;
        texturedPipelineDesc.vertexShader = texturedVertex;
        texturedPipelineDesc.fragmentShader = texturedFragment;
        texturedPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        texturedPipelineDesc.vertexBufferCount = 1;
        texturedPipelineDesc.attributeCount = 1;
        texturedPipelineDesc.attributes[0].format = VertexFormat::Float2;
        texturedPipelineDesc.textureCount = 1;
        texturedPipelineDesc.textures[0].name = "uTexture";
        texturedPipelineDesc.textures[0].slot = 3;
        const PipelineHandle textured = driver->createPipeline(texturedPipelineDesc);
        CHECK(textured.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, texture, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 60, 255, 0, 0));
        CHECK(pixelIs(240, 60, 0, 255, 0));
        CHECK(pixelIs(80, 180, 0, 0, 255));
        CHECK(pixelIs(240, 180, 255, 255, 255));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        {
            const TextureHandle edited = driver->createTexture(textureDesc);
            const TextureHandle bystander = driver->createTexture(textureDesc);
            CHECK(edited.valid());
            CHECK(bystander.valid());

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(0, edited, nearest);
            driver->bindTexture(3, bystander, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();

            driver->updateTexture(edited, 0, 0, kBlueTexels);

            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, bystander, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(80, 60, 255, 0, 0));

            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, edited, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(80, 60, 0, 0, 255));
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(bystander);
            driver->destroy(edited);
        }

        {
            const std::uint32_t side = 2048;
            ct::Vector<unsigned char> texels;
            texels.resize(static_cast<size_t>(side) * side * 4);
            TextureDesc largeDesc;
            largeDesc.width = side;
            largeDesc.height = side;
            TextureHandle large[5];
            messages = 0;
            for (int t = 0; t < 5; ++t)
            {
                for (size_t i = 0; i < texels.size(); i += 4)
                {
                    texels[i] = static_cast<unsigned char>(40 * (t + 1));
                    texels[i + 1] = static_cast<unsigned char>(200 - 30 * t);
                    texels[i + 2] = static_cast<unsigned char>(10 * t);
                    texels[i + 3] = 255;
                }
                largeDesc.data = texels.data();
                large[t] = driver->createTexture(largeDesc);
                CHECK(large[t].valid());
            }
            pump(window);
            driver->beginFrame();
            for (int t = 0; t < 5; ++t)
            {
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, large[t], nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 40 * (t + 1), 200 - 30 * t, 10 * t));
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            for (int t = 0; t < 5; ++t) driver->destroy(large[t]);
        }

        if (driver->caps().floatColorTargets)
        {
            TextureDesc hdrDesc;
            hdrDesc.format = TextureFormat::RGBA16F;
            hdrDesc.width = 64;
            hdrDesc.height = 64;
            hdrDesc.usage = kTextureSampled | kTextureRenderTarget;
            const TextureHandle hdr = driver->createTexture(hdrDesc);
            hdrDesc.format = TextureFormat::Depth32F;
            hdrDesc.usage = kTextureRenderTarget;
            const TextureHandle hdrDepth = driver->createTexture(hdrDesc);
            CHECK(hdr.valid());
            CHECK(hdrDepth.valid());

            const ShaderHandle scaledFragment = makeShader(driver, scaled_frag);
            texturedPipelineDesc.fragmentShader = scaledFragment;
            const PipelineHandle scaled = driver->createPipeline(texturedPipelineDesc);
            texturedPipelineDesc.fragmentShader = texturedFragment;
            CHECK(scaled.valid());

            flatPipelineDesc.blend = false;
            flatPipelineDesc.depthTest = true;
            flatPipelineDesc.targets.window = false;
            flatPipelineDesc.targets.colorCount = 1;
            flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA16F;
            flatPipelineDesc.targets.depth = TextureFormat::Depth32F;
            const PipelineHandle flatHdr = driver->createPipeline(flatPipelineDesc);
            CHECK(flatHdr.valid());

            RenderPassDesc offscreen;
            offscreen.colors[0].texture = hdr;
            offscreen.colorCount = 1;
            offscreen.depth.texture = hdrDepth;
            offscreen.depthStore = StoreOp::Discard;

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(offscreen);
            driver->bindUniformBuffer(2, params, kParamNearBright * stride, sizeof(Params));
            driver->bindPipeline(flatHdr);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->bindUniformBuffer(2, params, kParamFarGreen * stride, sizeof(Params));
            driver->draw(3, 0);
            driver->endRenderPass();

            driver->beginRenderPass(black);
            driver->bindPipeline(scaled);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, hdr, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 128, 32, 2));
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(flatHdr);
            driver->destroy(scaled);
            driver->destroy(scaledFragment);
            driver->destroy(hdrDepth);
            driver->destroy(hdr);
        }

        TextureDesc srgbDesc;
        srgbDesc.format = TextureFormat::RGBA8Srgb;
        srgbDesc.width = 16;
        srgbDesc.height = 16;
        srgbDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle srgbTarget = driver->createTexture(srgbDesc);
        srgbDesc.format = TextureFormat::RGB10A2;
        const TextureHandle tenBitTarget = driver->createTexture(srgbDesc);
        CHECK(srgbTarget.valid());
        CHECK(tenBitTarget.valid());

        flatPipelineDesc.blend = false;
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.targets.window = false;
        flatPipelineDesc.targets.colorCount = 1;
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA8Srgb;
        flatPipelineDesc.targets.depth = TextureFormat::None;
        const PipelineHandle flatSrgb = driver->createPipeline(flatPipelineDesc);
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGB10A2;
        const PipelineHandle flatTenBit = driver->createPipeline(flatPipelineDesc);
        CHECK(flatSrgb.valid());
        CHECK(flatTenBit.valid());

        RenderPassDesc srgbPass;
        srgbPass.colors[0].texture = srgbTarget;
        srgbPass.colorCount = 1;
        RenderPassDesc tenBitPass;
        tenBitPass.colors[0].texture = tenBitTarget;
        tenBitPass.colorCount = 1;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(srgbPass);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flatSrgb);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        driver->beginRenderPass(tenBitPass);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flatTenBit);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, srgbTarget, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, tenBitTarget, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 128, 128, 2));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);
        driver->destroy(flatTenBit);
        driver->destroy(flatSrgb);
        driver->destroy(tenBitTarget);
        driver->destroy(srgbTarget);

        TextureDesc targetDesc;
        targetDesc.width = 32;
        targetDesc.height = 32;
        targetDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle first = driver->createTexture(targetDesc);
        const TextureHandle second = driver->createTexture(targetDesc);
        CHECK(first.valid());
        CHECK(second.valid());

        const ShaderHandle twoTargetsFragment = makeShader(driver, two_targets_frag);
        PipelineDesc twoTargetsPipelineDesc;
        twoTargetsPipelineDesc.vertexShader = vertexShader;
        twoTargetsPipelineDesc.fragmentShader = twoTargetsFragment;
        twoTargetsPipelineDesc.vertexBuffers[0].stride = sizeof(float) * 2;
        twoTargetsPipelineDesc.vertexBufferCount = 1;
        twoTargetsPipelineDesc.attributeCount = 1;
        twoTargetsPipelineDesc.attributes[0].format = VertexFormat::Float2;
        twoTargetsPipelineDesc.targets.window = false;
        twoTargetsPipelineDesc.targets.colorCount = 2;
        twoTargetsPipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
        twoTargetsPipelineDesc.targets.colors[1] = TextureFormat::RGBA8;
        const PipelineHandle twoTargets = driver->createPipeline(twoTargetsPipelineDesc);
        CHECK(twoTargets.valid());

        RenderPassDesc twoTargetsPass;
        twoTargetsPass.colors[0].texture = first;
        twoTargetsPass.colors[1].texture = second;
        twoTargetsPass.colorCount = 2;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(twoTargetsPass);
        driver->bindPipeline(twoTargets);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, first, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, second, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        RenderPassDesc invalidPass;
        invalidPass.colors[0].texture = texture;
        invalidPass.colorCount = 1;
        driver->beginRenderPass(invalidPass);
        driver->endRenderPass();
        CHECK(messages == 1);

        CHECK(driver->caps().maxAnisotropy >= 1.0f);
        SamplerDesc anisotropicDesc;
        anisotropicDesc.maxAnisotropy = 4.0f;
        const SamplerHandle anisotropic = driver->createSampler(anisotropicDesc);
        CHECK(anisotropic.valid());

        const ShaderHandle arrayFragment = makeShader(driver, array_frag);
        const ShaderHandle cubeFragment = makeShader(driver, cube_frag);
        const ShaderHandle volumeFragment = makeShader(driver, volume_frag);
        const ShaderHandle lodFragment = makeShader(driver, lod_frag);

        texturedPipelineDesc.uniformBlockCount = 1;
        texturedPipelineDesc.uniformBlocks[0].name = "Params";
        texturedPipelineDesc.uniformBlocks[0].slot = 2;
        texturedPipelineDesc.fragmentShader = arrayFragment;
        const PipelineHandle arrayPipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = cubeFragment;
        const PipelineHandle cubePipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = volumeFragment;
        const PipelineHandle volumePipeline = driver->createPipeline(texturedPipelineDesc);
        texturedPipelineDesc.fragmentShader = lodFragment;
        const PipelineHandle lodPipeline = driver->createPipeline(texturedPipelineDesc);
        CHECK(arrayPipeline.valid());
        CHECK(cubePipeline.valid());
        CHECK(volumePipeline.valid());
        CHECK(lodPipeline.valid());

        SamplerDesc volumeSamplerDesc;
        volumeSamplerDesc.minFilter = Filter::Nearest;
        volumeSamplerDesc.magFilter = Filter::Nearest;
        volumeSamplerDesc.mipFilter = MipFilter::None;
        volumeSamplerDesc.addressU = AddressMode::ClampToEdge;
        volumeSamplerDesc.addressV = AddressMode::ClampToEdge;
        volumeSamplerDesc.addressW = AddressMode::ClampToEdge;
        const SamplerHandle volumeSampler = driver->createSampler(volumeSamplerDesc);
        SamplerDesc lodSamplerDesc = volumeSamplerDesc;
        lodSamplerDesc.mipFilter = MipFilter::Nearest;
        const SamplerHandle lodSampler = driver->createSampler(lodSamplerDesc);
        CHECK(volumeSampler.valid());
        CHECK(lodSampler.valid());

        unsigned char arrayTexels[32];
        for (int i = 0; i < 8; ++i)
        {
            const unsigned char color[4] = { static_cast<unsigned char>(i < 4 ? 255 : 0),
                static_cast<unsigned char>(i < 4 ? 0 : 255), 0, 255 };
            memcpy(arrayTexels + i * 4, color, 4);
        }
        TextureDesc arrayDesc;
        arrayDesc.type = TextureType::Texture2DArray;
        arrayDesc.width = 2;
        arrayDesc.height = 2;
        arrayDesc.depth = 2;
        arrayDesc.data = arrayTexels;
        const TextureHandle arrayTexture = driver->createTexture(arrayDesc);
        CHECK(arrayTexture.valid());

        static const unsigned char faceColors[6][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
            { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
        unsigned char cubeTexels[96];
        for (int i = 0; i < 24; ++i)
        {
            const unsigned char* face = faceColors[i / 4];
            const unsigned char color[4] = { face[0], face[1], face[2], 255 };
            memcpy(cubeTexels + i * 4, color, 4);
        }
        TextureDesc cubeDesc;
        cubeDesc.type = TextureType::TextureCube;
        cubeDesc.width = 2;
        cubeDesc.height = 2;
        cubeDesc.data = cubeTexels;
        const TextureHandle cubeTexture = driver->createTexture(cubeDesc);
        CHECK(cubeTexture.valid());

        unsigned char volumeTexels[32];
        for (int i = 0; i < 8; ++i)
        {
            const unsigned char color[4] = { static_cast<unsigned char>(i < 4 ? 255 : 0), 0,
                static_cast<unsigned char>(i < 4 ? 0 : 255), 255 };
            memcpy(volumeTexels + i * 4, color, 4);
        }
        TextureDesc volumeDesc;
        volumeDesc.type = TextureType::Texture3D;
        volumeDesc.width = 2;
        volumeDesc.height = 2;
        volumeDesc.depth = 2;
        volumeDesc.data = volumeTexels;
        const TextureHandle volumeTexture = driver->createTexture(volumeDesc);
        CHECK(volumeTexture.valid());

        unsigned char redTexels[16];
        for (int i = 0; i < 4; ++i)
        {
            const unsigned char color[4] = { 255, 0, 0, 255 };
            memcpy(redTexels + i * 4, color, 4);
        }
        TextureDesc explicitDesc;
        explicitDesc.width = 2;
        explicitDesc.height = 2;
        explicitDesc.mipLevels = 0;
        explicitDesc.data = redTexels;
        const TextureHandle explicitMips = driver->createTexture(explicitDesc);
        CHECK(explicitMips.valid());

        TextureDesc layersDesc;
        layersDesc.type = TextureType::Texture2DArray;
        layersDesc.width = 4;
        layersDesc.height = 4;
        layersDesc.depth = 2;
        layersDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle layers = driver->createTexture(layersDesc);
        CHECK(layers.valid());

        TextureDesc mipTargetDesc;
        mipTargetDesc.width = 4;
        mipTargetDesc.height = 4;
        mipTargetDesc.mipLevels = 0;
        mipTargetDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle mipTarget = driver->createTexture(mipTargetDesc);
        CHECK(mipTarget.valid());

        flatPipelineDesc.blend = false;
        flatPipelineDesc.depthTest = false;
        flatPipelineDesc.targets.window = false;
        flatPipelineDesc.targets.colorCount = 1;
        flatPipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
        flatPipelineDesc.targets.depth = TextureFormat::None;
        const PipelineHandle flatLayer = driver->createPipeline(flatPipelineDesc);
        CHECK(flatLayer.valid());

        RenderPassDesc layerZeroPass;
        layerZeroPass.colors[0].texture = layers;
        layerZeroPass.colors[0].layer = 0;
        layerZeroPass.colorCount = 1;
        layerZeroPass.clearColor[0] = 0.0f;
        layerZeroPass.clearColor[2] = 1.0f;
        RenderPassDesc layerOnePass;
        layerOnePass.colors[0].texture = layers;
        layerOnePass.colors[0].layer = 1;
        layerOnePass.colorCount = 1;
        RenderPassDesc mipPass;
        mipPass.colors[0].texture = mipTarget;
        mipPass.colors[0].mip = 1;
        mipPass.colorCount = 1;
        mipPass.clearColor[0] = 1.0f;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));

        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, arrayTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        messages = 0;
        driver->updateTexture(arrayTexture, 0, 1, kBlueTexels);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(black);
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubePositiveX * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubeNegativeY * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 255, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, nearest);
        driver->bindUniformBuffer(2, params, kParamCubeNegativeZ * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 255));

        SamplerDesc seamlessDesc;
        seamlessDesc.mipFilter = MipFilter::None;
        seamlessDesc.addressU = AddressMode::ClampToEdge;
        seamlessDesc.addressV = AddressMode::ClampToEdge;
        seamlessDesc.addressW = AddressMode::ClampToEdge;
        const SamplerHandle seamless = driver->createSampler(seamlessDesc);
        CHECK(seamless.valid());
        driver->beginRenderPass(black);
        driver->bindPipeline(cubePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, cubeTexture, seamless);
        driver->bindUniformBuffer(2, params, kParamCubeEdge * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 127, 0, 127, 4));
        driver->destroy(seamless);

        driver->beginRenderPass(black);
        driver->bindPipeline(volumePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, volumeTexture, volumeSampler);
        driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(volumePipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, volumeTexture, volumeSampler);
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        driver->updateTexture(explicitMips, 1, 0, kBlueTexels);
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        unsigned char halfTexels[16];
        for (int i = 0; i < 4; ++i)
        {
            const unsigned char red[4] = { 255, 0, 0, 255 };
            const unsigned char blue[4] = { 0, 0, 255, 255 };
            memcpy(halfTexels + i * 4, i < 2 ? red : blue, 4);
        }
        driver->updateTexture(explicitMips, 0, 0, halfTexels);
        driver->generateMipmaps(explicitMips);
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, explicitMips, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        messages = 0;
        driver->generateMipmaps(explicitMips);
        CHECK(messages == 1);
        messages = 0;
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 0, 128, 3));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(layerZeroPass);
        driver->endRenderPass();
        driver->beginRenderPass(layerOnePass);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->bindPipeline(flatLayer);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, layers, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceZero * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(arrayPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, layers, nearest);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));

        {
            TextureDesc manyDesc;
            manyDesc.type = TextureType::Texture2DArray;
            manyDesc.width = 4;
            manyDesc.height = 4;
            manyDesc.depth = 24;
            manyDesc.usage = kTextureSampled | kTextureRenderTarget;
            const TextureHandle many = driver->createTexture(manyDesc);
            CHECK(many.valid());
            messages = 0;
            for (unsigned layer = 0; layer < 24; ++layer)
            {
                RenderPassDesc manyPass;
                manyPass.colors[0].texture = many;
                manyPass.colors[0].layer = layer;
                manyPass.colorCount = 1;
                manyPass.clearColor[0] = static_cast<float>(layer * 10) / 255.0f;
                driver->beginRenderPass(manyPass);
                driver->endRenderPass();
            }
            bool allLayers = true;
            for (unsigned layer = 0; layer < 24; ++layer)
            {
                unsigned char texel[4] = { 0, 0, 0, 0 };
                Rect rect;
                rect.width = 1;
                rect.height = 1;
                RenderTarget layerTarget;
                layerTarget.texture = many;
                layerTarget.layer = layer;
                allLayers = allLayers && driver->readPixels(layerTarget, rect, texel) &&
                            texel[0] == layer * 10;
            }
            CHECK(allLayers);
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            driver->destroy(many);
        }

        {
            const ShaderHandle blocksVertex = makeShader(driver, no_buffer_vert);
            const ShaderHandle blocksFragment = makeShader(driver, blocks_frag);
            PipelineDesc blocksDesc;
            blocksDesc.vertexShader = blocksVertex;
            blocksDesc.fragmentShader = blocksFragment;
            blocksDesc.debugName = "six uniform blocks";
            messages = 0;
            const PipelineHandle blocksPipeline = driver->createPipeline(blocksDesc);
            CHECK(blocksPipeline.valid());
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            const std::uint32_t blockStride =
                    (16 + driver->caps().uniformBufferOffsetAlignment - 1) /
                    driver->caps().uniformBufferOffsetAlignment *
                    driver->caps().uniformBufferOffsetAlignment;
            ct::Vector<unsigned char> blockBytes;
            blockBytes.resize(blockStride * 6);
            memset(blockBytes.data(), 0, blockBytes.size());
            const float blockValues[6][4] = { { 0.1f, 0, 0, 0 }, { 0, 0.2f, 0, 0 },
                { 0, 0, 0.3f, 0 }, { 0, 0, 0, 0 }, { 0, 0.1f, 0, 0 }, { 0.2f, 0, 0, 0 } };
            const float fourth[4] = { 0, 0, 0.4f, 0 };
            memcpy(blockBytes.data() + 0 * blockStride, blockValues[0], 16);
            memcpy(blockBytes.data() + 1 * blockStride, blockValues[1], 16);
            memcpy(blockBytes.data() + 2 * blockStride, blockValues[2], 16);
            memcpy(blockBytes.data() + 3 * blockStride, fourth, 16);
            memcpy(blockBytes.data() + 4 * blockStride, blockValues[4], 16);
            memcpy(blockBytes.data() + 5 * blockStride, blockValues[5], 16);
            BufferDesc blocksBufferDesc;
            blocksBufferDesc.usage = BufferUsage::Uniform;
            blocksBufferDesc.size = static_cast<std::uint32_t>(blockBytes.size());
            blocksBufferDesc.data = blockBytes.data();
            const BufferHandle blocksBuffer = driver->createBuffer(blocksBufferDesc);
            CHECK(blocksBuffer.valid());

            driver->beginRenderPass(black);
            driver->bindPipeline(blocksPipeline);
            const std::uint32_t slots[6] = { 0, 1, 2, 3, 5, 9 };
            for (std::uint32_t i = 0; i < 6; ++i)
                driver->bindUniformBuffer(slots[i], blocksBuffer, i * blockStride, 16);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 77, 77, 179, 2));
            CHECK(messages == 0);
            driver->destroy(blocksBuffer);
            driver->destroy(blocksPipeline);
            driver->destroy(blocksVertex);
            driver->destroy(blocksFragment);
        }

        driver->beginRenderPass(mipPass);
        driver->endRenderPass();
        driver->beginRenderPass(black);
        driver->bindPipeline(lodPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, mipTarget, lodSampler);
        driver->bindUniformBuffer(2, params, kParamSliceOne * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        driver->beginFrame();
        messages = 0;
        driver->beginFrame();
        CHECK(messages == 1);
        driver->endFrame();
        driver->present();

        {
            unsigned char initial[64];
            for (int i = 0; i < 64; ++i) initial[i] = static_cast<unsigned char>(i);
            BufferDesc streamDesc;
            streamDesc.usage = BufferUsage::Vertex;
            streamDesc.size = 64;
            streamDesc.data = initial;
            streamDesc.update = BufferUpdate::Stream;
            const BufferHandle streamed = driver->createBuffer(streamDesc);
            CHECK(streamed.valid());
            messages = 0;
            unsigned char patch[16];
            for (int frame = 0; frame < 8; ++frame)
            {
                for (int i = 0; i < 16; ++i) patch[i] = static_cast<unsigned char>(200 + frame);
                driver->beginFrame();
                driver->updateBuffer(streamed, 16, patch, 16);
                if (frame == 5)
                {
                    unsigned char second[4] = { 9, 9, 9, 9 };
                    driver->updateBuffer(streamed, 0, second, 4);
                }
                driver->endFrame();
                driver->present();
            }
            unsigned char result[64];
            memset(result, 0, sizeof(result));
            const bool read = driver->readBuffer(streamed, 0, 64, result);
            bool preserved = read;
            for (int i = 0; i < 64 && read; ++i)
            {
                unsigned char expected = static_cast<unsigned char>(i);
                if (i < 4) expected = 9;
                if (i >= 16 && i < 32) expected = 207;
                if (i >= 4 && i < 16) expected = static_cast<unsigned char>(i);
                if (result[i] != expected) preserved = false;
            }
            CHECK(read);
            CHECK(preserved);
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            driver->destroy(streamed);
        }

        messages = 0;
        RenderPassDesc badLayerPass;
        badLayerPass.colors[0].texture = layers;
        badLayerPass.colors[0].layer = 5;
        badLayerPass.colorCount = 1;
        driver->beginRenderPass(badLayerPass);
        driver->endRenderPass();
        CHECK(messages == 1);
        messages = 0;

        PipelineDesc stateDesc = flatPipelineDesc;
        stateDesc.blend = false;
        stateDesc.depthTest = false;
        stateDesc.targets = TargetFormats();

        PipelineDesc stencilWriteDesc = stateDesc;
        stencilWriteDesc.stencilTest = true;
        stencilWriteDesc.stencilFront.compare = CompareOp::Always;
        stencilWriteDesc.stencilFront.passOp = StencilOp::Replace;
        stencilWriteDesc.stencilBack = stencilWriteDesc.stencilFront;
        stencilWriteDesc.colorMask = 0;
        PipelineDesc stencilEqualDesc = stateDesc;
        stencilEqualDesc.stencilTest = true;
        stencilEqualDesc.stencilFront.compare = CompareOp::Equal;
        stencilEqualDesc.stencilBack = stencilEqualDesc.stencilFront;
        PipelineDesc stencilNotEqualDesc = stencilEqualDesc;
        stencilNotEqualDesc.stencilFront.compare = CompareOp::NotEqual;
        stencilNotEqualDesc.stencilBack = stencilNotEqualDesc.stencilFront;
        const PipelineHandle stencilWrite = driver->createPipeline(stencilWriteDesc);
        const PipelineHandle stencilEqual = driver->createPipeline(stencilEqualDesc);
        const PipelineHandle stencilNotEqual = driver->createPipeline(stencilNotEqualDesc);
        CHECK(stencilWrite.valid());
        CHECK(stencilEqual.valid());
        CHECK(stencilNotEqual.valid());

        RenderPassDesc keepStencil = keep(black);
        keepStencil.stencilLoad = LoadOp::Load;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilWrite);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(6, 0);
        driver->bindPipeline(stencilEqual);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));

        driver->beginRenderPass(keepStencil);
        driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilNotEqual);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 255, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc maskedDesc = stateDesc;
        maskedDesc.colorMask = kColorRed | kColorAlpha;
        const PipelineHandle masked = driver->createPipeline(maskedDesc);
        CHECK(masked.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamWhite * stride, sizeof(Params));
        driver->bindPipeline(masked);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc maxDesc = stateDesc;
        maxDesc.blend = true;
        maxDesc.srcColor = BlendFactor::One;
        maxDesc.dstColor = BlendFactor::One;
        maxDesc.srcAlpha = BlendFactor::One;
        maxDesc.dstAlpha = BlendFactor::One;
        maxDesc.colorBlendOp = BlendOp::Max;
        maxDesc.alphaBlendOp = BlendOp::Max;
        PipelineDesc reverseDesc = maxDesc;
        reverseDesc.colorBlendOp = BlendOp::ReverseSubtract;
        reverseDesc.alphaBlendOp = BlendOp::ReverseSubtract;
        const PipelineHandle blendMax = driver->createPipeline(maxDesc);
        const PipelineHandle blendReverse = driver->createPipeline(reverseDesc);
        CHECK(blendMax.valid());
        CHECK(blendReverse.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
        driver->bindPipeline(flat);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamMaxColor * stride, sizeof(Params));
        driver->bindPipeline(blendMax);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 128, 204, 128, 2));

        driver->beginRenderPass(keep(black));
        driver->bindUniformBuffer(2, params, kParamQuarterGrey * stride, sizeof(Params));
        driver->bindPipeline(blendReverse);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 64, 140, 64, 2));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc biasDepthDesc = stateDesc;
        biasDepthDesc.depthTest = true;
        biasDepthDesc.depthCompare = CompareOp::Less;
        PipelineDesc biasedDesc = biasDepthDesc;
        biasedDesc.depthBiasConstant = -1000.0f;
        const PipelineHandle unbiased = driver->createPipeline(biasDepthDesc);
        const PipelineHandle biased = driver->createPipeline(biasedDesc);
        CHECK(unbiased.valid());
        CHECK(biased.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindPipeline(unbiased);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindUniformBuffer(2, params, kParamBlueMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->bindUniformBuffer(2, params, kParamRedMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 255));

        driver->beginRenderPass(keep(black));
        driver->bindPipeline(biased);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindUniformBuffer(2, params, kParamGreenMid * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 255, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        TextureDesc shadowDesc;
        shadowDesc.format = TextureFormat::Depth32F;
        shadowDesc.width = 16;
        shadowDesc.height = 16;
        shadowDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle shadowMap = driver->createTexture(shadowDesc);
        CHECK(shadowMap.valid());

        SamplerDesc comparisonDesc;
        comparisonDesc.minFilter = Filter::Linear;
        comparisonDesc.magFilter = Filter::Linear;
        comparisonDesc.mipFilter = MipFilter::None;
        comparisonDesc.addressU = AddressMode::ClampToEdge;
        comparisonDesc.addressV = AddressMode::ClampToEdge;
        comparisonDesc.compare = true;
        comparisonDesc.compareOp = CompareOp::LessEqual;
        const SamplerHandle comparison = driver->createSampler(comparisonDesc);
        CHECK(comparison.valid());

        const ShaderHandle shadowFragment = makeShader(driver, shadow_frag);
        texturedPipelineDesc.fragmentShader = shadowFragment;
        const PipelineHandle shadowPipeline = driver->createPipeline(texturedPipelineDesc);
        CHECK(shadowPipeline.valid());

        RenderPassDesc shadowPass;
        shadowPass.depth.texture = shadowMap;
        shadowPass.clearDepth = 0.5f;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(shadowPass);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(shadowPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, shadowMap, comparison);
        driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 255, 255, 255));

        driver->beginRenderPass(black);
        driver->bindPipeline(shadowPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, shadowMap, comparison);
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(160, 120, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        PipelineDesc wireDesc = stateDesc;
        wireDesc.wireframe = true;
        const PipelineHandle wire = driver->createPipeline(wireDesc);
        CHECK(wire.valid());

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->bindPipeline(wire);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        if (driver->caps().wireframe)
        {
            CHECK(pixelIs(160, 120, 0, 0, 0));
        }
        else
        {
            CHECK(pixelIs(160, 120, 255, 0, 0));
        }
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        TextureDesc stencilColorDesc;
        stencilColorDesc.width = 32;
        stencilColorDesc.height = 32;
        stencilColorDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle stencilColor = driver->createTexture(stencilColorDesc);
        TextureDesc stencilDepthDesc;
        stencilDepthDesc.format = TextureFormat::Depth24Stencil8;
        stencilDepthDesc.width = 32;
        stencilDepthDesc.height = 32;
        stencilDepthDesc.usage = kTextureRenderTarget;
        const TextureHandle stencilDepth = driver->createTexture(stencilDepthDesc);
        CHECK(stencilColor.valid());
        CHECK(stencilDepth.valid());

        stencilWriteDesc.targets.window = false;
        stencilWriteDesc.targets.colorCount = 1;
        stencilWriteDesc.targets.colors[0] = TextureFormat::RGBA8;
        stencilWriteDesc.targets.depth = TextureFormat::Depth24Stencil8;
        stencilEqualDesc.targets = stencilWriteDesc.targets;
        const PipelineHandle stencilWriteOffscreen = driver->createPipeline(stencilWriteDesc);
        const PipelineHandle stencilEqualOffscreen = driver->createPipeline(stencilEqualDesc);
        CHECK(stencilWriteOffscreen.valid());
        CHECK(stencilEqualOffscreen.valid());

        RenderPassDesc stencilOffscreen;
        stencilOffscreen.colors[0].texture = stencilColor;
        stencilOffscreen.colorCount = 1;
        stencilOffscreen.depth.texture = stencilDepth;

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(stencilOffscreen);
        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
        driver->setStencilReference(1);
        driver->bindPipeline(stencilWriteOffscreen);
        driver->bindVertexBuffer(0, quad, 0);
        driver->bindIndexBuffer(quadIndices);
        driver->drawIndexed(6, 0);
        driver->bindPipeline(stencilEqualOffscreen);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        driver->beginRenderPass(black);
        driver->bindPipeline(textured);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, stencilColor, nearest);
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(80, 120, 255, 0, 0));
        CHECK(pixelIs(240, 120, 0, 0, 0));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        CHECK(driver->caps().maxSamples >= 4);
        const float kWedge[6] = { -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f };
        BufferDesc wedgeDesc;
        wedgeDesc.size = sizeof(kWedge);
        wedgeDesc.data = kWedge;
        const BufferHandle wedge = driver->createBuffer(wedgeDesc);

        TextureDesc msaaDesc;
        msaaDesc.width = 320;
        msaaDesc.height = 240;
        msaaDesc.samples = 4;
        msaaDesc.usage = kTextureRenderTarget;
        const TextureHandle msaaColor = driver->createTexture(msaaDesc);
        msaaDesc.format = TextureFormat::Depth32F;
        const TextureHandle msaaDepth = driver->createTexture(msaaDesc);
        TextureDesc resolvedDesc;
        resolvedDesc.width = 320;
        resolvedDesc.height = 240;
        resolvedDesc.usage = kTextureSampled | kTextureRenderTarget;
        const TextureHandle resolvedColor = driver->createTexture(resolvedDesc);
        resolvedDesc.format = TextureFormat::Depth32F;
        const TextureHandle resolvedDepth = driver->createTexture(resolvedDesc);
        CHECK(wedge.valid());
        CHECK(msaaColor.valid());
        CHECK(msaaDepth.valid());
        CHECK(resolvedColor.valid());
        CHECK(resolvedDepth.valid());

        messages = 0;
        msaaDesc.samples = 3;
        CHECK(!driver->createTexture(msaaDesc).valid());
        msaaDesc.samples = 4;
        msaaDesc.usage = kTextureSampled | kTextureRenderTarget;
        CHECK(!driver->createTexture(msaaDesc).valid());
        CHECK(messages == 2);

        PipelineDesc msaaPipelineDesc = flatPipelineDesc;
        msaaPipelineDesc.blend = false;
        msaaPipelineDesc.depthTest = true;
        msaaPipelineDesc.targets.window = false;
        msaaPipelineDesc.targets.colorCount = 1;
        msaaPipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
        msaaPipelineDesc.targets.depth = TextureFormat::Depth32F;
        msaaPipelineDesc.targets.samples = 4;
        const PipelineHandle msaaPipeline = driver->createPipeline(msaaPipelineDesc);
        msaaPipelineDesc.targets.samples = 1;
        const PipelineHandle singlePipeline = driver->createPipeline(msaaPipelineDesc);
        CHECK(msaaPipeline.valid());
        CHECK(singlePipeline.valid());

        RenderPassDesc msaaPass;
        msaaPass.colors[0].texture = msaaColor;
        msaaPass.colorCount = 1;
        msaaPass.depth.texture = msaaDepth;
        msaaPass.resolves[0].texture = resolvedColor;
        msaaPass.depthResolve.texture = resolvedDepth;
        msaaPass.colorStore = StoreOp::Discard;
        msaaPass.depthStore = StoreOp::Discard;
        RenderPassDesc badResolve = msaaPass;
        badResolve.resolves[0].texture = stencilColor;
        RenderPassDesc mixedSamples = msaaPass;
        mixedSamples.depth.texture = resolvedDepth;
        mixedSamples.depthResolve = RenderTarget();

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(badResolve);
        CHECK(messages == 1);
        driver->endRenderPass();
        driver->beginRenderPass(mixedSamples);
        CHECK(messages == 2);
        driver->endRenderPass();

        messages = 0;
        driver->beginRenderPass(msaaPass);
        driver->bindUniformBuffer(2, params, kParamRedMid * stride, sizeof(Params));
        driver->bindPipeline(singlePipeline);
        driver->bindVertexBuffer(0, wedge, 0);
        driver->draw(3, 0);
        CHECK(messages == 1);
        driver->bindTexture(3, msaaColor, nearest);
        CHECK(messages == 2);
        messages = 0;
        driver->bindPipeline(msaaPipeline);
        driver->bindVertexBuffer(0, wedge, 0);
        driver->draw(3, 0);
        driver->endRenderPass();

        {
            RenderTarget resolvedTarget;
            resolvedTarget.texture = resolvedColor;
            RenderTarget msaaTarget;
            msaaTarget.texture = msaaColor;
            Rect one;
            one.width = 1;
            one.height = 1;
            unsigned char inside[4] = { 0, 0, 0, 0 };
            unsigned char outside[4] = { 9, 9, 9, 9 };
            unsigned char lowEdge[4] = { 0, 0, 0, 0 };
            unsigned char highEdge[4] = { 0, 0, 0, 0 };
            one.x = 40;
            one.y = 120;
            CHECK(driver->readPixels(resolvedTarget, one, inside));
            one.x = 300;
            one.y = 20;
            CHECK(driver->readPixels(resolvedTarget, one, outside));
            one.x = 161;
            one.y = 179;
            CHECK(driver->readPixels(resolvedTarget, one, lowEdge));
            one.y = 60;
            CHECK(driver->readPixels(resolvedTarget, one, highEdge));
            CHECK(inside[0] == 255 && inside[1] == 0 && inside[2] == 0);
            CHECK(outside[0] == 0 && outside[1] == 0 && outside[2] == 0);
            CHECK(lowEdge[0] > 32 && lowEdge[0] < 224);
            CHECK(highEdge[0] > 32 && highEdge[0] < 224);
            if (failures)
                printf("msaa: inside %d, outside %d, edges %d %d\n", inside[0], outside[0],
                        lowEdge[0], highEdge[0]);
            CHECK(messages == 0);
            CHECK(!driver->readPixels(msaaTarget, one, inside));
            CHECK(messages == 1);
        }

        messages = 0;
        driver->beginRenderPass(black);
        driver->bindPipeline(shadowPipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->bindTexture(3, resolvedDepth, comparison);
        driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
        driver->draw(3, 0);
        driver->endRenderPass();
        CHECK(pixelIs(40, 120, 0, 0, 0));
        CHECK(pixelIs(300, 20, 255, 255, 255));
        CHECK(pixelIs(300, 220, 255, 255, 255));
        driver->endFrame();
        driver->present();
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);

        driver->destroy(singlePipeline);
        driver->destroy(msaaPipeline);
        driver->destroy(resolvedDepth);
        driver->destroy(resolvedColor);
        driver->destroy(msaaDepth);
        driver->destroy(msaaColor);
        driver->destroy(wedge);

        {
            unsigned char regionTexels[4 * 4 * 4];
            for (int i = 0; i < 16; ++i)
            {
                regionTexels[i * 4 + 0] = 255;
                regionTexels[i * 4 + 1] = 0;
                regionTexels[i * 4 + 2] = 0;
                regionTexels[i * 4 + 3] = 255;
            }
            unsigned char bluePatch[2 * 3 * 4];
            for (int i = 0; i < 6; ++i)
            {
                bluePatch[i * 4 + 0] = 0;
                bluePatch[i * 4 + 1] = 0;
                bluePatch[i * 4 + 2] = 255;
                bluePatch[i * 4 + 3] = 255;
            }
            TextureDesc regionDesc;
            regionDesc.width = 4;
            regionDesc.height = 4;
            regionDesc.data = regionTexels;
            const TextureHandle regionTexture = driver->createTexture(regionDesc);
            CHECK(regionTexture.valid());

            TextureRegion region;
            region.x = 2;
            region.y = 1;
            region.width = 2;
            region.height = 3;
            messages = 0;
            driver->updateTextureRegion(regionTexture, region, bluePatch);
            CHECK(messages == 0);
            TextureRegion outside = region;
            outside.width = 3;
            driver->updateTextureRegion(regionTexture, outside, bluePatch);
            CHECK(messages == 1);

            const unsigned char kRedBlock[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
            const unsigned char kGreenBlock[8] = { 0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0 };
            const unsigned char kBlueBlock[8] = { 0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0 };
            unsigned char bcData[16];
            memcpy(bcData, kRedBlock, 8);
            memcpy(bcData + 8, kGreenBlock, 8);
            const unsigned char kEtcData[16] = { 0xFF, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0x00, 0xFF,
                0x00, 0x00, 0, 0, 0, 0 };
            const unsigned char kAstcData[16] = { 0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF };

            TextureDesc bcDesc;
            bcDesc.format = TextureFormat::BC1;
            bcDesc.width = 8;
            bcDesc.height = 4;
            bcDesc.mipLevels = 2;
            bcDesc.data = bcData;
            TextureDesc etcDesc = bcDesc;
            etcDesc.format = TextureFormat::ETC2RGB8;
            etcDesc.mipLevels = 1;
            etcDesc.data = kEtcData;
            TextureDesc astcDesc;
            astcDesc.format = TextureFormat::ASTC4x4;
            astcDesc.width = 4;
            astcDesc.height = 4;
            astcDesc.data = kAstcData;

            messages = 0;
            const TextureHandle bcTexture = driver->createTexture(bcDesc);
            const TextureHandle etcTexture = driver->createTexture(etcDesc);
            const TextureHandle astcTexture = driver->createTexture(astcDesc);
            const Caps& formats = driver->caps();
            CHECK(formats.textureBC || formats.textureETC2);
            CHECK(bcTexture.valid() == formats.textureBC);
            CHECK(etcTexture.valid() == formats.textureETC2);
            CHECK(astcTexture.valid() == formats.textureASTC);
            CHECK(messages == (formats.textureBC ? 0 : 1) + (formats.textureETC2 ? 0 : 1) +
                                      (formats.textureASTC ? 0 : 1));
            printf("compressed textures: BC %d, ETC2 %d, ASTC %d\n", formats.textureBC,
                    formats.textureETC2, formats.textureASTC);

            messages = 0;
            TextureDesc targetDesc = formats.textureBC ? bcDesc : etcDesc;
            targetDesc.data = nullptr;
            targetDesc.usage = kTextureSampled | kTextureRenderTarget;
            CHECK(!driver->createTexture(targetDesc).valid());
            CHECK(messages == 1);

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, regionTexture, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(280, 210, 0, 0, 255));
            CHECK(pixelIs(200, 90, 0, 0, 255));
            CHECK(pixelIs(280, 30, 255, 0, 0));
            CHECK(pixelIs(120, 150, 255, 0, 0));

            if (bcTexture.valid())
            {
                driver->updateTexture(bcTexture, 1, 0, kBlueBlock);
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, bcTexture, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(80, 120, 255, 0, 0));
                CHECK(pixelIs(240, 120, 0, 255, 0));
                CHECK(messages == 0);

                TextureRegion block;
                block.x = 4;
                block.width = 4;
                block.height = 4;
                driver->updateTextureRegion(bcTexture, block, kBlueBlock);
                CHECK(messages == 0);
                block.x = 2;
                driver->updateTextureRegion(bcTexture, block, kBlueBlock);
                CHECK(messages == 1);
                driver->generateMipmaps(bcTexture);
                CHECK(messages == 2);
                messages = 0;

                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, bcTexture, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(80, 120, 255, 0, 0));
                CHECK(pixelIs(240, 120, 0, 0, 255));
            }
            if (etcTexture.valid())
            {
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, etcTexture, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(80, 120, 255, 2, 2, 3));
                CHECK(pixelIs(240, 120, 2, 255, 2, 3));
            }
            if (astcTexture.valid())
            {
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, astcTexture, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 255, 0, 0));
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(astcTexture);
            driver->destroy(etcTexture);
            driver->destroy(bcTexture);
            driver->destroy(regionTexture);
        }

        {
            const float kWedge[6] = { -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f };
            const float kFarCorner[2] = { 3.0f, -1.0f };
            BufferDesc sourceDesc;
            sourceDesc.size = sizeof(kWedge);
            sourceDesc.data = kWedge;
            const BufferHandle copySource = driver->createBuffer(sourceDesc);
            BufferDesc emptyDesc;
            emptyDesc.size = sizeof(kWedge);
            emptyDesc.update = BufferUpdate::Dynamic;
            const BufferHandle copied = driver->createBuffer(emptyDesc);
            CHECK(copySource.valid());
            CHECK(copied.valid());

            messages = 0;
            driver->copyBuffer(copySource, 0, copied, 0, sizeof(kWedge));
            driver->updateBuffer(copied, sizeof(float) * 2, kFarCorner, sizeof(kFarCorner));
            CHECK(messages == 0);
            driver->copyBuffer(copySource, 0, copied, 8, sizeof(kWedge));
            CHECK(messages == 1);
            driver->copyBuffer(quadIndices, 0, copied, 0, 4);
            CHECK(messages == 2);
            driver->copyBuffer(copied, 0, copied, 8, 8);
            CHECK(messages == 3);

            unsigned char redTexels4[64];
            unsigned char greenTexels4[64];
            for (int i = 0; i < 16; ++i)
            {
                const unsigned char red[4] = { 255, 0, 0, 255 };
                const unsigned char green[4] = { 0, 255, 0, 255 };
                memcpy(redTexels4 + i * 4, red, 4);
                memcpy(greenTexels4 + i * 4, green, 4);
            }
            TextureDesc copyDesc;
            copyDesc.width = 4;
            copyDesc.height = 4;
            copyDesc.data = redTexels4;
            const TextureHandle copyRed = driver->createTexture(copyDesc);
            copyDesc.data = greenTexels4;
            const TextureHandle copyGreen = driver->createTexture(copyDesc);
            copyDesc.data = nullptr;
            copyDesc.format = TextureFormat::RG8;
            const TextureHandle copyOther = driver->createTexture(copyDesc);
            CHECK(copyRed.valid());
            CHECK(copyGreen.valid());
            CHECK(copyOther.valid());

            TextureCopy patch;
            patch.source = copyRed;
            patch.destination = copyGreen;
            patch.destinationX = 2;
            patch.destinationY = 2;
            patch.width = 2;
            patch.height = 2;
            messages = 0;
            driver->copyTexture(patch);
            CHECK(messages == 0);
            TextureCopy wrong = patch;
            wrong.destination = copyOther;
            driver->copyTexture(wrong);
            CHECK(messages == 1);
            wrong = patch;
            wrong.destination = copyRed;
            driver->copyTexture(wrong);
            CHECK(messages == 2);
            wrong = patch;
            wrong.width = 3;
            driver->copyTexture(wrong);
            CHECK(messages == 3);

            TextureDesc depthCopyDesc;
            depthCopyDesc.format = TextureFormat::Depth32F;
            depthCopyDesc.width = 32;
            depthCopyDesc.height = 32;
            depthCopyDesc.usage = kTextureRenderTarget;
            const TextureHandle depthSource = driver->createTexture(depthCopyDesc);
            depthCopyDesc.usage = kTextureSampled;
            const TextureHandle depthCopied = driver->createTexture(depthCopyDesc);
            CHECK(depthSource.valid());
            CHECK(depthCopied.valid());
            RenderPassDesc depthFill;
            depthFill.depth.texture = depthSource;
            depthFill.clearDepth = 0.5f;
            TextureCopy depthCopy;
            depthCopy.source = depthSource;
            depthCopy.destination = depthCopied;
            depthCopy.width = 32;
            depthCopy.height = 32;

            const unsigned char kRedBlock[8] = { 0x00, 0xF8, 0x00, 0xF8, 0, 0, 0, 0 };
            const unsigned char kGreenBlock[8] = { 0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0 };
            const unsigned char kBlueBlock[8] = { 0x1F, 0x00, 0x1F, 0x00, 0, 0, 0, 0 };
            unsigned char blockSource[16];
            unsigned char blockTarget[16];
            memcpy(blockSource, kRedBlock, 8);
            memcpy(blockSource + 8, kGreenBlock, 8);
            memcpy(blockTarget, kBlueBlock, 8);
            memcpy(blockTarget + 8, kBlueBlock, 8);
            TextureHandle bcSource;
            TextureHandle bcCopied;
            if (driver->caps().textureBC && driver->caps().compressedTextureCopy)
            {
                TextureDesc blockDesc;
                blockDesc.format = TextureFormat::BC1;
                blockDesc.width = 8;
                blockDesc.height = 4;
                blockDesc.data = blockSource;
                bcSource = driver->createTexture(blockDesc);
                blockDesc.data = blockTarget;
                bcCopied = driver->createTexture(blockDesc);
                CHECK(bcSource.valid());
                CHECK(bcCopied.valid());
                TextureCopy blockCopy;
                blockCopy.source = bcSource;
                blockCopy.sourceX = 4;
                blockCopy.destination = bcCopied;
                blockCopy.width = 4;
                blockCopy.height = 4;
                driver->copyTexture(blockCopy);
            }

            static const unsigned char arrayFaceColors[6][3] = { { 255, 0, 0 }, { 0, 255, 0 },
                { 0, 0, 255 }, { 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 } };
            unsigned char cubeArrayTexels[2 * 6 * 4 * 4];
            for (int i = 0; i < 48; ++i)
            {
                const int face = (i / 4) % 6;
                const unsigned char* color = arrayFaceColors[i < 24 ? face : 5 - face];
                const unsigned char texel[4] = { color[0], color[1], color[2], 255 };
                memcpy(cubeArrayTexels + i * 4, texel, 4);
            }
            TextureDesc cubeArrayDesc;
            cubeArrayDesc.type = TextureType::TextureCubeArray;
            cubeArrayDesc.width = 2;
            cubeArrayDesc.height = 2;
            cubeArrayDesc.depth = 2;
            cubeArrayDesc.data = cubeArrayTexels;
            messages = 0;
            const TextureHandle cubeArray = driver->createTexture(cubeArrayDesc);
            CHECK(cubeArray.valid() == driver->caps().cubeArrays);
            CHECK(messages == (driver->caps().cubeArrays ? 0 : 1));
            ShaderHandle cubeArrayVertex;
            ShaderHandle cubeArrayFragment;
            PipelineHandle cubeArrayPipeline;
            if (cubeArray.valid())
            {
                cubeArrayFragment = makeShader(driver, cube_array_frag);
                cubeArrayVertex = makeShader(driver, textured_vert);
                PipelineDesc cubeArrayPipelineDesc = texturedPipelineDesc;
                cubeArrayPipelineDesc.vertexShader = cubeArrayVertex;
                cubeArrayPipelineDesc.fragmentShader = cubeArrayFragment;
                cubeArrayPipeline = driver->createPipeline(cubeArrayPipelineDesc);
                CHECK(cubeArrayPipeline.valid());
            }

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(depthFill);
            driver->copyTexture(depthCopy);
            CHECK(messages == 1);
            messages = 0;
            driver->endRenderPass();
            driver->copyTexture(depthCopy);

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
            driver->bindPipeline(flat);
            driver->bindVertexBuffer(0, copied, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(300, 10, 255, 0, 0));
            CHECK(pixelIs(20, 200, 255, 0, 0));
            CHECK(pixelIs(300, 200, 0, 0, 0));

            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, copyGreen, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(280, 210, 255, 0, 0));
            CHECK(pixelIs(200, 150, 255, 0, 0));
            CHECK(pixelIs(40, 30, 0, 255, 0));
            CHECK(pixelIs(280, 30, 0, 255, 0));

            driver->beginRenderPass(black);
            driver->bindPipeline(shadowPipeline);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, depthCopied, comparison);
            driver->bindUniformBuffer(2, params, kParamSliceQuarter * stride, sizeof(Params));
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 255, 255));
            driver->beginRenderPass(black);
            driver->bindPipeline(shadowPipeline);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, depthCopied, comparison);
            driver->bindUniformBuffer(2, params, kParamSliceThreeQuarters * stride, sizeof(Params));
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 0, 0, 0));

            if (bcCopied.valid())
            {
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, bcCopied, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(80, 120, 0, 255, 0));
                CHECK(pixelIs(240, 120, 0, 0, 255));
            }

            if (cubeArray.valid())
            {
                driver->beginRenderPass(black);
                driver->bindPipeline(cubeArrayPipeline);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, cubeArray, nearest);
                driver->bindUniformBuffer(2, params, kParamCubePositiveX * stride, sizeof(Params));
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 255, 0, 0));
                driver->beginRenderPass(black);
                driver->bindPipeline(cubeArrayPipeline);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, cubeArray, nearest);
                driver->bindUniformBuffer(2, params, kParamCubeOneNegativeY * stride,
                        sizeof(Params));
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 0, 0, 255));

                TextureCopy faceCopy;
                faceCopy.source = cubeArray;
                faceCopy.destination = cubeArray;
                faceCopy.destinationLayer = 6 + 3;
                faceCopy.width = 2;
                faceCopy.height = 2;
                driver->copyTexture(faceCopy);
                driver->beginRenderPass(black);
                driver->bindPipeline(cubeArrayPipeline);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, cubeArray, nearest);
                driver->bindUniformBuffer(2, params, kParamCubeOneNegativeY * stride,
                        sizeof(Params));
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 255, 0, 0));
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(cubeArrayPipeline);
            driver->destroy(cubeArrayFragment);
            driver->destroy(cubeArrayVertex);
            driver->destroy(cubeArray);
            driver->destroy(bcCopied);
            driver->destroy(bcSource);
            driver->destroy(depthCopied);
            driver->destroy(depthSource);
            driver->destroy(copyOther);
            driver->destroy(copyGreen);
            driver->destroy(copyRed);
            driver->destroy(copied);
            driver->destroy(copySource);
        }

        if (driver->caps().compute)
        {
            const ShaderHandle fillShader = makeShader(driver, fill_comp);
            const ShaderHandle verticesShader = makeShader(driver, vertices_comp);
            CHECK(fillShader.valid());
            CHECK(verticesShader.valid());

            ComputePipelineDesc fillDesc;
            fillDesc.shader = fillShader;
            fillDesc.uniformBlockCount = 1;
            fillDesc.uniformBlocks[0].name = "Params";
            fillDesc.uniformBlocks[0].slot = 2;
            fillDesc.storageTextureCount = 1;
            fillDesc.storageTextures[0].name = "uImage";
            fillDesc.storageTextures[0].slot = 0;
            const PipelineHandle fillPipeline = driver->createComputePipeline(fillDesc);
            ComputePipelineDesc verticesDesc;
            verticesDesc.shader = verticesShader;
            verticesDesc.storageBufferCount = 2;
            verticesDesc.storageBuffers[0].name = "Vertices";
            verticesDesc.storageBuffers[0].slot = 0;
            verticesDesc.storageBuffers[1].name = "Arguments";
            verticesDesc.storageBuffers[1].slot = 1;
            const PipelineHandle verticesPipeline = driver->createComputePipeline(verticesDesc);
            CHECK(fillPipeline.valid());
            CHECK(verticesPipeline.valid());
            messages = 0;
            fillDesc.shader = flatFragment;
            CHECK(!driver->createComputePipeline(fillDesc).valid());
            CHECK(messages == 1);

            TextureDesc filledDesc;
            filledDesc.width = 8;
            filledDesc.height = 8;
            filledDesc.usage = kTextureSampled | kTextureStorage;
            const TextureHandle filled = driver->createTexture(filledDesc);
            CHECK(filled.valid());
            messages = 0;
            filledDesc.format = TextureFormat::RGBA8Srgb;
            CHECK(!driver->createTexture(filledDesc).valid());
            CHECK(messages == 1);

            BufferDesc gpuVerticesDesc;
            gpuVerticesDesc.usage = BufferUsage::Storage;
            gpuVerticesDesc.size = sizeof(float) * 12;
            const BufferHandle gpuVertices = driver->createBuffer(gpuVerticesDesc);
            BufferDesc gpuArgumentsDesc;
            gpuArgumentsDesc.usage = BufferUsage::Storage;
            gpuArgumentsDesc.size = sizeof(DrawIndirectCommand);
            const BufferHandle gpuArguments = driver->createBuffer(gpuArgumentsDesc);
            CHECK(gpuVertices.valid());
            CHECK(gpuArguments.valid());

            DrawIndexedIndirectCommand halves[2];
            halves[0].indexCount = 3;
            halves[1].indexCount = 3;
            halves[1].firstIndex = 3;
            BufferDesc halvesDesc;
            halvesDesc.usage = BufferUsage::Indirect;
            halvesDesc.size = sizeof(halves);
            halvesDesc.data = halves;
            const BufferHandle halvesBuffer = driver->createBuffer(halvesDesc);
            const DispatchIndirectCommand once;
            BufferDesc onceDesc;
            onceDesc.usage = BufferUsage::Indirect;
            onceDesc.size = sizeof(once);
            onceDesc.data = &once;
            const BufferHandle onceBuffer = driver->createBuffer(onceDesc);
            CHECK(halvesBuffer.valid());
            CHECK(onceBuffer.valid());

            PipelineDesc wideDesc = flatPipelineDesc;
            wideDesc.targets = TargetFormats();
            wideDesc.blend = false;
            wideDesc.depthTest = false;
            wideDesc.vertexBuffers[0].stride = sizeof(float) * 4;
            const PipelineHandle wide = driver->createPipeline(wideDesc);
            CHECK(wide.valid());

            const float kTwoColors[8] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };
            BufferDesc colorsDesc;
            colorsDesc.usage = BufferUsage::Storage;
            colorsDesc.size = sizeof(kTwoColors);
            colorsDesc.data = kTwoColors;
            const BufferHandle colors = driver->createBuffer(colorsDesc);
            CHECK(colors.valid());
            ShaderHandle storageVertex;
            ShaderHandle storageFragment;
            PipelineHandle storagePipeline;
            if (driver->caps().storageBuffersInGraphics)
            {
                storageVertex = makeShader(driver, flat_vert);
                storageFragment = makeShader(driver, storage_frag);
                PipelineDesc storageDesc = wideDesc;
                storageDesc.vertexBuffers[0].stride = sizeof(float) * 2;
                storageDesc.vertexShader = storageVertex;
                storageDesc.fragmentShader = storageFragment;
                storageDesc.storageBufferCount = 1;
                storageDesc.storageBuffers[0].name = "Colors";
                storageDesc.storageBuffers[0].slot = 1;
                storagePipeline = driver->createPipeline(storageDesc);
                CHECK(storagePipeline.valid());
            }

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->dispatch(1, 1, 1);
            CHECK(messages == 1);
            driver->bindStorageTexture(0, texture, 0, StorageAccess::Write);
            CHECK(messages == 2);
            messages = 0;

            driver->beginComputePass();
            driver->bindPipeline(fillPipeline);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindStorageTexture(0, filled, 0, StorageAccess::Write);
            driver->dispatch(1, 1, 1);
            driver->bindPipeline(verticesPipeline);
            driver->bindStorageBuffer(0, gpuVertices, 0, sizeof(float) * 12);
            driver->bindStorageBuffer(1, gpuArguments, 0, sizeof(DrawIndirectCommand));
            driver->dispatch(1, 1, 1);
            driver->bindPipeline(flat);
            driver->dispatch(1, 1, 1);
            CHECK(messages == 1);
            messages = 0;
            driver->endComputePass();

            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, filled, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 0, 255, 0));
            CHECK(pixelIs(10, 10, 0, 255, 0));

            float generated[12] = {};
            CHECK(driver->readBuffer(gpuVertices, 0, sizeof(generated), generated));
            CHECK(generated[0] == -1.0f && generated[1] == -1.0f && generated[3] == 1.0f);
            CHECK(generated[4] == 3.0f && generated[5] == -1.0f);
            CHECK(generated[8] == -1.0f && generated[9] == 3.0f);
            DrawIndirectCommand generatedArguments;
            memset(&generatedArguments, 0xFF, sizeof(generatedArguments));
            CHECK(driver->readBuffer(gpuArguments, 0, sizeof(generatedArguments),
                    &generatedArguments));
            CHECK(generatedArguments.vertexCount == 3 && generatedArguments.instanceCount == 1);
            float secondColor[4] = { 0, 0, 0, 0 };
            CHECK(driver->readBuffer(colors, 16, sizeof(secondColor), secondColor));
            CHECK(secondColor[0] == 0.0f && secondColor[2] == 1.0f && secondColor[3] == 1.0f);
            messages = 0;
            CHECK(!driver->readBuffer(colors, 16, 32, secondColor));
            CHECK(!driver->readBuffer(BufferHandle(), 0, 4, secondColor));
            CHECK(messages == 2);
            messages = 0;
            const ReadbackHandle asyncVertices = driver->requestBufferReadback(gpuVertices, 16, 16);
            CHECK(asyncVertices.valid());
            CHECK(!driver->requestBufferReadback(gpuVertices, 40, 16).valid());
            CHECK(messages == 1);
            messages = 0;

            driver->beginComputePass();
            driver->bindPipeline(fillPipeline);
            driver->bindUniformBuffer(2, params, kParamBlueMid * stride, sizeof(Params));
            driver->bindStorageTexture(0, filled, 0, StorageAccess::Write);
            driver->dispatchIndirect(onceBuffer, 0);
            driver->endComputePass();

            driver->beginRenderPass(black);
            driver->bindPipeline(textured);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, filled, nearest);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 0, 0, 255));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
            driver->bindPipeline(wide);
            driver->bindVertexBuffer(0, gpuVertices, 0);
            driver->drawIndirect(gpuArguments, 0, 1);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 0, 0));
            CHECK(pixelIs(300, 10, 255, 0, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindPipeline(flat);
            driver->bindVertexBuffer(0, quad, 0);
            driver->bindIndexBuffer(quadIndices);
            driver->drawIndexedIndirect(halvesBuffer, 0, 2, sizeof(DrawIndexedIndirectCommand));
            driver->endRenderPass();
            CHECK(pixelIs(140, 40, 0, 255, 0));
            CHECK(pixelIs(20, 200, 0, 255, 0));
            CHECK(pixelIs(240, 120, 0, 0, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindPipeline(flat);
            driver->bindVertexBuffer(0, quad, 0);
            driver->bindIndexBuffer(quadIndices);
            driver->drawIndexedIndirect(halvesBuffer, sizeof(DrawIndexedIndirectCommand), 1);
            CHECK(messages == 0);
            driver->drawIndexedIndirect(halvesBuffer, sizeof(DrawIndexedIndirectCommand), 2);
            CHECK(messages == 1);
            messages = 0;
            driver->endRenderPass();
            CHECK(pixelIs(140, 40, 0, 0, 0));
            CHECK(pixelIs(20, 200, 0, 255, 0));

            if (storagePipeline.valid())
            {
                driver->beginRenderPass(black);
                driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
                driver->bindStorageBuffer(1, colors, 0, sizeof(kTwoColors));
                driver->bindPipeline(storagePipeline);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 0, 0, 255));
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            float asyncResult[4] = { 9, 9, 9, 9 };
            bool asyncReady = false;
            const double asyncStart = nowSeconds();
            for (int i = 0; nowSeconds() - asyncStart < 3.0 && !asyncReady; ++i)
            {
                breathe();
                pump(window);
                driver->beginFrame();
                driver->beginRenderPass(black);
                driver->endRenderPass();
                driver->endFrame();
                driver->present();
                asyncReady = driver->readbackResult(asyncVertices, asyncResult);
            }
            CHECK(asyncReady);
            CHECK(asyncResult[0] == 3.0f && asyncResult[1] == -1.0f && asyncResult[3] == 1.0f);
            driver->destroy(asyncVertices);
            driver->destroy(storagePipeline);
            driver->destroy(storageFragment);
            driver->destroy(storageVertex);
            driver->destroy(colors);
            driver->destroy(wide);
            driver->destroy(onceBuffer);
            driver->destroy(halvesBuffer);
            driver->destroy(gpuArguments);
            driver->destroy(gpuVertices);
            driver->destroy(filled);
            driver->destroy(verticesPipeline);
            driver->destroy(fillPipeline);
            driver->destroy(verticesShader);
            driver->destroy(fillShader);
        }
        printf("compute %d, indirect %d, storage buffers in graphics %d\n", driver->caps().compute,
                driver->caps().indirectDraw, driver->caps().storageBuffersInGraphics);

        {
            PipelineDesc reflectedDesc = texturedPipelineDesc;
            reflectedDesc.fragmentShader = cubeFragment;
            reflectedDesc.uniformBlockCount = 0;
            reflectedDesc.textureCount = 0;
            const PipelineHandle reflected = driver->createPipeline(reflectedDesc);
            CHECK(reflected.valid());

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindPipeline(reflected);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindTexture(3, cubeTexture, nearest);
            driver->bindUniformBuffer(2, params, kParamCubePositiveX * stride, sizeof(Params));
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 0, 0));
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            driver->destroy(reflected);
        }

        {
            unsigned char cornerTexels[64];
            for (int i = 0; i < 16; ++i)
            {
                const unsigned char texel[4] = { static_cast<unsigned char>(i == 0 ? 255 : 0), 0,
                    static_cast<unsigned char>(i == 0 ? 0 : 255), 255 };
                memcpy(cornerTexels + i * 4, texel, 4);
            }
            TextureDesc cornerDesc;
            cornerDesc.width = 4;
            cornerDesc.height = 4;
            cornerDesc.data = cornerTexels;
            const TextureHandle corner = driver->createTexture(cornerDesc);
            CHECK(corner.valid());
            RenderTarget cornerTarget;
            cornerTarget.texture = corner;
            Rect whole;
            whole.width = 4;
            whole.height = 4;
            Rect leftPixel;
            leftPixel.x = 80;
            leftPixel.y = 120;
            leftPixel.width = 2;
            leftPixel.height = 1;
            Rect rightPixel = leftPixel;
            rightPixel.x = 240;

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindPipeline(flat);
            driver->bindVertexBuffer(0, quad, 0);
            driver->bindIndexBuffer(quadIndices);
            driver->drawIndexed(6, 0);
            CHECK(!driver->requestReadback(RenderTarget(), leftPixel).valid());
            CHECK(messages == 1);
            messages = 0;
            driver->endRenderPass();
            const ReadbackHandle leftRead = driver->requestReadback(RenderTarget(), leftPixel);
            const ReadbackHandle rightRead = driver->requestReadback(RenderTarget(), rightPixel);
            const ReadbackHandle cornerRead = driver->requestReadback(cornerTarget, whole);
            CHECK(leftRead.valid());
            CHECK(rightRead.valid());
            CHECK(cornerRead.valid());
            unsigned char blocking[64];
            memset(blocking, 7, sizeof(blocking));
            CHECK(driver->readPixels(cornerTarget, whole, blocking));
            driver->endFrame();
            driver->present();

            unsigned char leftResult[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
            unsigned char rightResult[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
            unsigned char cornerResult[64];
            memset(cornerResult, 9, sizeof(cornerResult));
            int readbackFrames = 0;
            bool allRead = false;
            const double readbackStart = nowSeconds();
            for (; nowSeconds() - readbackStart < 3.0 && !allRead; ++readbackFrames)
            {
                breathe();
                pump(window);
                driver->beginFrame();
                driver->beginRenderPass(black);
                driver->endRenderPass();
                driver->endFrame();
                driver->present();
                allRead = driver->readbackResult(leftRead, leftResult) &&
                          driver->readbackResult(rightRead, rightResult) &&
                          driver->readbackResult(cornerRead, cornerResult);
            }
            CHECK(allRead);
            CHECK(leftResult[0] == 0 && leftResult[1] == 255 && leftResult[2] == 0);
            CHECK(leftResult[4] == 0 && leftResult[5] == 255 && leftResult[6] == 0);
            CHECK(rightResult[0] == 0 && rightResult[1] == 0 && rightResult[2] == 0);
            CHECK(memcmp(cornerResult, blocking, sizeof(blocking)) == 0);
            int redTexels = 0;
            for (int i = 0; i < 16; ++i)
                if (cornerResult[i * 4] == 255 && cornerResult[i * 4 + 2] == 0) ++redTexels;
            CHECK(redTexels == 1);
            CHECK(driver->readbackResult(cornerRead, cornerResult));
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            printf("readback: ready after %d frames\n", readbackFrames);

            driver->destroy(cornerRead);
            driver->destroy(rightRead);
            driver->destroy(leftRead);
            CHECK(!driver->readbackResult(leftRead, leftResult));
            driver->destroy(corner);
        }

        {
            static TextureHandle manyTextures[256];
            static BufferHandle manyBuffers[256];
            unsigned char greenTexels[16 * 16 * 4];
            for (int i = 0; i < 16 * 16; ++i)
            {
                greenTexels[i * 4 + 0] = 0;
                greenTexels[i * 4 + 1] = 255;
                greenTexels[i * 4 + 2] = 0;
                greenTexels[i * 4 + 3] = 255;
            }
            messages = 0;
            for (int round = 0; round < 3; ++round)
            {
                for (int i = 0; i < 256; ++i)
                {
                    if (manyTextures[i].valid()) continue;
                    TextureDesc manyDesc;
                    manyDesc.width = 16;
                    manyDesc.height = 16 + (i % 5) * 16;
                    manyDesc.mipLevels = (i % 3) == 0 ? 0 : 1;
                    manyDesc.data = i == 255 ? greenTexels : nullptr;
                    manyTextures[i] = driver->createTexture(manyDesc);
                    BufferDesc manyBufferDesc;
                    manyBufferDesc.size = 64 + static_cast<std::uint32_t>(i) * 37;
                    manyBufferDesc.update = (i % 2) ? BufferUpdate::Static : BufferUpdate::Dynamic;
                    manyBuffers[i] = driver->createBuffer(manyBufferDesc);
                    if (!manyTextures[i].valid() || !manyBuffers[i].valid()) ++failures;
                }
                pump(window);
                driver->beginFrame();
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, manyTextures[255], nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 200, 0, 255, 0));
                driver->endFrame();
                driver->present();
                for (int i = round; i < 255; i += 2)
                {
                    driver->destroy(manyTextures[i]);
                    driver->destroy(manyBuffers[i]);
                    manyTextures[i] = TextureHandle();
                    manyBuffers[i] = BufferHandle();
                }
            }
            for (int i = 0; i < 256; ++i)
            {
                driver->destroy(manyTextures[i]);
                driver->destroy(manyBuffers[i]);
            }
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
        }

        {
            CHECK(driver->caps().multipleWindows);
            PlatformWindow* secondWindow = nullptr;
            headless::Surface headlessSecond;
            bool haveSecond = false;
            if (headlessMode)
            {
                headlessSecond.width = 160;
                headlessSecond.height = 120;
                haveSecond = useVulkan ||
                             headless::openSurface(&headlessContext, 160, 120, &headlessSecond);
            }
            else
            {
                WindowConfig secondConfig = config;
                secondConfig.title = "prisma test, second window";
                secondConfig.width = 160;
                secondConfig.height = 120;
                secondWindow = window_create(&secondConfig);
                haveSecond = secondWindow != nullptr;
            }
            CHECK(haveSecond);
            if (haveSecond)
            {
                for (int i = 0; i < 5; ++i) pump(secondWindow);
                contextWindow = window;
                SwapchainDesc secondDesc;
                if (headlessMode)
                {
                    secondDesc.gl = headless::glPlatform(&headlessSecond);
#ifdef PRISMA_TEST_VULKAN
                    secondDesc.vulkan = headless::vulkanPlatform(&headlessSecond);
#endif
                }
                else
                {
                    secondDesc.gl.user = secondWindow;
                    secondDesc.gl.makeCurrent = makeCurrentOn;
                    secondDesc.gl.swapBuffers = swapBuffers;
                    secondDesc.gl.framebufferSize = framebufferSize;
                    secondDesc.vulkan.user = secondWindow;
                    secondDesc.vulkan.instanceExtensions = instanceExtensions;
                    secondDesc.vulkan.createSurface = createSurface;
                    secondDesc.vulkan.framebufferSize = framebufferSize;
                }
                messages = 0;
                const SwapchainHandle second = driver->createSwapchain(secondDesc);
                CHECK(second.valid());
                CHECK(!driver->createSwapchain(SwapchainDesc()).valid());
                CHECK(messages == 1);

                RenderPassDesc secondPass;
                secondPass.swapchain = second;
                RenderTarget secondTarget;
                secondTarget.swapchain = second;
                std::uint32_t secondWidth = 0;
                std::uint32_t secondHeight = 0;
                if (headlessMode)
                {
                    secondWidth = headlessSecond.width;
                    secondHeight = headlessSecond.height;
                }
                else
                    framebufferSize(secondWindow, &secondWidth, &secondHeight);
                Rect leftSide;
                leftSide.x = static_cast<std::int32_t>(secondWidth / 4);
                leftSide.y = static_cast<std::int32_t>(secondHeight / 2);
                leftSide.width = 1;
                leftSide.height = 1;
                Rect rightSide = leftSide;
                rightSide.x = static_cast<std::int32_t>(secondWidth * 3 / 4);

                messages = 0;
                for (int frame = 0; frame < 4; ++frame)
                {
                    pump(window);
                    pump(secondWindow);
                    driver->beginFrame();
                    driver->beginRenderPass(black);
                    driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
                    driver->bindPipeline(flat);
                    driver->bindVertexBuffer(0, buffer, 0);
                    driver->draw(3, 0);
                    driver->endRenderPass();
                    CHECK(pixelIs(240, 120, 0, 255, 0));
                    CHECK(pixelIs(80, 120, 0, 255, 0));
                    if (frame != 2)
                    {
                        driver->beginRenderPass(secondPass);
                        driver->bindUniformBuffer(2, params, kParamRed * stride, sizeof(Params));
                        driver->bindPipeline(flat);
                        driver->bindVertexBuffer(0, quad, 0);
                        driver->bindIndexBuffer(quadIndices);
                        driver->drawIndexed(6, 0);
                        driver->endRenderPass();

                        unsigned char leftPixel[4] = { 9, 9, 9, 9 };
                        unsigned char rightPixel[4] = { 9, 9, 9, 9 };
                        CHECK(driver->readPixels(secondTarget, leftSide, leftPixel));
                        CHECK(driver->readPixels(secondTarget, rightSide, rightPixel));
                        CHECK(leftPixel[0] == 255 && leftPixel[1] == 0 && leftPixel[2] == 0);
                        CHECK(rightPixel[0] == 0 && rightPixel[1] == 0 && rightPixel[2] == 0);
                    }
                    driver->endFrame();
                    driver->present();
                }
                CHECK(messages == 0);
                if (messages) printf("unexpected: %s\n", lastMessage);

                driver->destroy(second);
                messages = 0;
                pump(window);
                driver->beginFrame();
                driver->beginRenderPass(secondPass);
                CHECK(messages == 1);
                driver->endRenderPass();
                driver->beginRenderPass(black);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 0, 0, 0));
                driver->endFrame();
                driver->present();
                CHECK(messages == 1);
                if (secondWindow) window_destroy(secondWindow);
                else
                    headless::closeSurface(&headlessSecond);
            }
        }

        {
            CHECK(driver->caps().geometryShaders);
            const ShaderHandle pointsGeometry = makeShader(driver, points_geom);
            const ShaderHandle adjacencyGeometry = makeShader(driver, adjacency_geom);
            CHECK(pointsGeometry.valid());
            CHECK(adjacencyGeometry.valid());

            const float kPoint[2] = { 0.0f, 0.0f };
            BufferDesc pointDesc;
            pointDesc.size = sizeof(kPoint);
            pointDesc.data = kPoint;
            const BufferHandle pointBuffer = driver->createBuffer(pointDesc);
            const float kAdjacent[12] = { -1.0f, -1.0f, -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, -1.0f,
                -1.0f, 3.0f, -1.0f, -1.0f };
            BufferDesc adjacentDesc;
            adjacentDesc.size = sizeof(kAdjacent);
            adjacentDesc.data = kAdjacent;
            const BufferHandle adjacentBuffer = driver->createBuffer(adjacentDesc);
            CHECK(pointBuffer.valid());
            CHECK(adjacentBuffer.valid());

            PipelineDesc geometryDesc = flatPipelineDesc;
            geometryDesc.targets = TargetFormats();
            geometryDesc.blend = false;
            geometryDesc.depthTest = false;
            geometryDesc.topology = Topology::Points;
            geometryDesc.geometryShader = pointsGeometry;
            const PipelineHandle pointsPipeline = driver->createPipeline(geometryDesc);
            geometryDesc.depthTest = true;
            const PipelineHandle pointsDepthPipeline = driver->createPipeline(geometryDesc);
            geometryDesc.depthTest = false;
            geometryDesc.topology = Topology::TrianglesAdjacency;
            geometryDesc.geometryShader = adjacencyGeometry;
            const PipelineHandle adjacencyPipeline = driver->createPipeline(geometryDesc);
            CHECK(pointsPipeline.valid());
            CHECK(pointsDepthPipeline.valid());
            CHECK(adjacencyPipeline.valid());

            messages = 0;
            geometryDesc.geometryShader = ShaderHandle();
            CHECK(!driver->createPipeline(geometryDesc).valid());
            CHECK(messages == 1);
            geometryDesc.topology = Topology::Points;
            geometryDesc.geometryShader = flatFragment;
            CHECK(!driver->createPipeline(geometryDesc).valid());
            CHECK(messages == 2);

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamQuad * stride, sizeof(Params));
            driver->bindPipeline(pointsPipeline);
            driver->bindVertexBuffer(0, pointBuffer, 0);
            driver->draw(1, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 0, 255));
            CHECK(pixelIs(210, 120, 255, 0, 255));
            CHECK(pixelIs(270, 120, 0, 0, 0));
            CHECK(pixelIs(20, 20, 0, 0, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamQuad * stride, sizeof(Params));
            driver->bindPipeline(pointsDepthPipeline);
            driver->bindVertexBuffer(0, pointBuffer, 0);
            driver->draw(1, 0);
            driver->bindUniformBuffer(2, params, kParamGreenMid * stride, sizeof(Params));
            driver->bindPipeline(flatDepth);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 0, 255));
            CHECK(pixelIs(270, 120, 0, 255, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindPipeline(adjacencyPipeline);
            driver->bindVertexBuffer(0, adjacentBuffer, 0);
            driver->draw(6, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 0, 255, 0));
            CHECK(pixelIs(20, 20, 0, 255, 0));
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(adjacencyPipeline);
            driver->destroy(pointsDepthPipeline);
            driver->destroy(pointsPipeline);
            driver->destroy(adjacentBuffer);
            driver->destroy(pointBuffer);
            driver->destroy(adjacencyGeometry);
            driver->destroy(pointsGeometry);
        }

        {
            CHECK(driver->caps().tessellation);
            CHECK(driver->caps().maxPatchControlPoints >= 3);
            const ShaderHandle notchControl = makeShader(driver, notch_tesc);
            const ShaderHandle notchEvaluation = makeShader(driver, notch_tese);
            CHECK(notchControl.valid());
            CHECK(notchEvaluation.valid());

            const float kTriangle[6] = { -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f };
            BufferDesc triangleDesc;
            triangleDesc.size = sizeof(kTriangle);
            triangleDesc.data = kTriangle;
            const BufferHandle triangleBuffer = driver->createBuffer(triangleDesc);
            CHECK(triangleBuffer.valid());

            PipelineDesc patchDesc = flatPipelineDesc;
            patchDesc.targets = TargetFormats();
            patchDesc.blend = false;
            patchDesc.depthTest = false;
            patchDesc.topology = Topology::Patches;
            patchDesc.patchControlPoints = 3;
            patchDesc.tessControlShader = notchControl;
            patchDesc.tessEvalShader = notchEvaluation;
            const PipelineHandle patchPipeline = driver->createPipeline(patchDesc);
            patchDesc.depthTest = true;
            const PipelineHandle patchDepthPipeline = driver->createPipeline(patchDesc);
            CHECK(patchPipeline.valid());
            CHECK(patchDepthPipeline.valid());

            messages = 0;
            PipelineDesc badPatch = patchDesc;
            badPatch.patchControlPoints = 0;
            CHECK(!driver->createPipeline(badPatch).valid());
            CHECK(messages == 1);
            badPatch = patchDesc;
            badPatch.topology = Topology::Triangles;
            CHECK(!driver->createPipeline(badPatch).valid());
            CHECK(messages == 2);
            badPatch = patchDesc;
            badPatch.tessEvalShader = ShaderHandle();
            CHECK(!driver->createPipeline(badPatch).valid());
            CHECK(messages == 3);
            badPatch = patchDesc;
            badPatch.tessControlShader = notchEvaluation;
            CHECK(!driver->createPipeline(badPatch).valid());
            CHECK(messages == 4);
            badPatch = patchDesc;
            badPatch.patchControlPoints = driver->caps().maxPatchControlPoints + 1;
            CHECK(!driver->createPipeline(badPatch).valid());
            CHECK(messages == 5);

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamLevelOne * stride, sizeof(Params));
            driver->bindPipeline(patchPipeline);
            driver->bindVertexBuffer(0, triangleBuffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 255, 0));
            CHECK(pixelIs(160, 20, 255, 255, 0));
            CHECK(pixelIs(300, 200, 0, 0, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamLevelTwo * stride, sizeof(Params));
            driver->bindPipeline(patchPipeline);
            driver->bindVertexBuffer(0, triangleBuffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 255, 0));
            CHECK(pixelIs(160, 90, 255, 255, 0));
            CHECK(pixelIs(160, 20, 0, 0, 0));

            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamLevelOne * stride, sizeof(Params));
            driver->bindPipeline(patchDepthPipeline);
            driver->bindVertexBuffer(0, triangleBuffer, 0);
            driver->draw(3, 0);
            driver->bindUniformBuffer(2, params, kParamGreenMid * stride, sizeof(Params));
            driver->bindPipeline(flatDepth);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(pixelIs(160, 120, 255, 255, 0));
            CHECK(pixelIs(300, 200, 0, 255, 0));
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            driver->destroy(patchDepthPipeline);
            driver->destroy(patchPipeline);
            driver->destroy(triangleBuffer);
            driver->destroy(notchEvaluation);
            driver->destroy(notchControl);
        }

        {
            struct FloatCase
            {
                TextureFormat format;
                const void* data;
                int red;
                int green;
                int blue;
            };
            static const std::uint16_t kHalfRed[1] = { 0x3800 };
            static const std::uint16_t kHalfRedGreen[2] = { 0x3400, 0x3A00 };
            static const float kSingleRed[1] = { 0.5f };
            static const float kSingleRedGreen[2] = { 0.25f, 0.75f };
            static const float kSingleAll[4] = { 2.0f, 0.5f, 0.25f, 1.0f };
            const FloatCase cases[5] = {
                { TextureFormat::R16F, kHalfRed, 128, 0, 0 },
                { TextureFormat::RG16F, kHalfRedGreen, 64, 191, 0 },
                { TextureFormat::R32F, kSingleRed, 128, 0, 0 },
                { TextureFormat::RG32F, kSingleRedGreen, 64, 191, 0 },
                { TextureFormat::RGBA32F, kSingleAll, 255, 128, 64 },
            };

            messages = 0;
            pump(window);
            driver->beginFrame();
            for (int i = 0; i < 5; ++i)
            {
                TextureDesc floatDesc;
                floatDesc.format = cases[i].format;
                floatDesc.width = 1;
                floatDesc.height = 1;
                floatDesc.data = cases[i].data;
                const TextureHandle floatTexture = driver->createTexture(floatDesc);
                CHECK(floatTexture.valid());
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, floatTexture, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, cases[i].red, cases[i].green, cases[i].blue, 2));
                driver->destroy(floatTexture);
            }

            const TextureFormat targetFormats[5] = { TextureFormat::R16F, TextureFormat::RG16F,
                TextureFormat::R32F, TextureFormat::RG32F, TextureFormat::RGBA32F };
            const int expectedGreen[5] = { 0, 128, 0, 128, 128 };
            const int expectedBlue[5] = { 0, 0, 0, 0, 128 };
            for (int i = 0; i < 5; ++i)
            {
                TextureDesc targetDesc;
                targetDesc.format = targetFormats[i];
                targetDesc.width = 4;
                targetDesc.height = 4;
                targetDesc.usage = kTextureSampled | kTextureRenderTarget;
                messages = 0;
                const TextureHandle floatTarget = driver->createTexture(targetDesc);
                if (!driver->caps().floatColorTargets)
                {
                    CHECK(!floatTarget.valid());
                    CHECK(messages == 1);
                    continue;
                }
                CHECK(floatTarget.valid());
                PipelineDesc floatPipelineDesc = flatPipelineDesc;
                floatPipelineDesc.blend = false;
                floatPipelineDesc.depthTest = false;
                floatPipelineDesc.targets = TargetFormats();
                floatPipelineDesc.targets.window = false;
                floatPipelineDesc.targets.colorCount = 1;
                floatPipelineDesc.targets.colors[0] = targetFormats[i];
                const PipelineHandle floatPipeline = driver->createPipeline(floatPipelineDesc);
                CHECK(floatPipeline.valid());
                RenderPassDesc floatPass;
                floatPass.colors[0].texture = floatTarget;
                floatPass.colorCount = 1;
                driver->beginRenderPass(floatPass);
                driver->bindUniformBuffer(2, params, kParamHalf * stride, sizeof(Params));
                driver->bindPipeline(floatPipeline);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->draw(3, 0);
                driver->endRenderPass();
                driver->beginRenderPass(black);
                driver->bindPipeline(textured);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->bindTexture(3, floatTarget, nearest);
                driver->draw(3, 0);
                driver->endRenderPass();
                CHECK(pixelIs(160, 120, 128, expectedGreen[i], expectedBlue[i], 2));
                driver->destroy(floatPipeline);
                driver->destroy(floatTarget);
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            TextureDesc storageDesc;
            storageDesc.format = TextureFormat::R32UInt;
            storageDesc.width = 4;
            storageDesc.height = 4;
            storageDesc.usage = kTextureSampled | kTextureStorage;
            const TextureHandle unsignedStorage = driver->createTexture(storageDesc);
            CHECK(unsignedStorage.valid() == driver->caps().compute);
            driver->destroy(unsignedStorage);
            printf("float formats: colour targets %d, linear 32-bit filtering %d\n",
                    driver->caps().floatColorTargets, driver->caps().floatLinearFiltering);
        }

        {
            TextureDesc coverageMsaaDesc;
            coverageMsaaDesc.width = 16;
            coverageMsaaDesc.height = 16;
            coverageMsaaDesc.samples = 4;
            coverageMsaaDesc.usage = kTextureRenderTarget;
            const TextureHandle coverageMsaa = driver->createTexture(coverageMsaaDesc);
            TextureDesc coverageResolvedDesc;
            coverageResolvedDesc.width = 16;
            coverageResolvedDesc.height = 16;
            coverageResolvedDesc.usage = kTextureSampled | kTextureRenderTarget;
            const TextureHandle coverageResolved = driver->createTexture(coverageResolvedDesc);
            CHECK(coverageMsaa.valid());
            CHECK(coverageResolved.valid());

            PipelineDesc coverageDesc = flatPipelineDesc;
            coverageDesc.blend = false;
            coverageDesc.depthTest = false;
            coverageDesc.targets = TargetFormats();
            coverageDesc.targets.window = false;
            coverageDesc.targets.colorCount = 1;
            coverageDesc.targets.colors[0] = TextureFormat::RGBA8;
            coverageDesc.targets.samples = 4;
            const PipelineHandle plainCoverage = driver->createPipeline(coverageDesc);
            coverageDesc.alphaToCoverage = true;
            const PipelineHandle alphaCoverage = driver->createPipeline(coverageDesc);
            CHECK(plainCoverage.valid());
            CHECK(alphaCoverage.valid());

            RenderPassDesc coveragePass;
            coveragePass.colors[0].texture = coverageMsaa;
            coveragePass.colorCount = 1;
            coveragePass.resolves[0].texture = coverageResolved;
            coveragePass.colorStore = StoreOp::Discard;
            RenderTarget coverageTarget;
            coverageTarget.texture = coverageResolved;
            Rect coveragePixel;
            coveragePixel.x = 8;
            coveragePixel.y = 8;
            coveragePixel.width = 1;
            coveragePixel.height = 1;

            messages = 0;
            pump(window);
            driver->beginFrame();
            unsigned char plainResult[4] = { 0, 0, 0, 0 };
            unsigned char alphaResult[4] = { 0, 0, 0, 0 };
            driver->beginRenderPass(coveragePass);
            driver->bindUniformBuffer(2, params, kParamCoverage * stride, sizeof(Params));
            driver->bindPipeline(plainCoverage);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(driver->readPixels(coverageTarget, coveragePixel, plainResult));
            driver->beginRenderPass(coveragePass);
            driver->bindUniformBuffer(2, params, kParamCoverage * stride, sizeof(Params));
            driver->bindPipeline(alphaCoverage);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            CHECK(driver->readPixels(coverageTarget, coveragePixel, alphaResult));
            driver->endFrame();
            driver->present();
            CHECK(plainResult[0] == 255);
            CHECK(alphaResult[0] > 60 && alphaResult[0] < 200);
            CHECK(alphaResult[1] == 0 && alphaResult[2] == 0);
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            printf("alpha to coverage: plain %d, enabled %d\n", plainResult[0], alphaResult[0]);

            driver->destroy(alphaCoverage);
            driver->destroy(plainCoverage);
            driver->destroy(coverageResolved);
            driver->destroy(coverageMsaa);
        }

        {
            CHECK(driver->caps().independentBlend);
            TextureDesc blendTargetDesc;
            blendTargetDesc.width = 4;
            blendTargetDesc.height = 4;
            blendTargetDesc.usage = kTextureSampled | kTextureRenderTarget;
            const TextureHandle blendFirst = driver->createTexture(blendTargetDesc);
            const TextureHandle blendSecond = driver->createTexture(blendTargetDesc);
            CHECK(blendFirst.valid());
            CHECK(blendSecond.valid());

            PipelineDesc sharedDesc = twoTargetsPipelineDesc;
            sharedDesc.blend = true;
            sharedDesc.srcColor = BlendFactor::One;
            sharedDesc.dstColor = BlendFactor::One;
            sharedDesc.srcAlpha = BlendFactor::One;
            sharedDesc.dstAlpha = BlendFactor::Zero;
            const PipelineHandle sharedBlend = driver->createPipeline(sharedDesc);
            PipelineDesc separateDesc = sharedDesc;
            separateDesc.independentBlend = true;
            separateDesc.targetBlend[0].blend = true;
            separateDesc.targetBlend[0].srcColor = BlendFactor::One;
            separateDesc.targetBlend[0].dstColor = BlendFactor::One;
            separateDesc.targetBlend[0].srcAlpha = BlendFactor::One;
            separateDesc.targetBlend[1].blend = true;
            separateDesc.targetBlend[1].srcColor = BlendFactor::Zero;
            separateDesc.targetBlend[1].dstColor = BlendFactor::One;
            separateDesc.targetBlend[1].srcAlpha = BlendFactor::Zero;
            separateDesc.targetBlend[1].dstAlpha = BlendFactor::One;
            const PipelineHandle separateBlend = driver->createPipeline(separateDesc);
            CHECK(sharedBlend.valid());
            CHECK(separateBlend.valid());

            RenderPassDesc blendPass;
            blendPass.colors[0].texture = blendFirst;
            blendPass.colors[1].texture = blendSecond;
            blendPass.colorCount = 2;
            blendPass.clearColor[0] = 0.25f;
            blendPass.clearColor[1] = 0.25f;
            blendPass.clearColor[2] = 0.25f;
            RenderTarget firstTarget;
            firstTarget.texture = blendFirst;
            RenderTarget secondTarget;
            secondTarget.texture = blendSecond;
            Rect blendPixel;
            blendPixel.x = 2;
            blendPixel.y = 2;
            blendPixel.width = 1;
            blendPixel.height = 1;

            const PipelineHandle order[3] = { sharedBlend, separateBlend, sharedBlend };
            const int expectedSecond[3][3] = { { 64, 255, 64 }, { 64, 64, 64 }, { 64, 255, 64 } };
            messages = 0;
            pump(window);
            driver->beginFrame();
            for (int i = 0; i < 3; ++i)
            {
                driver->beginRenderPass(blendPass);
                driver->bindPipeline(order[i]);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->draw(3, 0);
                driver->endRenderPass();
                unsigned char one[4] = { 0, 0, 0, 0 };
                unsigned char two[4] = { 0, 0, 0, 0 };
                CHECK(driver->readPixels(firstTarget, blendPixel, one));
                CHECK(driver->readPixels(secondTarget, blendPixel, two));
                CHECK(one[0] == 255 && one[1] == 64 && one[2] == 64);
                CHECK(two[0] == expectedSecond[i][0] && two[1] == expectedSecond[i][1] &&
                        two[2] == expectedSecond[i][2]);
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);

            PipelineDesc maskedDesc = twoTargetsPipelineDesc;
            maskedDesc.independentBlend = true;
            maskedDesc.targetBlend[1].colorMask = kColorBlue;
            const PipelineHandle maskedTarget = driver->createPipeline(maskedDesc);
            CHECK(maskedTarget.valid());
            messages = 0;
            pump(window);
            driver->beginFrame();
            for (int i = 0; i < 3; ++i)
            {
                driver->beginRenderPass(blendPass);
                driver->bindPipeline(maskedTarget);
                driver->bindVertexBuffer(0, buffer, 0);
                driver->draw(3, 0);
                driver->endRenderPass();
                unsigned char one[4] = { 0, 0, 0, 0 };
                unsigned char two[4] = { 0, 0, 0, 0 };
                CHECK(driver->readPixels(firstTarget, blendPixel, one));
                CHECK(driver->readPixels(secondTarget, blendPixel, two));
                CHECK(one[0] == 255 && one[1] == 0 && one[2] == 0);
                CHECK(two[0] == 64 && two[1] == 64 && two[2] == 0);
            }
            driver->endFrame();
            driver->present();
            CHECK(messages == 0);
            if (messages) printf("unexpected: %s\n", lastMessage);
            driver->destroy(maskedTarget);

            driver->destroy(separateBlend);
            driver->destroy(sharedBlend);
            driver->destroy(blendSecond);
            driver->destroy(blendFirst);
        }

        if (driver->caps().storageWritesInGraphics)
        {
            const ShaderHandle countFragment = makeShader(driver, count_frag);
            CHECK(countFragment.valid());
            const std::uint32_t zero = 0;
            BufferDesc counterDesc;
            counterDesc.usage = BufferUsage::Storage;
            counterDesc.size = sizeof(std::uint32_t);
            counterDesc.data = &zero;
            const BufferHandle counter = driver->createBuffer(counterDesc);
            CHECK(counter.valid());

            PipelineDesc countDesc = flatPipelineDesc;
            countDesc.targets = TargetFormats();
            countDesc.blend = false;
            countDesc.depthTest = false;
            countDesc.fragmentShader = countFragment;
            countDesc.storageBufferCount = 1;
            countDesc.storageBuffers[0].name = "Counter";
            countDesc.storageBuffers[0].slot = 0;
            const PipelineHandle countPipeline = driver->createPipeline(countDesc);
            CHECK(countPipeline.valid());

            messages = 0;
            pump(window);
            driver->beginFrame();
            driver->beginRenderPass(black);
            driver->bindUniformBuffer(2, params, kParamGreen * stride, sizeof(Params));
            driver->bindStorageBuffer(0, counter, 0, sizeof(std::uint32_t));
            driver->bindPipeline(countPipeline);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->draw(3, 0);
            driver->endRenderPass();
            std::uint32_t covered = 0;
            CHECK(driver->readBuffer(counter, 0, sizeof(covered), &covered));
            driver->endFrame();
            driver->present();
            CHECK(covered == 320u * 240u);
            CHECK(messages == 0);
            printf("fragment shader counted %u pixels\n", covered);

            driver->destroy(countPipeline);
            driver->destroy(counter);
            driver->destroy(countFragment);
        }

        CHECK(driver->caps().occlusionQueries);
        const QueryHandle visibleQuery = driver->createQuery(QueryType::Occlusion);
        const QueryHandle hiddenQuery = driver->createQuery(QueryType::Occlusion);
        const QueryHandle timeQuery = driver->createQuery(QueryType::Time);
        CHECK(visibleQuery.valid());
        CHECK(hiddenQuery.valid());
        CHECK(timeQuery.valid() == driver->caps().timerQueries);

        std::uint64_t visibleResult = 7;
        std::uint64_t hiddenResult = 7;
        std::uint64_t timeResult = 0;
        CHECK(!driver->queryResult(visibleQuery, &visibleResult));
        CHECK(!driver->queryResult(QueryHandle(), &visibleResult));

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginQuery(visibleQuery);
        CHECK(messages == 1);
        driver->endQuery(visibleQuery);
        CHECK(messages == 2);
        driver->beginRenderPass(black);
        driver->beginQuery(visibleQuery);
        driver->beginQuery(hiddenQuery);
        CHECK(messages == 3);
        driver->beginQuery(visibleQuery);
        CHECK(messages == 4);
        driver->endQuery(visibleQuery);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 4);

        messages = 0;
        bool visibleReady = false;
        bool hiddenReady = false;
        bool timeReady = !timeQuery.valid();
        int queryFrames = 0;
        const double queryStart = nowSeconds();
        for (; nowSeconds() - queryStart < 3.0 &&
                !(visibleReady && hiddenReady && timeReady && queryFrames >= 6);
                ++queryFrames)
        {
            breathe();
            pump(window);
            driver->beginFrame();
            if (timeQuery.valid()) driver->beginQuery(timeQuery);
            driver->beginRenderPass(black);
            driver->bindPipeline(flatDepth);
            driver->bindVertexBuffer(0, buffer, 0);
            driver->bindUniformBuffer(2, params, kParamNearBlue * stride, sizeof(Params));
            driver->beginQuery(visibleQuery);
            driver->draw(3, 0);
            driver->endQuery(visibleQuery);
            driver->bindUniformBuffer(2, params, kParamFarRed * stride, sizeof(Params));
            driver->beginQuery(hiddenQuery);
            driver->draw(3, 0);
            driver->endQuery(hiddenQuery);
            driver->endRenderPass();
            if (timeQuery.valid()) driver->endQuery(timeQuery);
            driver->endFrame();
            driver->present();

            visibleReady = driver->queryResult(visibleQuery, &visibleResult);
            hiddenReady = driver->queryResult(hiddenQuery, &hiddenResult);
            if (timeQuery.valid()) timeReady = driver->queryResult(timeQuery, &timeResult);
        }
        CHECK(visibleReady);
        CHECK(hiddenReady);
        CHECK(timeReady);
        CHECK(visibleResult == 1);
        CHECK(hiddenResult == 0);
        if (timeQuery.valid())
        {
            CHECK(timeResult > 0);
            CHECK(timeResult < 1000000000ull);
        }
        CHECK(messages == 0);
        if (messages) printf("unexpected: %s\n", lastMessage);
        printf("queries: ready after %d frames, gpu time %llu ns\n", queryFrames,
                static_cast<unsigned long long>(timeResult));

        messages = 0;
        pump(window);
        driver->beginFrame();
        driver->beginRenderPass(black);
        driver->beginQuery(visibleQuery);
        driver->endRenderPass();
        CHECK(messages == 1);
        driver->beginRenderPass(keep(black));
        driver->beginQuery(hiddenQuery);
        driver->destroy(hiddenQuery);
        driver->beginQuery(visibleQuery);
        driver->endQuery(visibleQuery);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();
        CHECK(messages == 1);
        if (messages != 1) printf("unexpected: %s\n", lastMessage);

        driver->destroy(timeQuery);
        driver->destroy(hiddenQuery);
        driver->destroy(visibleQuery);
        CHECK(!driver->queryResult(visibleQuery, &visibleResult));

        driver->destroy(stencilEqualOffscreen);
        driver->destroy(stencilWriteOffscreen);
        driver->destroy(stencilDepth);
        driver->destroy(stencilColor);
        driver->destroy(wire);
        driver->destroy(shadowPipeline);
        driver->destroy(shadowFragment);
        driver->destroy(comparison);
        driver->destroy(shadowMap);
        driver->destroy(biased);
        driver->destroy(unbiased);
        driver->destroy(blendReverse);
        driver->destroy(blendMax);
        driver->destroy(masked);
        driver->destroy(stencilNotEqual);
        driver->destroy(stencilEqual);
        driver->destroy(stencilWrite);

        driver->destroy(flatLayer);
        driver->destroy(mipTarget);
        driver->destroy(layers);
        driver->destroy(explicitMips);
        driver->destroy(volumeTexture);
        driver->destroy(cubeTexture);
        driver->destroy(arrayTexture);
        driver->destroy(lodSampler);
        driver->destroy(volumeSampler);
        driver->destroy(lodPipeline);
        driver->destroy(volumePipeline);
        driver->destroy(cubePipeline);
        driver->destroy(arrayPipeline);
        driver->destroy(lodFragment);
        driver->destroy(volumeFragment);
        driver->destroy(cubeFragment);
        driver->destroy(arrayFragment);
        driver->destroy(anisotropic);

        driver->destroy(twoTargets);
        driver->destroy(twoTargetsFragment);
        driver->destroy(second);
        driver->destroy(first);

        driver->destroy(textured);
        driver->destroy(texturedVertex);
        driver->destroy(texturedFragment);
        driver->destroy(nearest);
        driver->destroy(mipped);
        driver->destroy(texture);

        driver->destroy(flatBlend);
        driver->destroy(flatDepth);
        driver->destroy(flat);
        driver->destroy(flatVertex);
        driver->destroy(flatFragment);
        driver->destroy(params);
        driver->destroy(quadIndices);
        driver->destroy(quad);

        driver->destroy(pipeline);
        driver->destroy(vertexShader);
        driver->destroy(fragmentShader);
        driver->destroy(buffer);
        destroyDriver(driver);
    }

    if (headlessMode)
    {
        headless::closeSurface(&headlessMain);
        headless::closeContext(&headlessContext);
    }
    else
    {
        window_destroy(window);
        platform_shutdown();
    }

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
