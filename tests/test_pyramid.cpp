#include "Check.h"
#include "ColorPyramid.h"
#include "GpuContext.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include "no_buffer.vert.h"
#include "pyramid_probe.frag.h"

#include <math.h>
#include <stdio.h>

int main(int argc, char** argv)
{
    using namespace prisma;

    GpuContext gpu;
    if (!gpu.open(argc, argv)) return 1;
    Driver* driver = gpu.driver;
    if (!driver->caps().floatColorTargets)
    {
        printf("test_pyramid: no float color targets\n");
        gpu.close();
        return 0;
    }

    zenapp::ColorPyramid pyramid;
    CHECK(zenapp::createColorPyramid(driver, 1280, 720, &pyramid));
    CHECK(pyramid.width[0] == 1280 && pyramid.height[0] == 720);
    CHECK(pyramid.width[6] == 20 && pyramid.height[6] == 11);

    TextureDesc targetDesc;
    targetDesc.width = 4;
    targetDesc.height = 4;
    targetDesc.usage = kTextureSampled | kTextureRenderTarget;
    const TextureHandle target = driver->createTexture(targetDesc);
    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(pyramid_probe_frag, driver->caps());
    const ShaderHandle vertexShader = driver->createShader(vertexDesc);
    const ShaderHandle fragmentShader = driver->createShader(fragmentDesc);
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.targets.window = false;
    pipelineDesc.targets.colorCount = 1;
    pipelineDesc.targets.colors[0] = TextureFormat::RGBA8;
    const PipelineHandle pipeline = driver->createPipeline(pipelineDesc);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);
    CHECK(target.valid() && pipeline.valid());
    if (!(target.valid() && pipeline.valid()))
    {
        gpu.close();
        return 1;
    }

    const float colors[3][3] = { { 0.8f, 0.4f, 0.2f }, { 3.9f, 1.0f, 0.1f }, { 0.0f, 0.0f, 0.0f } };
    float worst = 0.0f;
    for (int c = 0; c < 3; ++c)
    {
        driver->beginFrame();
        RenderPassDesc fill;
        fill.colors[0].texture = pyramid.level[0];
        fill.colorCount = 1;
        fill.depthLoad = LoadOp::DontCare;
        fill.stencilLoad = LoadOp::DontCare;
        fill.clearColor[0] = colors[c][0];
        fill.clearColor[1] = colors[c][1];
        fill.clearColor[2] = colors[c][2];
        fill.clearColor[3] = 1.0f;
        driver->beginRenderPass(fill);
        driver->endRenderPass();
        zenapp::renderColorPyramid(driver, pyramid);
        driver->endFrame();
        driver->present();

        for (unsigned level = 0; level < zenapp::ColorPyramid::kLevels; ++level)
        {
            driver->beginFrame();
            RenderPassDesc pass;
            pass.colors[0].texture = target;
            pass.colorCount = 1;
            driver->beginRenderPass(pass);
            driver->bindPipeline(pipeline);
            driver->bindTexture(0, pyramid.level[level], pyramid.sampler);
            driver->draw(3, 0);
            driver->endRenderPass();
            unsigned char pixel[4] = { 0, 0, 0, 0 };
            Rect rect;
            rect.x = 1;
            rect.y = 1;
            rect.width = 1;
            rect.height = 1;
            RenderTarget renderTarget;
            renderTarget.texture = target;
            CHECK(driver->readPixels(renderTarget, rect, pixel));
            driver->endFrame();
            driver->present();
            for (int k = 0; k < 3; ++k)
            {
                const float expected = fminf(colors[c][k] * 0.25f, 1.0f);
                const float error = fabsf(pixel[k] / 255.0f - expected);
                worst = error > worst ? error : worst;
            }
        }
    }
    printf("color pyramid: worst error of a constant colour over seven levels %.4f\n", worst);
    CHECK(worst < 0.012f);

    driver->destroy(pipeline);
    driver->destroy(target);
    zenapp::destroyColorPyramid(driver, &pyramid);
    gpu.close();
    printf(failures ? "test_pyramid: %d failures\n" : "test_pyramid: all passed\n", failures);
    return failures ? 1 : 0;
}
