#include "AreaLights.h"
#include "Check.h"
#include "GpuContext.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include "area_light_probe.frag.h"
#include "no_buffer.vert.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

const double kPi = 3.14159265358979323846;

struct Probe
{
    float mode[4];
    float normal[4];
    float view[4];
    float position[4];
    float corners[4][4];
};

struct Vec
{
    double x, y, z;
};

Vec operator+(Vec a, Vec b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec operator-(Vec a, Vec b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec operator*(Vec a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec cross(Vec a, Vec b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
Vec normalized(Vec a) { return a * (1.0 / sqrt(dot(a, a))); }

Vec toVec(const float* a) { return { a[0], a[1], a[2] }; }

// The inverse matrix of the table, interpolated bilinearly like the sampler does.
void tableInverse(double perceptualRoughness, double noV, double* out)
{
    const double size = zenapp::ltc::kSize;
    double u = perceptualRoughness * (size - 1.0);
    double v = sqrt(1.0 - fmin(fmax(noV, 0.0), 1.0)) * (size - 1.0);
    const unsigned x0 = static_cast<unsigned>(floor(u));
    const unsigned y0 = static_cast<unsigned>(floor(v));
    const unsigned x1 = x0 + 1 < zenapp::ltc::kSize ? x0 + 1 : x0;
    const unsigned y1 = y0 + 1 < zenapp::ltc::kSize ? y0 + 1 : y0;
    const double fx = u - x0;
    const double fy = v - y0;
    for (int k = 0; k < 4; ++k)
    {
        const auto at = [&](unsigned x, unsigned y) {
            return static_cast<double>(zenapp::ltc::kInverse[(x + y * zenapp::ltc::kSize) * 4 + k]);
        };
        out[k] = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) +
                 (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
    }
}

// The integral over the quad of the density of the clamped cosine under the matrix (the identity
// when m is null), found by summing over a grid of the quad.
double referenceIntegral(const Probe& probe, const double* m, bool twoSided)
{
    const Vec n = normalized(toVec(probe.normal));
    const Vec v = normalized(toVec(probe.view));
    const Vec p = toVec(probe.position);
    const Vec c0 = toVec(probe.corners[0]);
    const Vec edgeU = toVec(probe.corners[1]) - c0;
    const Vec edgeV = toVec(probe.corners[3]) - c0;
    const Vec areaVector = cross(edgeU, edgeV);
    const Vec lightNormal = normalized(areaVector);
    const double area = sqrt(dot(areaVector, areaVector));
    if (!twoSided && dot(lightNormal, p - c0) <= 0.0) return 0.0;

    const Vec t1 = normalized(v - n * dot(v, n));
    const Vec t2 = cross(n, t1);
    const unsigned steps = 600;
    double sum = 0.0;
    for (unsigned i = 0; i < steps; ++i)
    {
        for (unsigned j = 0; j < steps; ++j)
        {
            const Vec x = c0 + edgeU * ((i + 0.5) / steps) + edgeV * ((j + 0.5) / steps);
            const Vec d = x - p;
            const double distanceSquared = dot(d, d);
            const Vec w = d * (1.0 / sqrt(distanceSquared));
            const double dOmega = fabs(dot(lightNormal, w)) * area / (steps * steps) / distanceSquared;
            Vec local = { dot(t1, w), dot(t2, w), dot(n, w) };
            double density;
            if (m)
            {
                const Vec q = { m[0] * local.x + m[2] * local.z, local.y,
                    m[1] * local.x + m[3] * local.z };
                const double length = sqrt(dot(q, q));
                const double determinant = fabs(m[0] * m[3] - m[2] * m[1]);
                density = fmax(q.z, 0.0) / length / kPi * determinant / (length * length * length);
            }
            else
                density = fmax(local.z, 0.0) / kPi;
            sum += density * dOmega;
        }
    }
    return sum;
}

} // namespace

int main(int argc, char** argv)
{
    using namespace prisma;

    GpuContext gpu;
    if (!gpu.open(argc, argv)) return 1;
    Driver* driver = gpu.driver;

    TextureDesc targetDesc;
    targetDesc.width = 4;
    targetDesc.height = 4;
    targetDesc.usage = kTextureSampled | kTextureRenderTarget;
    const TextureHandle target = driver->createTexture(targetDesc);
    const TextureHandle table = zenapp::createLtcTexture(driver);
    const SamplerHandle sampler = zenapp::createLtcSampler(driver);

    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::Uniform;
    bufferDesc.size = sizeof(Probe);
    bufferDesc.update = BufferUpdate::Stream;
    const BufferHandle buffer = driver->createBuffer(bufferDesc);

    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(area_light_probe_frag, driver->caps());
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
    const bool ready = target.valid() && table.valid() && sampler.valid() && buffer.valid() &&
                       pipeline.valid();
    CHECK(ready);
    if (!ready)
    {
        gpu.close();
        return 1;
    }

    const auto run = [&](const Probe& probe) {
        driver->beginFrame();
        driver->updateBuffer(buffer, 0, &probe, sizeof(probe));
        RenderPassDesc pass;
        pass.colors[0].texture = target;
        pass.colorCount = 1;
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindUniformBuffer(0, buffer, 0, sizeof(Probe));
        driver->bindTexture(2, table, sampler);
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
        return pixel[0] / 255.0 + pixel[1] / 65025.0 + pixel[2] / 16581375.0 +
               pixel[3] / 4228250625.0;
    };

    struct Case
    {
        const char* name;
        float position[3];
        float normal[3];
        float view[3];
        float center[3];
        float right[3];
        float up[3];
        bool twoSided;
        float roughness;
    };
    const Case cases[] = {
        { "overhead panel, floor", { 0, 0, 0 }, { 0, 1, 0 }, { 0.3f, 0.9f, 0.3f }, { 0, 2, 0 },
            { 1.2f, 0, 0 }, { 0, 0, 0.8f }, false, 0.35f },
        { "overhead panel, mirror like", { 0.5f, 0, 0.2f }, { 0, 1, 0 }, { 0.2f, 0.8f, -0.3f },
            { 0, 2, 0 }, { 1.2f, 0, 0 }, { 0, 0, 0.8f }, false, 0.12f },
        { "panel across the horizon", { 0, 0, 0 }, { 0, 1, 0 }, { 0.1f, 0.7f, 0.7f },
            { 0, 0.5f, 1.5f }, { 0, 1.0f, 0 }, { 1.0f, 0, 0 }, false, 0.4f },
        { "panel across the horizon, rough", { 0, 0, 0 }, { 0.2f, 1, 0.1f }, { 0.4f, 0.5f, 0.7f },
            { 0.5f, 0.3f, 1.2f }, { 0, 0.9f, 0 }, { 1.4f, 0, 0 }, false, 0.9f },
        { "seen from behind, one sided", { 0, 3, 0 }, { 0, -1, 0 }, { 0.2f, -0.9f, 0.1f },
            { 0, 2, 0 }, { 1.2f, 0, 0 }, { 0, 0, 0.8f }, false, 0.4f },
        { "seen from behind, two sided", { 0, 3, 0 }, { 0, -1, 0 }, { 0.2f, -0.9f, 0.1f },
            { 0, 2, 0 }, { 1.2f, 0, 0 }, { 0, 0, 0.8f }, true, 0.4f },
        { "tilted panel at the side", { -0.4f, 0, 0.3f }, { 0.1f, 1, 0 }, { 0.5f, 0.6f, 0.6f },
            { 2.2f, 1.1f, 0.2f }, { 0.2f, 0.3f, 1.1f }, { 0, 1.0f, -0.3f }, false, 0.6f },
        { "big panel close by", { 0, 0, 0 }, { 0, 1, 0 }, { 0, 1, 0.1f }, { 0, 0.6f, 0 },
            { 2.0f, 0, 0 }, { 0, 0, 2.0f }, false, 0.25f },
    };

    double worst = 0.0;
    for (const Case& c: cases)
    {
        const zenapp::AreaLightData light = zenapp::makeRectangleLight(c.center, c.right, c.up,
                c.center, 1.0f, c.twoSided);
        Probe probe;
        memset(&probe, 0, sizeof(probe));
        memcpy(probe.corners, light.corners, sizeof(probe.corners));
        memcpy(probe.normal, c.normal, sizeof(c.normal));
        memcpy(probe.view, c.view, sizeof(c.view));
        memcpy(probe.position, c.position, sizeof(c.position));
        probe.mode[1] = c.twoSided ? 1.0f : 0.0f;
        probe.mode[3] = c.roughness;

        for (int specular = 0; specular < 2; ++specular)
        {
            probe.mode[0] = static_cast<float>(specular);
            double matrix[4];
            const double* m = nullptr;
            if (specular)
            {
                const Vec n = normalized(toVec(probe.normal));
                const Vec v = normalized(toVec(probe.view));
                tableInverse(c.roughness, dot(n, v), matrix);
                m = matrix;
            }
            const double expected = referenceIntegral(probe, m, c.twoSided);
            const double got = run(probe);
            const double error = fabs(got - expected);
            printf("%-34s %s: reference %.5f, shader %.5f\n", c.name, specular ? "specular" : "diffuse ",
                    expected, got);
            worst = fmax(worst, error);
        }
    }
    printf("worst absolute error %.5f\n", worst);
    CHECK(worst < 0.01);

    driver->destroy(pipeline);
    driver->destroy(buffer);
    driver->destroy(sampler);
    driver->destroy(table);
    driver->destroy(target);
    gpu.close();
    return failures ? 1 : 0;
}
