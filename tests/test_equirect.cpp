#include "Check.h"
#include "Equirect.h"

#include <math.h>
#include <stdio.h>

namespace
{

const float kPi = 3.14159265358979f;

void normalize(float* v)
{
    const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= length;
}

float dot(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void directionFromColor(const float* color, float* out)
{
    for (int i = 0; i < 3; ++i) out[i] = color[i] * 2.0f - 1.0f;
    normalize(out);
}

zenapp::EquirectImage directionImage(unsigned width, unsigned height)
{
    zenapp::EquirectImage image;
    image.width = width;
    image.height = height;
    image.rgb.resize(static_cast<size_t>(width) * height * 3);
    for (unsigned j = 0; j < height; ++j)
    {
        for (unsigned i = 0; i < width; ++i)
        {
            const float phi = ((i + 0.5f) / width * 2.0f - 1.0f) * kPi;
            const float lat = (1.0f - (j + 0.5f) / height * 2.0f) * kPi * 0.5f;
            const float direction[3] = { cosf(lat) * sinf(phi), sinf(lat), cosf(lat) * cosf(phi) };
            float* texel = image.rgb.data() + (static_cast<size_t>(j) * width + i) * 3;
            for (int c = 0; c < 3; ++c) texel[c] = direction[c] * 0.5f + 0.5f;
        }
    }
    return image;
}

} // namespace

int main()
{
    const zenapp::EquirectImage image = directionImage(256, 128);
    zenapp::EnvironmentFaces faces;
    CHECK(zenapp::equirectToFaces(image, 64, &faces));
    CHECK(faces.size == 64 && faces.data.size() == 64u * 64u * 4u * 6u);

    const unsigned c = 32;
    const float expected[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 } };
    for (unsigned face = 0; face < 6; ++face)
    {
        const float* texel = faces.face(face) + (static_cast<size_t>(c) * 64 + c) * 4;
        float decoded[3];
        directionFromColor(texel, decoded);
        float centre[3] = { expected[face][0], expected[face][1], expected[face][2] };
        const float s = (c + 0.5f) / 32.0f - 1.0f;
        const float t = 1.0f - (c + 0.5f) / 32.0f;
        float direction[3];
        switch (face)
        {
        case 0: direction[0] = 1; direction[1] = t; direction[2] = -s; break;
        case 1: direction[0] = -1; direction[1] = t; direction[2] = s; break;
        case 2: direction[0] = s; direction[1] = 1; direction[2] = -t; break;
        case 3: direction[0] = s; direction[1] = -1; direction[2] = t; break;
        case 4: direction[0] = s; direction[1] = t; direction[2] = 1; break;
        default: direction[0] = -s; direction[1] = t; direction[2] = -1; break;
        }
        normalize(direction);
        CHECK(dot(decoded, direction) > 0.995f);
        CHECK(dot(decoded, centre) > 0.99f);
    }

    {
        const float s = 2.0f * (48 + 0.5f) / 64.0f - 1.0f;
        const float t = 1.0f - 2.0f * (32 + 0.5f) / 64.0f;
        const float* texel = faces.face(0) + (static_cast<size_t>(32) * 64 + 48) * 4;
        float decoded[3];
        directionFromColor(texel, decoded);
        float wanted[3] = { 1.0f, t, -s };
        normalize(wanted);
        CHECK(dot(decoded, wanted) > 0.995f);
    }
    {
        const float s = 2.0f * (32 + 0.5f) / 64.0f - 1.0f;
        const float t = 1.0f - 2.0f * (16 + 0.5f) / 64.0f;
        const float* texel = faces.face(2) + (static_cast<size_t>(16) * 64 + 32) * 4;
        float decoded[3];
        directionFromColor(texel, decoded);
        float wanted[3] = { s, 1.0f, -t };
        normalize(wanted);
        CHECK(dot(decoded, wanted) > 0.995f);
    }

    zenapp::EnvironmentFaces coarse;
    CHECK(zenapp::equirectToFaces(image, 16, &coarse));
    for (size_t i = 0; i < coarse.data.size(); ++i) CHECK(coarse.data[i] >= 0.0f && coarse.data[i] <= 1.0f);

    zenapp::EquirectImage notEquirect = directionImage(64, 64);
    zenapp::EnvironmentFaces none;
    CHECK(!zenapp::equirectToFaces(notEquirect, 16, &none));
    CHECK(!zenapp::equirectToFaces(image, 0, &none));

    zenapp::EnvironmentFaces missing;
    CHECK(!zenapp::loadEquirectFaces("does/not/exist.hdr", 0, &missing));

    if (failures) printf("%d failed\n", failures);
    else
        printf("ok\n");
    return failures ? 1 : 0;
}
