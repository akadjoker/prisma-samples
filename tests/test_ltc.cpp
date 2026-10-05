#include "Check.h"
#include "LtcTables.h"

#include <math.h>
#include <stdio.h>

namespace
{

const double kPi = 3.14159265358979323846;

// The BRDF times the cosine of the fit of ltc_code: GGX with the height correlated Smith term and
// no Fresnel, for a view in the x-z plane.
double brdfCosine(const double* v, const double* l, double alpha)
{
    const auto lambda = [&](double c) {
        return 0.5 * (-1.0 + sqrt(1.0 + alpha * alpha * (1.0 - c * c) / (c * c)));
    };
    const double g2 = 1.0 / (1.0 + lambda(v[2]) + lambda(l[2]));
    double h[3] = { v[0] + l[0], v[1] + l[1], v[2] + l[2] };
    const double length = sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    for (double& x: h) x /= length;
    const double slope = (h[0] * h[0] + h[1] * h[1]) / (h[2] * h[2]);
    double d = 1.0 / (1.0 + slope / (alpha * alpha));
    d = d * d / (kPi * alpha * alpha * h[2] * h[2] * h[2] * h[2]);
    return d * g2 / (4.0 * v[2]);
}

struct Texel
{
    double alpha;
    double v[3];
    const float* inverse;
};

Texel texel(unsigned roughnessIndex, unsigned thetaIndex)
{
    Texel t;
    const double roughness = static_cast<double>(roughnessIndex) / (zenapp::ltc::kSize - 1);
    t.alpha = fmax(roughness * roughness, 1e-4);
    const double x = static_cast<double>(thetaIndex) / (zenapp::ltc::kSize - 1);
    const double theta = fmin(1.57, acos(1.0 - x * x));
    t.v[0] = sin(theta);
    t.v[1] = 0.0;
    t.v[2] = cos(theta);
    t.inverse = zenapp::ltc::kInverse + (roughnessIndex + thetaIndex * zenapp::ltc::kSize) * 4;
    return t;
}

// The density of the transformed cosine in direction w, with the inverse matrix read as
// mat3(vec3(x, 0, y), vec3(0, 1, 0), vec3(z, 0, w)) of the texel (the way the shader reads it) or,
// with transposed set, as its transpose.
double ltcDensity(const Texel& t, const double* w, bool transposed)
{
    const double a = t.inverse[0];
    const double b = transposed ? t.inverse[1] : t.inverse[2];
    const double c = transposed ? t.inverse[2] : t.inverse[1];
    const double d = t.inverse[3];
    const double p[3] = { a * w[0] + b * w[2], w[1], c * w[0] + d * w[2] };
    const double length = sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    const double determinant = fabs(a * d - b * c);
    return fmax(p[2], 0.0) / length / kPi * determinant / (length * length * length);
}

} // namespace

int main()
{
    const unsigned samples[][2] = { { 40, 20 }, { 30, 45 }, { 50, 60 }, { 20, 10 }, { 60, 30 },
        { 10, 50 } };
    const unsigned thetaSteps = 240;
    const unsigned phiSteps = 480;
    double worstMagnitude = 0.0;
    double worstShape[2] = { 0.0, 0.0 };
    for (const auto& s: samples)
    {
        const Texel t = texel(s[0], s[1]);
        double norm = 0.0;
        double target = 0.0;
        double errors[2] = { 0.0, 0.0 };
        double integral[2] = { 0.0, 0.0 };
        for (unsigned i = 0; i < thetaSteps; ++i)
        {
            const double theta = (i + 0.5) / thetaSteps * kPi * 0.5;
            for (unsigned j = 0; j < phiSteps; ++j)
            {
                const double phi = (j + 0.5) / phiSteps * kPi * 2.0;
                const double l[3] = { sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta) };
                const double dw = sin(theta) * (kPi * 0.5 / thetaSteps) * (kPi * 2.0 / phiSteps);
                const double res = brdfCosine(t.v, l, t.alpha);
                norm += res * dw;
                target += res * res * dw;
            }
        }
        for (unsigned i = 0; i < thetaSteps; ++i)
        {
            const double theta = (i + 0.5) / thetaSteps * kPi * 0.5;
            for (unsigned j = 0; j < phiSteps; ++j)
            {
                const double phi = (j + 0.5) / phiSteps * kPi * 2.0;
                const double l[3] = { sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta) };
                const double dw = sin(theta) * (kPi * 0.5 / thetaSteps) * (kPi * 2.0 / phiSteps);
                const double res = brdfCosine(t.v, l, t.alpha) / norm;
                for (int k = 0; k < 2; ++k)
                {
                    const double density = ltcDensity(t, l, k == 1);
                    errors[k] += (density - res) * (density - res) * dw;
                    integral[k] += density * dw;
                }
            }
        }
        const double magnitude = zenapp::ltc::kMagnitude[s[0] + s[1] * zenapp::ltc::kSize];
        const double magnitudeError = fabs(norm - magnitude) / magnitude;
        const double reference = target / (norm * norm);
        printf("roughness %2u theta %2u: magnitude %.4f table %.4f, shape error %.4f (as the "
               "shader reads it) %.4f (transposed)\n",
                s[0], s[1], norm, magnitude, sqrt(errors[0] / reference),
                sqrt(errors[1] / reference));
        worstMagnitude = fmax(worstMagnitude, magnitudeError);
        for (int k = 0; k < 2; ++k) worstShape[k] = fmax(worstShape[k], sqrt(errors[k] / reference));
    }
    printf("worst magnitude error %.4f, worst shape error %.4f / %.4f\n", worstMagnitude,
            worstShape[0], worstShape[1]);
    CHECK(worstMagnitude < 0.03);
    CHECK(worstShape[0] < 0.3);
    CHECK(worstShape[0] * 3.0 < worstShape[1]);
    return failures ? 1 : 0;
}
