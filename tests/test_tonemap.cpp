#include "Check.h"
#include "GpuContext.h"
#include "prisma/rhi/Driver.h"
#include "prisma/rhi/ShaderBlob.h"

#include "no_buffer.vert.h"
#include "tonemap_probe.frag.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace
{

struct Probe
{
    float mode[4];
    float color[4];
};

struct M3
{
    float c[3][3];
};

struct V3
{
    float x, y, z;
};

V3 mulV(const M3& m, V3 v)
{
    const float in[3] = { v.x, v.y, v.z };
    float out[3];
    for (int r = 0; r < 3; ++r) out[r] = m.c[0][r] * in[0] + m.c[1][r] * in[1] + m.c[2][r] * in[2];
    return { out[0], out[1], out[2] };
}

M3 mulM(const M3& a, const M3& b)
{
    M3 out;
    for (int col = 0; col < 3; ++col)
    {
        const V3 column = mulV(a, { b.c[col][0], b.c[col][1], b.c[col][2] });
        out.c[col][0] = column.x;
        out.c[col][1] = column.y;
        out.c[col][2] = column.z;
    }
    return out;
}

const M3 kSrgbToXyz = { { { 0.4124560f, 0.2126730f, 0.0193339f }, { 0.3575760f, 0.7151520f, 0.1191920f },
    { 0.1804380f, 0.0721750f, 0.9503040f } } };
const M3 kXyzToSrgb = { { { 3.2404542f, -0.9692660f, 0.0556434f }, { -1.5371385f, 1.8760108f, -0.2040259f },
    { -0.4985314f, 0.0415560f, 1.0572252f } } };
const M3 kRec2020ToXyz = { { { 0.6369530f, 0.2626983f, 0.0000000f }, { 0.1446169f, 0.6780088f, 0.0280731f },
    { 0.1688558f, 0.0592929f, 1.0608272f } } };
const M3 kXyzToRec2020 = { { { 1.7166634f, -0.6666738f, 0.0176425f }, { -0.3556733f, 1.6164557f, -0.0427770f },
    { -0.2533681f, 0.0157683f, 0.9422433f } } };
const M3 kAp1ToXyz = { { { 0.6624541811f, 0.2722287168f, -0.0055746495f },
    { 0.1340042065f, 0.6740817658f, 0.0040607335f }, { 0.1561876870f, 0.0536895174f, 1.0103391003f } } };
const M3 kXyzToAp1 = { { { 1.6410233797f, -0.6636628587f, 0.0117218943f },
    { -0.3248032942f, 1.6153315917f, -0.0082844420f }, { -0.2364246952f, 0.0167563477f, 0.9883948585f } } };
const M3 kAp1ToAp0 = { { { 0.6954522414f, 0.0447945634f, -0.0055258826f },
    { 0.1406786965f, 0.8596711185f, 0.0040252103f }, { 0.1638690622f, 0.0955343182f, 1.0015006723f } } };
const M3 kAp0ToAp1 = { { { 1.4514393161f, -0.0765537734f, 0.0083161484f },
    { -0.2365107469f, 1.1762296998f, -0.0060324498f }, { -0.2149285693f, -0.0996759264f, 0.9977163014f } } };
const V3 kLuminanceAp1 = { 0.272229f, 0.674082f, 0.0536895f };

float clampf(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

float dotv(V3 a, V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

V3 mixv(V3 a, V3 b, float t)
{
    return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
}

float referenceChannel(V3 colorSrgb, int channel, bool encode)
{
    const M3 srgbToRec2020 = mulM(kXyzToRec2020, kSrgbToXyz);
    const M3 rec2020ToSrgb = mulM(kXyzToSrgb, kRec2020ToXyz);
    const M3 rec2020ToAp0 = mulM(kAp1ToAp0, mulM(kXyzToAp1, kRec2020ToXyz));
    const M3 ap1ToRec2020 = mulM(kXyzToRec2020, kAp1ToXyz);

    V3 ap0 = mulV(rec2020ToAp0, mulV(srgbToRec2020, colorSrgb));
    const float mi = fminf(ap0.x, fminf(ap0.y, ap0.z));
    const float ma = fmaxf(ap0.x, fmaxf(ap0.y, ap0.z));
    const float saturation = (fmaxf(ma, 1e-5f) - fmaxf(mi, 1e-5f)) / fmaxf(ma, 1e-2f);
    const float chroma = sqrtf(fmaxf(ap0.z * (ap0.z - ap0.y) + ap0.y * (ap0.y - ap0.x) +
                                             ap0.x * (ap0.x - ap0.z), 0.0f));
    const float yc = (ap0.z + ap0.y + ap0.x + 1.75f * chroma) / 3.0f;
    const float x = (saturation - 0.4f) / 0.2f;
    const float t = fmaxf(1.0f - fabsf(x / 2.0f), 0.0f);
    const float sign = x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
    const float s = (1.0f + sign * (1.0f - t * t)) / 2.0f;
    const float gain = 0.05f * s;
    float glow;
    if (yc <= 2.0f / 3.0f * 0.08f) glow = gain;
    else if (yc >= 2.0f * 0.08f) glow = 0.0f;
    else glow = gain * (0.08f / yc - 0.5f);
    ap0 = { ap0.x * (1.0f + glow), ap0.y * (1.0f + glow), ap0.z * (1.0f + glow) };

    float hue = 0.0f;
    if (!(ap0.x == ap0.y && ap0.y == ap0.z))
        hue = atan2f(sqrtf(3.0f) * (ap0.y - ap0.z), 2.0f * ap0.x - ap0.y - ap0.z) * 57.29577951f;
    if (hue < 0.0f) hue += 360.0f;
    float centered = hue;
    if (centered < -180.0f) centered += 360.0f;
    else if (centered > 180.0f) centered -= 360.0f;
    const float u = clampf(1.0f - fabsf(2.0f * centered / 135.0f), 0.0f, 1.0f);
    float weight = u * u * (3.0f - 2.0f * u);
    weight *= weight;
    ap0.x += weight * saturation * (0.03f - ap0.x) * (1.0f - 0.82f);

    V3 ap1 = mulV(kAp0ToAp1, ap0);
    ap1 = { clampf(ap1.x, 0.0f, 65504.0f), clampf(ap1.y, 0.0f, 65504.0f), clampf(ap1.z, 0.0f, 65504.0f) };
    ap1 = mixv({ 1, 1, 1 }, ap1, 1.0f);
    ap1 = mixv({ dotv(ap1, kLuminanceAp1), dotv(ap1, kLuminanceAp1), dotv(ap1, kLuminanceAp1) }, ap1, 0.96f);
    const float brightness = 1.0f / 0.6f;
    ap1 = { ap1.x * brightness, ap1.y * brightness, ap1.z * brightness };

    const float a = 2.785085f, b = 0.107772f, c = 2.936045f, d = 0.887122f, e = 0.806889f;
    V3 post;
    post.x = (ap1.x * (a * ap1.x + b)) / (ap1.x * (c * ap1.x + d) + e);
    post.y = (ap1.y * (a * ap1.y + b)) / (ap1.y * (c * ap1.y + d) + e);
    post.z = (ap1.z * (a * ap1.z + b)) / (ap1.z * (c * ap1.z + d) + e);

    V3 xyz = mulV(kAp1ToXyz, post);
    const float sum = fmaxf(xyz.x + xyz.y + xyz.z, 1e-5f);
    const float xx = xyz.x / sum;
    const float yy = xyz.y / sum;
    const float luma = powf(clampf(xyz.y, 0.0f, 65504.0f), 0.9811f);
    const float k = luma / fmaxf(yy, 1e-5f);
    V3 linearCv = mulV(kXyzToAp1, { xx * k, luma, (1.0f - xx - yy) * k });
    const float lum = dotv(linearCv, kLuminanceAp1);
    linearCv = mixv({ lum, lum, lum }, linearCv, 0.93f);

    const V3 out = mulV(rec2020ToSrgb, mulV(ap1ToRec2020, linearCv));
    float values[3] = { clampf(out.x, 0.0f, 1.0f), clampf(out.y, 0.0f, 1.0f), clampf(out.z, 0.0f, 1.0f) };
    float v = values[channel];
    if (encode) v = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return v;
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

    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::Uniform;
    bufferDesc.size = sizeof(Probe);
    bufferDesc.update = BufferUpdate::Stream;
    const BufferHandle buffer = driver->createBuffer(bufferDesc);

    ShaderDesc vertexDesc = shaderDesc(no_buffer_vert, driver->caps());
    ShaderDesc fragmentDesc = shaderDesc(tonemap_probe_frag, driver->caps());
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

    const auto run = [&](const Probe& probe) {
        driver->beginFrame();
        driver->updateBuffer(buffer, 0, &probe, sizeof(probe));
        RenderPassDesc pass;
        pass.colors[0].texture = target;
        pass.colorCount = 1;
        driver->beginRenderPass(pass);
        driver->bindPipeline(pipeline);
        driver->bindUniformBuffer(0, buffer, 0, sizeof(Probe));
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
        return pixel[0] / 255.0f + pixel[1] / 65025.0f + pixel[2] / 16581375.0f +
               pixel[3] / 4228250625.0f;
    };

    const V3 colors[] = { { 0.0f, 0.0f, 0.0f }, { 0.005f, 0.005f, 0.005f }, { 0.18f, 0.18f, 0.18f },
        { 1.0f, 1.0f, 1.0f }, { 4.0f, 4.0f, 4.0f }, { 20.0f, 20.0f, 20.0f }, { 0.8f, 0.05f, 0.04f },
        { 0.05f, 0.6f, 0.1f }, { 0.04f, 0.1f, 0.9f }, { 0.35f, 0.18f, 0.08f }, { 0.6f, 0.4f, 0.1f },
        { 2.0f, 0.9f, 0.2f }, { 0.02f, 0.05f, 0.4f }, { 3.0f, 3.5f, 5.0f } };

    float worst = 0.0f;
    float darkest = 1.0f;
    float brightest = 0.0f;
    for (const V3& color: colors)
    {
        for (int encode = 0; encode < 2; ++encode)
        {
            for (int channel = 0; channel < 3; ++channel)
            {
                Probe probe;
                memset(&probe, 0, sizeof(probe));
                probe.mode[0] = static_cast<float>(encode);
                probe.mode[1] = static_cast<float>(channel);
                probe.color[0] = color.x;
                probe.color[1] = color.y;
                probe.color[2] = color.z;
                const float got = run(probe);
                const float expected = referenceChannel(color, channel, encode != 0);
                const float error = fabsf(got - expected);
                worst = error > worst ? error : worst;
                darkest = expected < darkest ? expected : darkest;
                brightest = expected > brightest ? expected : brightest;
            }
        }
    }
    printf("aces legacy worst absolute error %.6f, outputs from %.4f to %.4f\n", worst, darkest,
            brightest);
    CHECK(worst < 2e-3f);
    CHECK(brightest > 0.95f);
    CHECK(darkest < 0.01f);

    driver->destroy(pipeline);
    driver->destroy(buffer);
    driver->destroy(target);
    gpu.close();
    printf(failures ? "test_tonemap: %d failures\n" : "test_tonemap: all passed\n", failures);
    return failures ? 1 : 0;
}
