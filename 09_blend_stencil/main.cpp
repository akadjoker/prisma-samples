#include "common/Projection.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blend_cube.frag.h"
#include "blend_cube.vert.h"
#include "blend_quad.frag.h"
#include "blend_quad.vert.h"

namespace
{

struct Vertex
{
    float position[3];
    float color[3];
};

struct QuadVertex
{
    float position[2];
    float color[4];
};

const float kFaces[6][3][3] = {
    { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
    { { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
    { { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
    { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
    { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
};

const float kFaceColors[6][3] = {
    { 0.9f, 0.2f, 0.2f },
    { 0.2f, 0.8f, 0.3f },
    { 0.2f, 0.4f, 0.9f },
    { 0.9f, 0.8f, 0.2f },
    { 0.8f, 0.3f, 0.8f },
    { 0.2f, 0.8f, 0.8f },
};

const float kCornerSigns[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

const QuadVertex kQuad[4] = {
    { { -0.75f, -0.6f }, { 1.0f, 0.4f, 0.2f, 0.6f } },
    { { 0.75f, -0.6f }, { 0.3f, 0.9f, 0.4f, 0.6f } },
    { { -0.75f, 0.6f }, { 0.9f, 0.9f, 0.3f, 0.6f } },
    { { 0.75f, 0.6f }, { 0.3f, 0.5f, 1.0f, 0.6f } },
};

struct BlendMode
{
    const char* name;
    prisma::BlendFactor source;
    prisma::BlendOp op;
};

const BlendMode kBlendModes[4] = {
    { "source alpha + one", prisma::BlendFactor::SrcAlpha, prisma::BlendOp::Add },
    { "source alpha, reverse subtract", prisma::BlendFactor::SrcAlpha,
        prisma::BlendOp::ReverseSubtract },
    { "source colour + one", prisma::BlendFactor::SrcColor, prisma::BlendOp::Add },
    { "source colour, reverse subtract", prisma::BlendFactor::SrcColor,
        prisma::BlendOp::ReverseSubtract },
};

const char* const kStencilNames[3] = { "stencil off", "cube inside the quad",
    "cube outside the quad" };

void setCubeStencil(prisma::PipelineDesc* desc, prisma::CompareOp compare)
{
    desc->stencilTest = true;
    desc->stencilFront.compare = compare;
    desc->stencilBack.compare = compare;
    desc->stencilWriteMask = 0;
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

    PlatformWindow* window = zenapp::openWindow("prisma 09 blend and stencil", driverType);
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
                vertex.color[c] = kFaceColors[face][c];
            }
        }
        const std::uint16_t first = static_cast<std::uint16_t>(face * 4);
        const std::uint16_t order[6] = { 0, 1, 2, 0, 2, 3 };
        for (int i = 0; i < 6; ++i) indices[face * 6 + i] = first + order[i];
    }

    prisma::BufferDesc bufferDesc;
    bufferDesc.size = sizeof(vertices);
    bufferDesc.data = vertices;
    bufferDesc.debugName = "cube vertices";
    const prisma::BufferHandle vertexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = sizeof(indices);
    bufferDesc.data = indices;
    bufferDesc.debugName = "cube indices";
    const prisma::BufferHandle indexBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Vertex;
    bufferDesc.size = sizeof(kQuad);
    bufferDesc.data = kQuad;
    bufferDesc.debugName = "quad vertices";
    const prisma::BufferHandle quadBuffer = driver->createBuffer(bufferDesc);

    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(Math::Mat4);
    bufferDesc.data = nullptr;
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "cube uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle cubeVertex = zenapp::createShader(driver, blend_cube_vert);
    const prisma::ShaderHandle cubeFragment = zenapp::createShader(driver, blend_cube_frag);
    const prisma::ShaderHandle quadVertex = zenapp::createShader(driver, blend_quad_vert);
    const prisma::ShaderHandle quadFragment = zenapp::createShader(driver, blend_quad_frag);

    prisma::PipelineDesc cubeDesc;
    cubeDesc.vertexShader = cubeVertex;
    cubeDesc.fragmentShader = cubeFragment;
    cubeDesc.vertexBuffers[0].stride = sizeof(Vertex);
    cubeDesc.vertexBufferCount = 1;
    cubeDesc.attributeCount = 2;
    cubeDesc.attributes[0].location = 0;
    cubeDesc.attributes[0].format = prisma::VertexFormat::Float3;
    cubeDesc.attributes[0].offset = 0;
    cubeDesc.attributes[1].location = 1;
    cubeDesc.attributes[1].format = prisma::VertexFormat::Float3;
    cubeDesc.attributes[1].offset = sizeof(float) * 3;
    cubeDesc.depthTest = true;
    cubeDesc.cullMode = prisma::CullMode::Back;

    prisma::PipelineHandle cubePipelines[3];
    cubeDesc.debugName = "cube without stencil";
    cubePipelines[0] = driver->createPipeline(cubeDesc);
    setCubeStencil(&cubeDesc, prisma::CompareOp::Equal);
    cubeDesc.debugName = "cube where stencil equals";
    cubePipelines[1] = driver->createPipeline(cubeDesc);
    setCubeStencil(&cubeDesc, prisma::CompareOp::NotEqual);
    cubeDesc.debugName = "cube where stencil differs";
    cubePipelines[2] = driver->createPipeline(cubeDesc);

    prisma::PipelineDesc quadDesc;
    quadDesc.vertexShader = quadVertex;
    quadDesc.fragmentShader = quadFragment;
    quadDesc.vertexBuffers[0].stride = sizeof(QuadVertex);
    quadDesc.vertexBufferCount = 1;
    quadDesc.attributeCount = 2;
    quadDesc.attributes[0].location = 0;
    quadDesc.attributes[0].format = prisma::VertexFormat::Float2;
    quadDesc.attributes[0].offset = 0;
    quadDesc.attributes[1].location = 1;
    quadDesc.attributes[1].format = prisma::VertexFormat::Float4;
    quadDesc.attributes[1].offset = sizeof(float) * 2;
    quadDesc.topology = prisma::Topology::TriangleStrip;
    quadDesc.depthTest = false;
    quadDesc.depthWrite = false;

    prisma::PipelineHandle quadPipelines[4];
    quadDesc.blend = true;
    quadDesc.dstColor = prisma::BlendFactor::One;
    for (int i = 0; i < 4; ++i)
    {
        quadDesc.srcColor = kBlendModes[i].source;
        quadDesc.colorBlendOp = kBlendModes[i].op;
        quadDesc.debugName = kBlendModes[i].name;
        quadPipelines[i] = driver->createPipeline(quadDesc);
    }

    quadDesc.blend = false;
    quadDesc.colorMask = 0;
    quadDesc.stencilTest = true;
    quadDesc.stencilFront.compare = prisma::CompareOp::Always;
    quadDesc.stencilFront.passOp = prisma::StencilOp::Replace;
    quadDesc.stencilBack = quadDesc.stencilFront;
    quadDesc.debugName = "quad stencil mask";
    const prisma::PipelineHandle maskPipeline = driver->createPipeline(quadDesc);

    driver->destroy(cubeVertex);
    driver->destroy(cubeFragment);
    driver->destroy(quadVertex);
    driver->destroy(quadFragment);

    bool ready = vertexBuffer.valid() && indexBuffer.valid() && quadBuffer.valid() &&
                 uniformBuffer.valid() && maskPipeline.valid();
    for (int i = 0; i < 3; ++i) ready = ready && cubePipelines[i].valid();
    for (int i = 0; i < 4; ++i) ready = ready && quadPipelines[i].valid();
    if (!ready) log_error("blend and stencil: resource creation failed");

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int blendMode = 0;
    int stencilMode = 0;
    bool titleDirty = true;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);
        if (key_pressed(window, KEY_ONE))
        {
            blendMode = 0;
            titleDirty = true;
        }
        if (key_pressed(window, KEY_TWO))
        {
            blendMode = 1;
            titleDirty = true;
        }
        if (key_pressed(window, KEY_THREE))
        {
            blendMode = 2;
            titleDirty = true;
        }
        if (key_pressed(window, KEY_FOUR))
        {
            blendMode = 3;
            titleDirty = true;
        }
        if (key_pressed(window, KEY_S))
        {
            stencilMode = (stencilMode + 1) % 3;
            titleDirty = true;
        }
        if (titleDirty)
        {
            char title[160];
            snprintf(title, sizeof(title), "prisma 09 blend and stencil | 1-4 blend: %s | S: %s",
                    kBlendModes[blendMode].name, kStencilNames[stencilMode]);
            window_set_title(window, title);
            titleDirty = false;
        }

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float angle = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -5.0f));
        const Math::Mat4 model = Math::Mat4::RotationY(angle) * Math::Mat4::RotationX(angle * 0.7f);
        const Math::Mat4 modelViewProjection = projection * view * model;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &modelViewProjection, sizeof(Math::Mat4));
        driver->beginRenderPass(pass);
        driver->setStencilReference(1);

        if (stencilMode != 0)
        {
            driver->bindPipeline(maskPipeline);
            driver->bindVertexBuffer(0, quadBuffer, 0);
            driver->draw(4, 0);
        }

        driver->bindPipeline(cubePipelines[stencilMode]);
        driver->bindVertexBuffer(0, vertexBuffer, 0);
        driver->bindIndexBuffer(indexBuffer);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(Math::Mat4));
        driver->drawIndexed(36, 0);

        driver->bindPipeline(quadPipelines[blendMode]);
        driver->bindVertexBuffer(0, quadBuffer, 0);
        driver->draw(4, 0);

        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(maskPipeline);
    for (int i = 0; i < 4; ++i) driver->destroy(quadPipelines[i]);
    for (int i = 0; i < 3; ++i) driver->destroy(cubePipelines[i]);
    driver->destroy(uniformBuffer);
    driver->destroy(quadBuffer);
    driver->destroy(indexBuffer);
    driver->destroy(vertexBuffer);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
