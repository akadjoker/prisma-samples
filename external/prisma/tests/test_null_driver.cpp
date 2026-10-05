#include "Check.h"
#include "prisma/rhi/Driver.h"

int main()
{
    using namespace prisma;

    CHECK(isDriverSupported(DriverType::Null));

    DriverDesc desc;
    DriverError error = DriverError::None;

    desc.type = DriverType::Vulkan;
    CHECK(createDriver(desc, &error) == nullptr);
    CHECK(error == (isDriverSupported(DriverType::Vulkan) ? DriverError::MissingPlatform
                                                          : DriverError::NotCompiled));

    if (isDriverSupported(DriverType::OpenGL))
    {
        desc.type = DriverType::OpenGL;
        CHECK(createDriver(desc, &error) == nullptr);
        CHECK(error == DriverError::MissingPlatform);
    }

    desc.type = DriverType::Null;
    Driver* driver = createDriver(desc, &error);
    CHECK(error == DriverError::None);
    CHECK(driver != nullptr);
    if (driver)
    {
        CHECK(driver->type() == DriverType::Null);
        CHECK(!driver->caps().compute);
        CHECK(!driver->createBuffer(BufferDesc()).valid());

        static const float data[6] = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f };
        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(data);
        bufferDesc.data = data;
        const BufferHandle buffer = driver->createBuffer(bufferDesc);
        CHECK(buffer.valid());
        CHECK(sizeof(buffer) == 4);

        ShaderDesc shaderDesc;
        shaderDesc.source = "void main() {}";
        const ShaderHandle vertexShader = driver->createShader(shaderDesc);
        shaderDesc.stage = ShaderStage::Fragment;
        const ShaderHandle fragmentShader = driver->createShader(shaderDesc);
        CHECK(vertexShader.valid());
        CHECK(fragmentShader.valid());

        CHECK(!driver->createPipeline(PipelineDesc()).valid());
        PipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.fragmentShader = fragmentShader;
        const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
        CHECK(pipeline.valid());

        driver->beginFrame();
        driver->beginRenderPass(RenderPassDesc());
        driver->bindPipeline(pipeline);
        driver->bindVertexBuffer(0, buffer, 0);
        driver->draw(3, 0);
        driver->endRenderPass();
        driver->endFrame();
        driver->present();

        driver->destroy(vertexShader);
        pipelineDesc.vertexShader = vertexShader;
        CHECK(!driver->createPipeline(pipelineDesc).valid());

        driver->destroy(pipeline);
        driver->destroy(buffer);
        const BufferHandle reused = driver->createBuffer(bufferDesc);
        CHECK(reused.valid());
        CHECK(reused != buffer);
        driver->destroy(fragmentShader);
        driver->destroy(reused);
        destroyDriver(driver);
    }

    if (failures)
    {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
