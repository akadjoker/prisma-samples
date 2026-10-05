#include "common/Projection.h"
#include "common/SdkMesh.h"
#include "common/ZenApp.h"
#include "mathc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "model.frag.h"
#include "model.vert.h"

namespace
{

struct FrameUniforms
{
    Math::Mat4 modelViewProjection;
    Math::Mat4 model;
    float lightDirection[4];
    float params[4];
};

void measure(const zenapp::SdkMesh& mesh, Math::Vec3* center, float* size)
{
    const zenapp::SdkMeshData& data = mesh.data();
    const zenapp::SdkVertexBuffer& buffer = data.vertexBuffers[data.meshes[0].vertexBuffers[0]];
    unsigned offset = 0;
    for (unsigned e = 0; e < 32 && buffer.decl[e].stream != 0xFF; ++e)
        if (buffer.decl[e].usage == 0) offset = buffer.decl[e].offset;

    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (uint64_t i = 0; i < buffer.numVertices; ++i)
    {
        float position[3];
        memcpy(position, data.file.data() + buffer.dataOffset + i * buffer.strideBytes + offset,
                sizeof(position));
        low = Math::Vec3::Min(low, Math::Vec3(position[0], position[1], position[2]));
        high = Math::Vec3::Max(high, Math::Vec3(position[0], position[1], position[2]));
    }
    const Math::Vec3 half = (high - low) * 0.5f;
    *center = (low + high) * 0.5f;
    *size = half.x > half.y ? (half.x > half.z ? half.x : half.z)
                            : (half.y > half.z ? half.y : half.z);
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

    PlatformWindow* window = zenapp::openWindow("prisma 07 model", driverType);
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

    char path[1024];
    zenapp::mediaPath("Tiny/tiny.sdkmesh", path, sizeof(path));
    zenapp::SdkMesh mesh;
    bool ready = mesh.load(driver, path) && mesh.meshCount() > 0;
    if (!ready) log_error("model: cannot load %s", path);

    const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc textureDesc;
    textureDesc.width = 1;
    textureDesc.height = 1;
    textureDesc.data = whitePixel;
    textureDesc.debugName = "white";
    const prisma::TextureHandle white = driver->createTexture(textureDesc);

    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy =
            driver->caps().maxAnisotropy < 8.0f ? driver->caps().maxAnisotropy : 8.0f;
    samplerDesc.debugName = "model sampler";
    const prisma::SamplerHandle sampler = driver->createSampler(samplerDesc);

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = sizeof(FrameUniforms);
    bufferDesc.update = prisma::BufferUpdate::Stream;
    bufferDesc.debugName = "frame uniforms";
    const prisma::BufferHandle uniformBuffer = driver->createBuffer(bufferDesc);

    const prisma::ShaderHandle vertexShader = zenapp::createShader(driver, model_vert);
    const prisma::ShaderHandle fragmentShader = zenapp::createShader(driver, model_frag);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    if (ready) ready = mesh.layout(0, &pipelineDesc);
    pipelineDesc.depthTest = true;
    pipelineDesc.cullMode = prisma::CullMode::Back;
    pipelineDesc.debugName = "model pipeline";
    const prisma::PipelineHandle pipeline = driver->createPipeline(pipelineDesc);

    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    ready = ready && white.valid() && sampler.valid() && uniformBuffer.valid() && pipeline.valid();
    if (!ready) log_error("model: resource creation failed");

    Math::Vec3 center(0.0f, 0.0f, 0.0f);
    float size = 1.0f;
    if (ready) measure(mesh, &center, &size);
    const float scale = 2.0f / size;

    prisma::RenderPassDesc pass;
    pass.clearColor[0] = 0.08f;
    pass.clearColor[1] = 0.08f;
    pass.clearColor[2] = 0.10f;

    int frames = 0;
    while (ready && !window_should_close(window))
    {
        window_begin_frame(window);
        if (key_pressed(window, KEY_ESCAPE)) window_set_should_close(window, true);

        int width = 1;
        int height = 1;
        window_get_framebuffer_size(window, &width, &height);
        const float aspect =
                height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
        const float time = (still ? 0.0f : static_cast<float>(time_seconds())) + 0.6f;

        const Math::Mat4 projection = zenapp::perspectiveZeroToOne(1.0f, aspect, 0.1f, 100.0f);
        const Math::Mat4 view = Math::Mat4::Translation(Math::Vec3(0.0f, 0.0f, -5.0f));

        FrameUniforms frame;
        frame.model = Math::Mat4::RotationY(time * 0.5f) * Math::Mat4::RotationX(-1.5708f) *
                      Math::Mat4::Scale(Math::Vec3(scale, scale, scale)) *
                      Math::Mat4::Translation(-center);
        frame.modelViewProjection = projection * view * frame.model;
        const Math::Vec3 light = Math::Vec3(0.5f, 0.7f, 0.6f).Normalized();
        frame.lightDirection[0] = light.x;
        frame.lightDirection[1] = light.y;
        frame.lightDirection[2] = light.z;
        frame.lightDirection[3] = 0.0f;
        frame.params[0] = size * 0.05f * (0.5f + 0.5f * sinf(time * 2.0f));
        frame.params[1] = frame.params[2] = frame.params[3] = 0.0f;

        driver->beginFrame();
        driver->updateBuffer(uniformBuffer, 0, &frame, sizeof(frame));
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindUniformBuffer(0, uniformBuffer, 0, sizeof(FrameUniforms));
        for (unsigned i = 0; i < mesh.subsetCount(0); ++i)
        {
            const zenapp::SdkSubset& subset = mesh.subset(0, i);
            prisma::TextureHandle diffuse = mesh.diffuse(subset.materialId);
            if (!diffuse.valid()) diffuse = white;
            driver->bindTexture(0, diffuse, sampler);
            mesh.drawSubset(driver, 0, i);
        }
        driver->endRenderPass();
        zenapp::endFrame(driver);
        driver->present();

        if (maxFrames > 0 && ++frames >= maxFrames) window_set_should_close(window, true);
    }

    driver->destroy(pipeline);
    driver->destroy(uniformBuffer);
    driver->destroy(sampler);
    driver->destroy(white);
    mesh.destroy(driver);
    prisma::destroyDriver(driver);
    window_destroy(window);
    platform_shutdown();
    return ready ? 0 : 1;
}
