#pragma once

#include "Lights.h"
#include "Projection.h"
#include "mathc.h"

#include <ct/sort.hpp>
#include <ct/vector.hpp>

#include <math.h>

namespace zenapp
{

struct ShadowMapView
{
    Math::Mat4 viewProjection;
    Math::Mat4 cullViewProjection;
    float texelScale = 2.0f;
    unsigned light = 0;
    unsigned face = 0;
};

struct ShadowUniforms
{
    enum
    {
        kMaxMaps = 192
    };

    float params[4];
    Math::Mat4 matrices[kMaxMaps];
    float info[kMaxMaps][4];
};

struct LightShadows
{
    ct::Vector<ShadowMapView> maps;
    ct::Vector<int> firstMap;
};

struct ShadowSlots
{
    enum
    {
        kMaxSlots = ShadowUniforms::kMaxMaps / 6,
        kMapsPerSlot = 6
    };

    int light[kMaxSlots];
    bool drawn[kMaxSlots];
    unsigned count = 0;
};

const float kShadowNear = 0.01f;
const float kShadowMaxHalfAngle = 1.4f;
const float kShadowReleaseFactor = 1.15f;

inline unsigned pointShadowFace(const Math::Vec3& fromLight)
{
    const float x = fabsf(fromLight.x);
    const float y = fabsf(fromLight.y);
    const float z = fabsf(fromLight.z);
    const float largest = x > y ? (x > z ? x : z) : (y > z ? y : z);
    if (largest == x) return fromLight.x >= 0.0f ? 0u : 1u;
    if (largest == y) return fromLight.y >= 0.0f ? 2u : 3u;
    return fromLight.z >= 0.0f ? 4u : 5u;
}

inline Math::Mat4 shadowView(const Math::Vec3& position, const Math::Vec3& direction)
{
    Math::Vec3 up(0.0f, 1.0f, 0.0f);
    if (fabsf(direction.Dot(up)) > 0.999f) up = Math::Vec3(up.z, up.x, up.y);
    return Math::Mat4::LookAt(position, position + direction, up);
}

inline Math::Mat4 pointShadowView(unsigned face, const Math::Vec3& position)
{
    static const float directions[6][3] = { { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } };
    return shadowView(position,
            Math::Vec3(directions[face][0], directions[face][1], directions[face][2]));
}

inline float shadowHalfAngle(const FroxelLight& light)
{
    if (!light.spot) return 0.78539816f;
    return light.outerAngle < kShadowMaxHalfAngle ? light.outerAngle : kShadowMaxHalfAngle;
}

inline unsigned lightShadowMaps(const FroxelLight& light, unsigned index, float nearPlane,
        ShadowMapView* out)
{
    const unsigned needed = light.spot ? 1u : 6u;
    const Math::Vec3 position(light.position[0], light.position[1], light.position[2]);
    const float halfAngle = shadowHalfAngle(light);
    const Math::Mat4 projection =
            perspectiveZeroToOne(halfAngle * 2.0f, 1.0f, nearPlane, light.radius);
    const Math::Mat4 cullProjection =
            Math::Mat4::Perspective(halfAngle * 2.0f, 1.0f, nearPlane, light.radius);
    for (unsigned face = 0; face < needed; ++face)
    {
        ShadowMapView& map = out[face];
        map.light = index;
        map.face = face;
        const Math::Mat4 view =
                light.spot ? shadowView(position, Math::Vec3(light.direction[0], light.direction[1],
                                                          light.direction[2]))
                           : pointShadowView(face, position);
        map.viewProjection = projection * view;
        map.cullViewProjection = cullProjection * view;
        map.texelScale = 2.0f * tanf(halfAngle);
    }
    return needed;
}

inline void selectLightShadows(const LightSet& set, const Math::Vec3& viewer, unsigned maxMaps,
        float nearPlane, LightShadows* out)
{
    out->maps.clear();
    out->firstMap.resize(set.count);
    ct::Vector<unsigned> order;
    ct::Vector<float> distance;
    order.reserve(set.count);
    distance.resize(set.count);
    for (unsigned i = 0; i < set.count; ++i)
    {
        const FroxelLight& light = set.culling[i];
        out->firstMap[i] = -1;
        distance[i] = (Math::Vec3(light.position[0], light.position[1], light.position[2]) - viewer)
                              .Length();
        if (light.radius > nearPlane) order.push_back(i);
    }
    if (order.size() > 1)
        ct::sort(order.data(), order.data() + order.size(), [&distance](unsigned a, unsigned b) {
            if (distance[a] != distance[b]) return distance[a] < distance[b];
            return a < b;
        });

    for (unsigned o = 0; o < order.size() && out->maps.size() < maxMaps; ++o)
    {
        const unsigned index = order[o];
        const FroxelLight& light = set.culling[index];
        const unsigned needed = light.spot ? 1u : 6u;
        if (out->maps.size() + needed > maxMaps) continue;

        ShadowMapView views[6];
        lightShadowMaps(light, index, nearPlane, views);
        out->firstMap[index] = static_cast<int>(out->maps.size());
        for (unsigned face = 0; face < needed; ++face) out->maps.push_back(views[face]);
    }
}

inline void clearShadowSlots(ShadowSlots* slots, unsigned count)
{
    slots->count = count < ShadowSlots::kMaxSlots ? count
                                                  : static_cast<unsigned>(ShadowSlots::kMaxSlots);
    for (unsigned i = 0; i < ShadowSlots::kMaxSlots; ++i)
    {
        slots->light[i] = -1;
        slots->drawn[i] = false;
    }
}

inline bool chooseShadowLights(LightSet* set, const Math::Frustum& view, const Math::Vec3& eye,
        float nearPlane, float maxDistance, ShadowSlots* slots)
{
    unsigned order[Froxelizer::kMaxLights + 1];
    float distance[Froxelizer::kMaxLights + 1];
    unsigned candidates = 0;
    for (unsigned i = 0; i < set->count; ++i)
    {
        const FroxelLight& light = set->culling[i];
        const Math::Vec3 position(light.position[0], light.position[1], light.position[2]);
        distance[i] = (position - eye).Length();
        if (light.radius > nearPlane && (maxDistance <= 0.0f || distance[i] <= maxDistance) &&
                view.IntersectsSphere(position, light.radius))
            order[candidates++] = i;
    }
    bool changed = false;
    if (maxDistance > 0.0f)
        for (unsigned s = 0; s < slots->count; ++s)
        {
            const int light = slots->light[s];
            if (light < 0 || distance[light] <= maxDistance * kShadowReleaseFactor) continue;
            set->lights[light].spot[3] = 0.0f;
            slots->light[s] = -1;
            slots->drawn[s] = false;
            changed = true;
        }
    if (candidates > 1)
        ct::sort(order, order + candidates, [&distance](unsigned a, unsigned b) {
            if (distance[a] != distance[b]) return distance[a] < distance[b];
            return a < b;
        });
    const unsigned wantedCount = candidates < slots->count ? candidates : slots->count;

    bool wantedSlot[ShadowSlots::kMaxSlots] = {};
    unsigned missing[ShadowSlots::kMaxSlots];
    unsigned missingCount = 0;
    for (unsigned w = 0; w < wantedCount; ++w)
    {
        bool found = false;
        for (unsigned s = 0; s < slots->count && !found; ++s)
            if (slots->light[s] == static_cast<int>(order[w]))
            {
                wantedSlot[s] = true;
                found = true;
            }
        if (!found) missing[missingCount++] = order[w];
    }

    for (unsigned m = 0; m < missingCount; ++m)
    {
        unsigned next = slots->count;
        for (unsigned s = 0; s < slots->count && next == slots->count; ++s)
            if (!wantedSlot[s] && slots->light[s] < 0) next = s;
        for (unsigned s = 0; s < slots->count && next == slots->count; ++s)
            if (!wantedSlot[s]) next = s;
        if (next >= slots->count) break;
        if (slots->light[next] >= 0) set->lights[slots->light[next]].spot[3] = 0.0f;
        slots->light[next] = static_cast<int>(missing[m]);
        slots->drawn[next] = false;
        wantedSlot[next] = true;
        changed = true;
    }
    return changed;
}

inline void applyLightShadows(const LightShadows& shadows, LightSet* set)
{
    for (unsigned i = 0; i < set->count && i < shadows.firstMap.size(); ++i)
        set->lights[i].spot[3] = static_cast<float>(shadows.firstMap[i] + 1);
}

inline void fillShadowUniforms(const LightShadows& shadows, unsigned mapSize, float normalOffset,
        ShadowUniforms* out)
{
    memset(out, 0, sizeof(*out));
    out->params[0] = mapSize > 0 ? 1.0f / static_cast<float>(mapSize) : 0.0f;
    out->params[1] = normalOffset;
    out->params[2] = static_cast<float>(shadows.maps.size());
    for (unsigned i = 0; i < shadows.maps.size() && i < ShadowUniforms::kMaxMaps; ++i)
    {
        out->matrices[i] = shadows.maps[i].viewProjection;
        out->info[i][0] = shadows.maps[i].texelScale;
    }
}

} // namespace zenapp
