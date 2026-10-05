#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <ct/vector.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oit_append.frag.h"
#include "oit_clear.comp.h"
#include "oit_floor.frag.h"
#include "oit_fullscreen.vert.h"
#include "oit_plain.frag.h"
#include "oit_resolve.frag.h"
#include "oit_scene.vert.h"

namespace
{

const std::uint32_t kLayersPerPixel = 4;
const std::uint32_t kNodeBytes = 16;
const std::uint32_t kClearGroup = 256;

struct Vertex
{
    float position[3];
    float normal[3];
};

struct FrameUniforms
{
    Math::Mat4 viewProjection;
    float screen[4];
    float light[4];
};

struct ObjectUniforms
{
    Math::Mat4 model;
    float color[4];
};

struct Item
{
    bool cube;
    float position[3];
    float width;
    float height;
    float tilt;
    float rate;
    float phase;
    float color[4];
};

const Item kItems[12] = {
    { false, { -1.7f, 0.1f, 0.9f }, 1.1f, 0.9f, 0.15f, 0.30f, 0.0f,
        { 0.95f, 0.25f, 0.20f, 0.55f } },
    { false, { -0.7f, 0.3f, 0.3f }, 1.0f, 1.0f, -0.20f, -0.25f, 1.0f,
        { 0.25f, 0.80f, 0.30f, 0.50f } },
    { false, { 0.4f, 0.0f, -0.4f }, 1.2f, 0.8f, 0.10f, 0.20f, 2.0f,
        { 0.20f, 0.40f, 0.95f, 0.55f } },
    { false, { 1.5f, 0.2f, 0.7f }, 0.9f, 1.1f, -0.30f, -0.35f, 3.0f,
        { 0.95f, 0.85f, 0.20f, 0.50f } },
    { false, { -1.0f, -0.4f, -1.2f }, 1.3f, 0.7f, 0.25f, 0.22f, 4.0f,
        { 0.85f, 0.30f, 0.85f, 0.50f } },
    { false, { 0.2f, 0.6f, 1.5f }, 0.8f, 0.8f, -0.10f, -0.28f, 5.0f,
        { 0.20f, 0.85f, 0.85f, 0.55f } },
    { false, { 1.9f, -0.3f, -1.0f }, 1.0f, 1.2f, 0.20f, 0.26f, 0.5f,
        { 1.00f, 0.55f, 0.15f, 0.50f } },
    { false, { -2.1f, 0.5f, -0.3f }, 0.9f, 0.9f, -0.25f, -0.18f, 1.5f,
        { 0.55f, 0.35f, 0.90f, 0.55f } },
    { false, { 0.8f, -0.5f, 0.4f }, 1.1f, 0.6f, 0.30f, 0.32f, 2.5f,
        { 0.15f, 0.65f, 0.55f, 0.50f } },
    { true, { -0.3f, -0.1f, 0.6f }, 0.55f, 0.55f, 0.30f, 0.40f, 0.3f,
        { 0.95f, 0.35f, 0.50f, 0.45f } },
    { true, { 1.1f, 0.1f, -0.2f }, 0.45f, 0.45f, -0.20f, -0.45f, 1.7f,
        { 0.30f, 0.55f, 1.00f, 0.45f } },
    { true, { -1.4f, 0.0f, -0.6f }, 0.5f, 0.5f, 0.50f, 0.35f, 2.9f,
        { 0.60f, 0.90f, 0.30f, 0.45f } },
};

const std::uint32_t kItemCount = sizeof(kItems) / sizeof(kItems[0]);

const float kFaces[6][3][3] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
};

const float kCornerSigns[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    return (value + alignment - 1) / alignment * alignment;
}

struct ListBuffers
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t capacity = 0;
    prisma::BufferHandle heads;
    prisma::BufferHandle nodes;

    bool valid() const { return heads.valid() && nodes.valid(); }
};

void createListBuffers(prisma::Driver* driver, std::uint32_t width, std::uint32_t height,
        ListBuffers* lists)
{
    const std::uint32_t pixels = width * height;
    lists->width = width;
    lists->height = height;
    lists->capacity = pixels * kLayersPerPixel;

    prisma::BufferDesc desc;
    desc.usage = prisma::BufferUsage::Storage;
    desc.size = pixels * sizeof(std::uint32_t);
    desc.debugName = "list heads";
    lists->heads = driver->createBuffer(desc);
    desc.size = lists->capacity * kNodeBytes;
    desc.debugName = "list nodes";
    lists->nodes = driver->createBuffer(desc);
}

void destroyListBuffers(prisma::Driver* driver, ListBuffers* lists)
{
    driver->destroy(lists->nodes);
    driver->destroy(lists->heads);
    *lists = ListBuffers();
}

const char* modeName(bool sorted)
{
    return sorted ? "per-pixel linked lists" : "plain alpha blend";
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

    PlatformWindow* window =
            zenapp::openWindow("prisma 25 order independent transparency", driverType);
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

    if (!driver->caps().compute || !driver->caps().storageWritesInGraphics)
    {
        log_error("oit: this GPU has no compute shaders or cannot write storage buffers from "
                  "fragment shaders");
        prisma::destroyDriver(driver);
        window_destroy(window);
        platform_shutdown();
        return 0;
    }

    Vertex vertices[24];
    std::uint16_t indices[36];
    for (int face = 0; face < 6; ++face)
    {
        for (int corner = 0; corner < 4; ++corner)
        {
            Vertex& vertex = vertices[face * 4 + corner];
            for (int c = 0; c < 3; ++c)
            {
                vertex.position[c] = kFaces[face][0][c] +
                                     kFaces[face][1][c] * kCornerSigns[corner][0] +
                                     kFaces[face][2][c] * kCornerSigns[corner][1];
                vertex.normal[c] = kFaces[face][0][c];
            }
        }
        const std::uint16_t first = static_cast<std::uint16_t>(face * 4);
        const std::uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) indices[face * 6 + i] = first + order[i];
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(vertices);
    bufferDesc.data = vertices;
    bufferDesc.debugName = "box vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = sizeof(indices);
    bufferDesc.data = indices;
    bufferDesc.debugName = "box indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Storage;
    bufferDesc.size = sizeof(std::uint32_t);
    bufferDesc.data = nullptr;
    bufferDesc.debugName = "list counter";
    const prisma::BufferHandle counterBuffer = driver->createBuffer(bufferDesc);

    const std::uint32_t alignment = driver->caps().uniformBufferOffsetAlignment;
    std::uint32_t stride = sizeof(FrameUniforms) > sizeof(ObjectUniforms) ? sizeof(FrameUniforms)
                                                                          : sizeof(ObjectUniforms);
    stride = alignUp(stride, alignment);
    const std::uint32_t rangeCount = kItemCount + 2;
    ct::Vector<unsigned char> uniforms;
    uniforms.resize(stride * rangeCount);
    memset(uniforms.data(), 0, uniforms.size());

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = stride * rangeCount;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "oit uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(float) * 4;
    bufferDesc.debugName = "clear params";
    const prisma::BufferHandle clearBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle sceneVertex = zenapp::createShader(driver, oit_scene_vert);
    const prisma::ShaderHandle floorFragment = zenapp::createShader(driver, oit_floor_frag);
    const prisma::ShaderHandle plainFragment = zenapp::createShader(driver, oit_plain_frag);
    const prisma::ShaderHandle appendFragment = zenapp::createShader(driver, oit_append_frag);
    const prisma::ShaderHandle fullscreenVertex = zenapp::createShader(driver, oit_fullscreen_vert);
    const prisma::ShaderHandle resolveFragment = zenapp::createShader(driver, oit_resolve_frag);
    const prisma::ShaderHandle clearShader = zenapp::createShader(driver, oit_clear_comp);

    prisma::PipelineDesc floorDesc;
    floorDesc.vertexShader = sceneVertex;
    floorDesc.fragmentShader = floorFragment;
    floorDesc.vertexBuffers[0].stride = sizeof(Vertex);
    floorDesc.vertexBufferCount = 1;
    floorDesc.attributeCount = 2;
    floorDesc.attributes[0].location = 0;
    floorDesc.attributes[0].format = prisma::VertexFormat::Float3;
    floorDesc.attributes[0].offset = 0;
    floorDesc.attributes[1].location = 1;
    floorDesc.attributes[1].format = prisma::VertexFormat::Float3;
    floorDesc.attributes[1].offset = sizeof(float) * 3;
    floorDesc.depthTest = true;
    floorDesc.depthWrite = true;
    floorDesc.debugName = "floor";
    const prisma::PipelineHandle floorPipeline = driver->createPipeline(floorDesc);

    prisma::PipelineDesc plainDesc = floorDesc;
    plainDesc.fragmentShader = plainFragment;
    plainDesc.depthWrite = false;
    plainDesc.blend = true;
    plainDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    plainDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    plainDesc.srcAlpha = prisma::BlendFactor::One;
    plainDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    plainDesc.debugName = "plain blend";
    const prisma::PipelineHandle plainPipeline = driver->createPipeline(plainDesc);

    prisma::PipelineDesc appendDesc = floorDesc;
    appendDesc.fragmentShader = appendFragment;
    appendDesc.depthWrite = false;
    appendDesc.colorMask = 0;
    appendDesc.storageBufferCount = 3;
    appendDesc.storageBuffers[0].name = "Heads";
    appendDesc.storageBuffers[0].slot = 0;
    appendDesc.storageBuffers[1].name = "Nodes";
    appendDesc.storageBuffers[1].slot = 1;
    appendDesc.storageBuffers[2].name = "Counter";
    appendDesc.storageBuffers[2].slot = 2;
    appendDesc.debugName = "append fragments";
    const prisma::PipelineHandle appendPipeline = driver->createPipeline(appendDesc);

    prisma::PipelineDesc resolveDesc;
    resolveDesc.vertexShader = fullscreenVertex;
    resolveDesc.fragmentShader = resolveFragment;
    resolveDesc.depthTest = false;
    resolveDesc.depthWrite = false;
    resolveDesc.blend = true;
    resolveDesc.srcColor = prisma::BlendFactor::One;
    resolveDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    resolveDesc.srcAlpha = prisma::BlendFactor::One;
    resolveDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    resolveDesc.storageBufferCount = 2;
    resolveDesc.storageBuffers[0].name = "Heads";
    resolveDesc.storageBuffers[0].slot = 0;
    resolveDesc.storageBuffers[1].name = "Nodes";
    resolveDesc.storageBuffers[1].slot = 1;
    resolveDesc.debugName = "resolve lists";
    const prisma::PipelineHandle resolvePipeline = driver->createPipeline(resolveDesc);

    prisma::ComputePipelineDesc clearDesc;
    clearDesc.shader = clearShader;
    clearDesc.uniformBlockCount = 1;
    clearDesc.uniformBlocks[0].name = "Params";
    clearDesc.uniformBlocks[0].slot = 0;
    clearDesc.storageBufferCount = 2;
    clearDesc.storageBuffers[0].name = "Heads";
    clearDesc.storageBuffers[0].slot = 0;
    clearDesc.storageBuffers[1].name = "Counter";
    clearDesc.storageBuffers[1].slot = 1;
    clearDesc.debugName = "clear lists";
    const prisma::PipelineHandle clearPipeline = driver->createComputePipeline(clearDesc);

    driver->destroy(sceneVertex);
    driver->destroy(floorFragment);
    driver->destroy(plainFragment);
    driver->destroy(appendFragment);
    driver->destroy(fullscreenVertex);
    driver->destroy(resolveFragment);
    driver->destroy(clearShader);

    bool ready = vertexBuffer.valid() && indexBuffer.valid() && counterBuffer.valid() &&
                 uniformBuffer.valid() && clearBuffer.valid() && floorPipeline.valid() &&
                 plainPipeline.valid() && appendPipeline.valid() && resolvePipeline.valid() &&
                 clearPipeline.valid();
    if (!ready) log_error("oit: resource creation failed");

    prisma::RenderPassDesc floorPass;
    floorPass.clearColor[0] = 0.82f;
    floorPass.clearColor[1] = 0.84f;
    floorPass.clearColor[2] = 0.86f;

    prisma::RenderPassDesc layerPass;
    layerPass.colorLoad = prisma::LoadOp::Load;
    layerPass.depthLoad = prisma::LoadOp::Load;
    layerPass.stencilLoad = prisma::LoadOp::Load;

    prisma::RenderPassDesc resolvePass;
    resolvePass.colorLoad = prisma::LoadOp::Load;
    resolvePass.depthLoad = prisma::LoadOp::DontCare;
    resolvePass.stencilLoad = prisma::LoadOp::DontCare;

    ListBuffers lists;
    bool sorted = !zenapp::hasArgument(argc, argv, "plain");
    bool titleDirty = true;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_O))
        {
            sorted = !sorted;
            titleDirty = true;
        }
        if (titleDirty)
        {
            char title[160];
            snprintf(title, sizeof(title), "prisma 25 order independent transparency | O: %s",
                    modeName(sorted));
            window_set_title(window, title);
            titleDirty = false;
        }

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        if (width < 2 || height < 2) continue;

        const std::uint32_t targetWidth = static_cast<std::uint32_t>(width);
        const std::uint32_t targetHeight = static_cast<std::uint32_t>(height);
        if (targetWidth != lists.width || targetHeight != lists.height || !lists.valid())
        {
            destroyListBuffers(driver, &lists);
            createListBuffers(driver, targetWidth, targetHeight, &lists);
            if (!lists.valid())
            {
                log_error("oit: cannot create the list buffers");
                destroyListBuffers(driver, &lists);
                ready = false;
                break;
            }
        }

        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(0.9f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::LookAt(Math::Vec3(0.0f, 2.0f, 5.2f),
                Math::Vec3(0.0f, -0.2f, 0.0f), Math::Vec3(0.0f, 1.0f, 0.0f));

        FrameUniforms frame;
        frame.viewProjection = projection * view;
        frame.screen[0] = static_cast<float>(width);
        frame.screen[1] = static_cast<float>(height);
        frame.screen[2] = static_cast<float>(lists.capacity);
        frame.screen[3] = 0.0f;
        frame.light[0] = 0.4f;
        frame.light[1] = 0.8f;
        frame.light[2] = 0.5f;
        frame.light[3] = 0.0f;
        memcpy(uniforms.data(), &frame, sizeof(frame));

        ObjectUniforms floorObject;
        floorObject.model = Math::Mat4::Translation(Math::Vec3(0.0f, -1.4f, 0.0f)) *
                            Math::Mat4::RotationX(-1.5707963f) *
                            Math::Mat4::Scale(Math::Vec3(7.0f, 7.0f, 1.0f)) *
                            Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -1.0f));
        floorObject.color[0] = floorObject.color[1] = floorObject.color[2] = 0.6f;
        floorObject.color[3] = 1.0f;
        memcpy(uniforms.data() + stride, &floorObject, sizeof(floorObject));

        for (std::uint32_t i = 0; i < kItemCount; ++i)
        {
            const Item& item = kItems[i];
            const Math::Vec3 position(item.position[0], item.position[1], item.position[2]);
            const Math::Mat4 rotation = Math::Mat4::Translation(position) *
                                        Math::Mat4::RotationY(time * item.rate + item.phase) *
                                        Math::Mat4::RotationX(item.tilt);
            ObjectUniforms object;
            if (item.cube)
                object.model = rotation *
                               Math::Mat4::Scale(Math::Vec3(item.width, item.width, item.width));
            else
                object.model = rotation *
                               Math::Mat4::Scale(Math::Vec3(item.width, item.height, 1.0f)) *
                               Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -1.0f));
            memcpy(object.color, item.color, sizeof(object.color));
            memcpy(uniforms.data() + (i + 2) * stride, &object, sizeof(object));
        }

        const float clearParams[4] = { static_cast<float>(width * height), 0.0f, 0.0f, 0.0f };
        const std::uint32_t headBytes = targetWidth * targetHeight * sizeof(std::uint32_t);
        const std::uint32_t nodeBytes = lists.capacity * kNodeBytes;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, uniforms.data(), stride * rangeCount);
        driver->updateBuffer(clearBuffer, 0, clearParams, sizeof(clearParams));

        if (sorted)
        {
            driver->beginComputePass();
            driver->bindPipeline(clearPipeline);
            driver->bindUniformBuffer(0, clearBuffer, 0, sizeof(clearParams));
            driver->bindStorageBuffer(0, lists.heads, 0, headBytes);
            driver->bindStorageBuffer(1, counterBuffer, 0, sizeof(std::uint32_t));
            driver->dispatch((targetWidth * targetHeight + kClearGroup - 1) / kClearGroup, 1, 1);
            driver->endComputePass();
        }

        driver->beginRenderPass(floorPass);
        driver->bindPipeline(floorPipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        driver->bindUniformBuffer(1, uniformBuffer, stride, sizeof(ObjectUniforms));
        driver->drawIndexed(6, 0);
        driver->endRenderPass();

        driver->beginRenderPass(layerPass);
        driver->bindPipeline(sorted ? appendPipeline : plainPipeline);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        if (sorted)
        {
            driver->bindStorageBuffer(0, lists.heads, 0, headBytes);
            driver->bindStorageBuffer(1, lists.nodes, 0, nodeBytes);
            driver->bindStorageBuffer(2, counterBuffer, 0, sizeof(std::uint32_t));
        }
        for (std::uint32_t i = 0; i < kItemCount; ++i)
        {
            driver->bindUniformBuffer(1, uniformBuffer, (i + 2) * stride, sizeof(ObjectUniforms));
            driver->drawIndexed(kItems[i].cube ? 36 : 6, 0);
        }
        driver->endRenderPass();

        if (sorted)
        {
            driver->beginRenderPass(resolvePass);
            driver->bindPipeline(resolvePipeline);
            driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
            driver->bindStorageBuffer(0, lists.heads, 0, headBytes);
            driver->bindStorageBuffer(1, lists.nodes, 0, nodeBytes);
            driver->draw(3, 0);
            driver->endRenderPass();
        }

        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    destroyListBuffers(driver, &lists);
    driver->destroy(clearPipeline);
    driver->destroy(resolvePipeline);
    driver->destroy(appendPipeline);
    driver->destroy(plainPipeline);
    driver->destroy(floorPipeline);
    driver->destroy(clearBuffer);
    driver->destroy(uniformBuffer);
    driver->destroy(counterBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
