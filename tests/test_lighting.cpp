#include "Check.h"
#include "GpuContext.h"
#include "Lights.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include "lighting_probe.frag.h"
#include "no_buffer.vert.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

const float kPi = 3.14159265358979f;

struct Probe
{
    float mode[4];
    float a[4];
    float b[4];
    float c[4];
    float d[4];
};

float dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void normalize3(float* a)
{
    const float length = sqrtf(dot3(a, a));
    for (int i = 0; i < 3; ++i) a[i] /= length;
}

float clamp01(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

float referenceD(float alpha, float noH)
{
    const float denominator = noH * noH * (alpha * alpha - 1.0f) + 1.0f;
    return alpha * alpha / (kPi * denominator * denominator);
}

float referenceV(float alpha, float noV, float noL)
{
    const float a2 = alpha * alpha;
    const float ggxV = noL * sqrtf(noV * noV * (1.0f - a2) + a2);
    const float ggxL = noV * sqrtf(noL * noL * (1.0f - a2) + a2);
    return 0.5f / (ggxV + ggxL);
}

float referenceFalloff(float distance, float radius)
{
    const float ratio = distance / radius;
    const float window = clamp01(1.0f - ratio * ratio * ratio * ratio);
    return window * window / (distance * distance);
}

float referenceF0ClearCoat(float f0)
{
    float value = f0 * (f0 * (0.941892f - 0.263008f * f0) + 0.346479f) - 0.0285998f;
    return clamp01(value);
}

void referenceShade(const float* n, const float* v, const float* l, const float* baseColor,
        float metallic, float perceptualRoughness, float clearCoat, float clearCoatRoughness,
        const float* radiance, float attenuation, float* out)
{
    const float coatPerceptual = fminf(fmaxf(clearCoatRoughness, 0.045f), 1.0f);
    float basePerceptual = fminf(fmaxf(perceptualRoughness, 0.045f), 1.0f);
    basePerceptual += (fmaxf(basePerceptual, coatPerceptual) - basePerceptual) * clearCoat;
    const float alpha = basePerceptual * basePerceptual;
    float h[3] = { v[0] + l[0], v[1] + l[1], v[2] + l[2] };
    normalize3(h);
    const float noL = clamp01(dot3(n, l));
    const float noV = fmaxf(dot3(n, v), 1e-4f);
    const float noH = clamp01(dot3(n, h));
    const float loH = clamp01(dot3(l, h));
    const float d = referenceD(alpha, noH);
    const float visibility = referenceV(alpha, noV, noL);
    float f0[3];
    float f0Sum = 0.0f;
    for (int c = 0; c < 3; ++c)
    {
        const float dielectric = baseColor[c] * metallic + 0.04f * (1.0f - metallic);
        f0[c] = dielectric + (referenceF0ClearCoat(dielectric) - dielectric) * clearCoat;
        f0Sum += f0[c];
    }
    const float f90 = clamp01(f0Sum * 50.0f * 0.33f);
    const float coatD = referenceD(coatPerceptual * coatPerceptual, noH);
    const float coatV = 0.25f / fmaxf(loH * loH, 0.0000039f);
    const float coatF = (0.04f + 0.96f * powf(1.0f - loH, 5.0f)) * clearCoat;
    for (int c = 0; c < 3; ++c)
    {
        const float fresnel = f0[c] + (f90 - f0[c]) * powf(1.0f - loH, 5.0f);
        const float diffuse = baseColor[c] * (1.0f - metallic) / kPi;
        float color = diffuse + d * visibility * fresnel;
        if (clearCoat > 0.0f) color = color * (1.0f - coatF) + coatD * coatV * coatF;
        out[c] += color * radiance[c] * attenuation * noL;
    }
}

struct ArrayLights
{
    float sunDirection[4];
    float sunColorIntensity[4];
    float counts[4];
    zenapp::LightData lights[16];
};

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

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    const unsigned probeStride = (sizeof(Probe) + alignment - 1) / alignment * alignment;
    const unsigned lightsOffset = probeStride;
    const unsigned lightsStride = (sizeof(ArrayLights) + alignment - 1) / alignment * alignment;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::Uniform;
    bufferDesc.size = probeStride + lightsStride;
    bufferDesc.update = BufferUpdate::Stream;
    const BufferHandle buffer = driver->createBuffer(bufferDesc);

    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(lighting_probe_frag, driver->caps());
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
    CHECK(target.valid() && buffer.valid() && pipeline.valid());
    if (!(target.valid() && buffer.valid() && pipeline.valid()))
    {
        gpu.close();
        return 1;
    }

    zenapp::LightSet set;
    zenapp::clearLights(&set);
    ArrayLights lights;
    memset(&lights, 0, sizeof(lights));
    ct::Vector<unsigned char> bytes;
    bytes.resize(probeStride + lightsStride);

    const auto run = [&](const Probe& probe) {
        memcpy(bytes.data(), &probe, sizeof(probe));
        memcpy(bytes.data() + lightsOffset, &lights, sizeof(lights));
        driver->beginFrame();
        driver->updateBuffer(buffer, 0, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        RenderPassDesc pass;
        pass.colors[0].texture = target;
        pass.colorCount = 1;
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindUniformBuffer(0, buffer, 0, sizeof(Probe));
        driver->bindUniformBuffer(3, buffer, lightsOffset, sizeof(ArrayLights));
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
        const float scale = static_cast<float>(probe.mode[2]);
        const float value = (pixel[0] / 255.0f + pixel[1] / 65025.0f + pixel[2] / 16581375.0f +
                                    pixel[3] / 4228250625.0f);
        return value * scale;
    };

    const float alphas[4] = { 0.05f, 0.3f, 0.6f, 1.0f };
    const float cosines[4] = { 0.2f, 0.6f, 0.9f, 0.999f };
    float worstD = 0.0f;
    float worstV = 0.0f;
    for (float alpha: alphas)
    {
        for (float c: cosines)
        {
            Probe probe;
            memset(&probe, 0, sizeof(probe));
            probe.mode[0] = 0.0f;
            probe.mode[2] = 70000.0f;
            probe.a[0] = alpha;
            probe.a[1] = c;
            const float expected = referenceD(alpha, c);
            const float got = run(probe);
            const float error = fmaxf(fabsf(got - expected) - 1e-4f, 0.0f) / fmaxf(expected, 1e-3f);
            worstD = error > worstD ? error : worstD;

            probe.mode[0] = 1.0f;
            probe.mode[2] = 4096.0f;
            probe.a[1] = c;
            probe.a[2] = 0.35f;
            const float expectedV = referenceV(alpha, c, 0.35f);
            const float gotV = run(probe);
            const float errorV = fmaxf(fabsf(gotV - expectedV) - 1e-4f, 0.0f) / fmaxf(expectedV, 1e-3f);
            worstV = errorV > worstV ? errorV : worstV;
        }
    }
    printf("distribution worst relative error %.5f, visibility %.5f\n", worstD, worstV);
    CHECK(worstD < 2e-3f);
    CHECK(worstV < 2e-3f);

    const float sunToLight[3] = { 0.3f, 0.8f, 0.5f };
    const float sunColor[3] = { 1.0f, 0.95f, 0.9f };
    const float pointPosition[3] = { 1.0f, 2.0f, 0.5f };
    const float pointColor[3] = { 0.4f, 0.7f, 1.0f };
    const float spotPosition[3] = { -1.0f, 1.5f, 1.0f };
    const float spotDirection[3] = { 0.5f, -0.6f, -0.4f };
    const float spotColor[3] = { 1.0f, 0.5f, 0.2f };
    const float pointRadius = 4.0f;
    const float spotRadius = 3.5f;
    const float spotInner = 0.25f;
    const float spotOuter = 0.5f;
    zenapp::setSunLight(&set, sunToLight, sunColor, 2.0f);
    zenapp::addPointLight(&set, pointPosition, pointColor, 12.0f, pointRadius);
    zenapp::addSpotLight(&set, spotPosition, spotDirection, spotColor, 18.0f, spotRadius,
            spotInner, spotOuter);
    CHECK(set.count == 2);
    memcpy(lights.sunDirection, set.sunDirection, sizeof(lights.sunDirection));
    memcpy(lights.sunColorIntensity, set.sunColorIntensity, sizeof(lights.sunColorIntensity));
    lights.counts[0] = static_cast<float>(set.count);
    memcpy(lights.lights, set.lights, sizeof(zenapp::LightData) * set.count);

    struct Case
    {
        float position[3];
        float normal[3];
        float view[3];
        float baseColor[3];
        float metallic;
        float roughness;
        float clearCoat;
        float clearCoatRoughness;
    };
    const Case cases[] = {
        { { 0.0f, 0.0f, 0.0f }, { 0, 1, 0 }, { 0.2f, 0.8f, 0.6f }, { 0.8f, 0.1f, 0.1f }, 0.0f, 0.5f },
        { { 0.0f, 0.0f, 0.0f }, { 0, 1, 0 }, { 0.2f, 0.8f, 0.6f }, { 1.0f, 0.71f, 0.29f }, 1.0f, 0.25f },
        { { 0.5f, 0.0f, 0.2f }, { 0.3f, 0.9f, 0.1f }, { -0.4f, 0.7f, 0.5f }, { 0.9f, 0.9f, 0.9f }, 0.5f, 0.8f },
        { { 1.0f, 0.2f, 1.2f }, { 0, 1, 0 }, { 0.0f, 1.0f, 0.3f }, { 0.2f, 0.4f, 0.8f }, 0.0f, 1.0f },
        { { 3.5f, 0.0f, 3.5f }, { 0, 1, 0 }, { 0.0f, 1.0f, 0.2f }, { 0.5f, 0.5f, 0.5f }, 0.0f, 0.6f },
        { { -0.5f, 0.0f, 0.2f }, { 0.3f, 0.7f, 0.2f }, { 0.5f, 0.6f, 0.3f }, { 0.7f, 0.7f, 0.2f }, 1.0f, 0.4f },
        { { 0.2f, 0.0f, 0.1f }, { 0.1f, 1.0f, 0.2f }, { 0.9f, 0.3f, 0.2f }, { 0.004f, 0.003f, 0.005f }, 1.0f, 0.3f },
        { { 0.0f, 0.0f, 0.0f }, { 0, 1, 0 }, { 0.2f, 0.8f, 0.6f }, { 0.7f, 0.0f, 0.0f }, 1.0f, 0.65f, 1.0f, 0.1f },
        { { 0.5f, 0.0f, 0.2f }, { 0.3f, 0.9f, 0.1f }, { -0.4f, 0.7f, 0.5f }, { 0.8f, 0.5f, 0.2f }, 1.0f, 0.3f, 0.5f, 0.4f },
        { { 1.0f, 0.2f, 1.2f }, { 0, 1, 0 }, { 0.5f, 0.6f, 0.4f }, { 0.2f, 0.5f, 0.3f }, 0.0f, 0.2f, 1.0f, 0.8f },
    };

    float worstShading = 0.0f;
    float peak = 0.0f;
    for (const Case& scenario: cases)
    {
        float n[3] = { scenario.normal[0], scenario.normal[1], scenario.normal[2] };
        float v[3] = { scenario.view[0], scenario.view[1], scenario.view[2] };
        normalize3(n);
        normalize3(v);

        float expected[3] = { 0.0f, 0.0f, 0.0f };
        float sunL[3] = { sunToLight[0], sunToLight[1], sunToLight[2] };
        normalize3(sunL);
        const float sunRadiance[3] = { sunColor[0] * 2.0f, sunColor[1] * 2.0f, sunColor[2] * 2.0f };
        referenceShade(n, v, sunL, scenario.baseColor, scenario.metallic, scenario.roughness,
                scenario.clearCoat, scenario.clearCoatRoughness, sunRadiance, 1.0f, expected);

        float toPoint[3] = { pointPosition[0] - scenario.position[0],
            pointPosition[1] - scenario.position[1], pointPosition[2] - scenario.position[2] };
        const float pointDistance = sqrtf(dot3(toPoint, toPoint));
        normalize3(toPoint);
        const float pointRadiance[3] = { pointColor[0] * 12.0f, pointColor[1] * 12.0f,
            pointColor[2] * 12.0f };
        referenceShade(n, v, toPoint, scenario.baseColor, scenario.metallic, scenario.roughness,
                scenario.clearCoat, scenario.clearCoatRoughness, pointRadiance, referenceFalloff(pointDistance, pointRadius), expected);

        float toSpot[3] = { spotPosition[0] - scenario.position[0],
            spotPosition[1] - scenario.position[1], spotPosition[2] - scenario.position[2] };
        const float spotDistance = sqrtf(dot3(toSpot, toSpot));
        normalize3(toSpot);
        float axis[3] = { spotDirection[0], spotDirection[1], spotDirection[2] };
        normalize3(axis);
        const float cosAngle = -dot3(axis, toSpot);
        const float cosOuter = cosf(spotOuter);
        const float cosInner = cosf(spotInner);
        const float cone = clamp01((cosAngle - cosOuter) / (cosInner - cosOuter));
        const float spotRadiance[3] = { spotColor[0] * 18.0f, spotColor[1] * 18.0f,
            spotColor[2] * 18.0f };
        referenceShade(n, v, toSpot, scenario.baseColor, scenario.metallic, scenario.roughness,
                scenario.clearCoat, scenario.clearCoatRoughness, spotRadiance, referenceFalloff(spotDistance, spotRadius) * cone * cone, expected);

        for (int channel = 0; channel < 3; ++channel)
        {
            Probe probe;
            memset(&probe, 0, sizeof(probe));
            probe.mode[0] = 2.0f;
            probe.mode[1] = static_cast<float>(channel);
            probe.mode[2] = 64.0f;
            for (int i = 0; i < 3; ++i)
            {
                probe.a[i] = scenario.normal[i];
                probe.b[i] = scenario.view[i];
                probe.c[i] = scenario.position[i];
                probe.d[i] = scenario.baseColor[i];
            }
            probe.a[3] = scenario.metallic;
            probe.b[3] = scenario.roughness;
            probe.c[3] = scenario.clearCoat;
            probe.d[3] = scenario.clearCoatRoughness;
            const float got = run(probe);
            const float error = fabsf(got - expected[channel]) /
                                fmaxf(expected[channel], 0.05f);
            worstShading = error > worstShading ? error : worstShading;
            peak = expected[channel] > peak ? expected[channel] : peak;
        }
    }
    printf("lighting worst relative error %.5f, brightest expected value %.3f\n", worstShading,
            peak);
    CHECK(worstShading < 5e-3f);
    CHECK(peak > 0.5f);

    driver->destroy(pipeline);
    driver->destroy(buffer);
    driver->destroy(target);
    gpu.close();

    printf(failures ? "test_lighting: %d failures\n" : "test_lighting: all passed\n", failures);
    return failures ? 1 : 0;
}
