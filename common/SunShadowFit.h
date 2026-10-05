#pragma once

#include "LightShadows.h"
#include "SunShadow.h"
#include "mathc.h"

#include <math.h>
#include <string.h>

namespace zenapp
{

inline void fitSunShadow(const Math::Vec3& boundsLow, const Math::Vec3& boundsHigh,
        const Math::Vec3& toSun, unsigned size, float normalBias, SunShadowParams* out)
{
    const Math::Vec3 center = (boundsLow + boundsHigh) * 0.5f;
    const float radius = (boundsHigh - boundsLow).Length() * 0.5f;
    const Math::Mat4 view = shadowView(center + toSun * (radius * 2.0f), toSun * -1.0f);
    Math::Vec3 low(1e30f, 1e30f, 1e30f);
    Math::Vec3 high(-1e30f, -1e30f, -1e30f);
    for (int corner = 0; corner < 8; ++corner)
    {
        const Math::Vec4 world((corner & 1) ? boundsHigh.x : boundsLow.x,
                (corner & 2) ? boundsHigh.y : boundsLow.y, (corner & 4) ? boundsHigh.z : boundsLow.z,
                1.0f);
        const Math::Vec4 inLight = view * world;
        low = Math::Vec3(fminf(low.x, inLight.x), fminf(low.y, inLight.y), fminf(low.z, inLight.z));
        high = Math::Vec3(fmaxf(high.x, inLight.x), fmaxf(high.y, inLight.y),
                fmaxf(high.z, inLight.z));
    }
    const float padding = radius * 0.02f;
    const Math::Mat4 viewProjection = orthographicZeroToOne(low.x - padding, high.x + padding,
            low.y - padding, high.y + padding, -high.z - padding, -low.z + padding) * view;

    memset(out, 0, sizeof(*out));
    memcpy(out->matrix, viewProjection.Data(), sizeof(out->matrix));
    out->params[0] = static_cast<float>(size);
    out->params[1] = 1.0f / static_cast<float>(size);
    out->params[2] = normalBias * (high.x - low.x + 2.0f * padding) / static_cast<float>(size);
    out->params[3] = normalBias * (high.y - low.y + 2.0f * padding) / static_cast<float>(size);
    const Math::Mat4 lightToWorld = view.Inverse();
    for (int i = 0; i < 3; ++i)
    {
        out->axisX[i] = lightToWorld.Data()[i];
        out->axisY[i] = lightToWorld.Data()[4 + i];
    }
}

} // namespace zenapp
