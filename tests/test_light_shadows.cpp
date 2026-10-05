#include "Check.h"
#include "LightShadows.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{

unsigned state = 12345u;

float random01()
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
}

float randomRange(float low, float high) { return low + (high - low) * random01(); }

bool inside(const Math::Mat4& viewProjection, const Math::Vec3& point, float slack)
{
    const Math::Vec4 clip = viewProjection * Math::Vec4(point, 1.0f);
    if (clip.w <= 0.0f) return false;
    const float limit = clip.w * (1.0f + slack);
    return fabsf(clip.x) <= limit && fabsf(clip.y) <= limit && clip.z >= -clip.w * slack &&
           clip.z <= limit;
}

float depth(const Math::Mat4& viewProjection, const Math::Vec3& point)
{
    const Math::Vec4 clip = viewProjection * Math::Vec4(point, 1.0f);
    return clip.z / clip.w;
}

} // namespace

int main()
{
    const float white[3] = { 1.0f, 1.0f, 1.0f };
    const Math::Vec3 centre(1.0f, 2.0f, 3.0f);
    const float radius = 5.0f;

    static zenapp::LightSet single;
    zenapp::clearLights(&single);
    const float centrePosition[3] = { centre.x, centre.y, centre.z };
    zenapp::addPointLight(&single, centrePosition, white, 100.0f, radius);

    zenapp::LightShadows shadows;
    zenapp::selectLightShadows(single, Math::Vec3(0.0f, 0.0f, 0.0f), 64, zenapp::kShadowNear,
            &shadows);
    CHECK(shadows.maps.size() == 6);
    CHECK(shadows.firstMap.size() == 1 && shadows.firstMap[0] == 0);
    for (unsigned face = 0; face < shadows.maps.size(); ++face)
    {
        CHECK(shadows.maps[face].light == 0);
        CHECK(shadows.maps[face].face == face);
        CHECK(fabsf(shadows.maps[face].texelScale - 2.0f) < 1e-5f);
        const Math::Frustum frustum =
                Math::Frustum::FromViewProjection(shadows.maps[face].cullViewProjection);
        static const float axes[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
            { 0, 0, 1 }, { 0, 0, -1 } };
        const Math::Vec3 axis(axes[face][0], axes[face][1], axes[face][2]);
        CHECK(frustum.ContainsPoint(centre + axis * 2.0f));
        CHECK(!frustum.ContainsPoint(centre - axis * 2.0f));
        CHECK(!frustum.ContainsPoint(centre + axis * (radius + 1.0f)));
    }

    unsigned outsideOwnFace = 0;
    unsigned insideOtherFace = 0;
    const unsigned samples = 20000;
    for (unsigned s = 0; s < samples && shadows.maps.size() == 6; ++s)
    {
        Math::Vec3 direction(randomRange(-1.0f, 1.0f), randomRange(-1.0f, 1.0f),
                randomRange(-1.0f, 1.0f));
        const float length = direction.Length();
        if (length < 0.05f) continue;
        direction = direction * (1.0f / length);
        const Math::Vec3 point = centre + direction * randomRange(0.05f, radius * 0.99f);
        const unsigned face = zenapp::pointShadowFace(point - centre);
        if (!inside(shadows.maps[face].viewProjection, point, 0.001f)) ++outsideOwnFace;

        const float x = fabsf(direction.x);
        const float y = fabsf(direction.y);
        const float z = fabsf(direction.z);
        const float largest = x > y ? (x > z ? x : z) : (y > z ? y : z);
        const float second = largest == x ? (y > z ? y : z) : largest == y ? (x > z ? x : z)
                                                                           : (x > y ? x : y);
        if (second > largest * 0.98f) continue;
        for (unsigned other = 0; other < 6; ++other)
            if (other != face && inside(shadows.maps[other].viewProjection, point, 0.0f))
                ++insideOtherFace;
    }
    CHECK(outsideOwnFace == 0);
    CHECK(insideOtherFace == 0);

    if (shadows.maps.size() == 6)
    {
        const Math::Mat4& positiveX = shadows.maps[0].viewProjection;
        const float nearDepth = depth(positiveX, centre + Math::Vec3(zenapp::kShadowNear, 0, 0));
        const float middleDepth = depth(positiveX, centre + Math::Vec3(1.0f, 0.0f, 0.0f));
        const float farDepth = depth(positiveX, centre + Math::Vec3(radius, 0.0f, 0.0f));
        CHECK(fabsf(nearDepth) < 1e-3f);
        CHECK(fabsf(farDepth - 1.0f) < 1e-3f);
        CHECK(nearDepth < middleDepth && middleDepth < farDepth);

        const Math::Vec4 axis = positiveX * Math::Vec4(centre + Math::Vec3(2.0f, 0.0f, 0.0f), 1.0f);
        CHECK(fabsf(axis.x) < 1e-4f && fabsf(axis.y) < 1e-4f);
        const Math::Vec4 above =
                positiveX * Math::Vec4(centre + Math::Vec3(2.0f, 1.0f, 0.0f), 1.0f);
        CHECK(above.y > 0.0f && fabsf(above.x) < 1e-4f);
    }

    static zenapp::LightSet many;
    zenapp::clearLights(&many);
    const float p1[3] = { 3.0f, 0.0f, 0.0f };
    const float p2[3] = { 1.0f, 0.0f, 0.0f };
    const float p3[3] = { 2.0f, 0.0f, 0.0f };
    const float ps[3] = { 0.0f, 2.5f, 0.0f };
    const float down[3] = { 0.0f, -1.0f, 0.0f };
    const int far = zenapp::addPointLight(&many, p1, white, 10.0f, 4.0f);
    const int nearest = zenapp::addPointLight(&many, p2, white, 10.0f, 4.0f);
    const int middle = zenapp::addPointLight(&many, p3, white, 10.0f, 4.0f);
    const int spot = zenapp::addSpotLight(&many, ps, down, white, 10.0f, 6.0f, 0.3f, 0.6f);
    const Math::Vec3 viewer(0.0f, 0.0f, 0.0f);

    zenapp::selectLightShadows(many, viewer, 13, zenapp::kShadowNear, &shadows);
    CHECK(shadows.maps.size() == 13);
    CHECK(shadows.firstMap[nearest] == 0);
    CHECK(shadows.firstMap[middle] == 6);
    CHECK(shadows.firstMap[spot] == 12);
    CHECK(shadows.firstMap[far] == -1);

    zenapp::selectLightShadows(many, viewer, 7, zenapp::kShadowNear, &shadows);
    CHECK(shadows.maps.size() == 7);
    CHECK(shadows.firstMap[nearest] == 0);
    CHECK(shadows.firstMap[middle] == -1);
    CHECK(shadows.firstMap[spot] == 6);

    zenapp::selectLightShadows(many, viewer, 5, zenapp::kShadowNear, &shadows);
    CHECK(shadows.maps.size() == 1);
    CHECK(shadows.firstMap[spot] == 0);

    zenapp::applyLightShadows(shadows, &many);
    CHECK(many.lights[spot].spot[3] == 1.0f);
    CHECK(many.lights[nearest].spot[3] == 0.0f);
    static zenapp::ShadowUniforms uniforms;
    zenapp::fillShadowUniforms(shadows, 512, 1.5f, &uniforms);
    CHECK(uniforms.params[0] == 1.0f / 512.0f && uniforms.params[1] == 1.5f);
    CHECK(uniforms.params[2] == 1.0f);
    CHECK(fabsf(uniforms.info[0][0] - 2.0f * tanf(0.6f)) < 1e-5f);
    CHECK(sizeof(zenapp::ShadowUniforms) == 16 + 192 * 64 + 192 * 16);
    CHECK(sizeof(zenapp::ShadowUniforms) <= 16384);

    zenapp::selectLightShadows(many, viewer, 0, zenapp::kShadowNear, &shadows);
    CHECK(shadows.maps.size() == 0);
    CHECK(shadows.firstMap[nearest] == -1 && shadows.firstMap[spot] == -1);

    zenapp::selectLightShadows(many, viewer, 64, 4.5f, &shadows);
    CHECK(shadows.maps.size() == 1);
    CHECK(shadows.firstMap[spot] == 0);
    CHECK(depth(shadows.maps[0].viewProjection, Math::Vec3(0.0f, 2.5f - 4.5f, 0.0f)) < 1e-3f);

    zenapp::selectLightShadows(many, viewer, 64, zenapp::kShadowNear, &shadows);
    CHECK(shadows.maps.size() == 19);
    if (shadows.firstMap[spot] >= 0)
    {
        const zenapp::ShadowMapView& map = shadows.maps[shadows.firstMap[spot]];
        const Math::Vec3 origin(ps[0], ps[1], ps[2]);
        const Math::Vec4 axis = map.viewProjection * Math::Vec4(origin + Math::Vec3(0, -3, 0), 1);
        CHECK(fabsf(axis.x) < 1e-4f && fabsf(axis.y) < 1e-4f && axis.w > 0.0f);
        unsigned missed = 0;
        for (unsigned s = 0; s < 2000; ++s)
        {
            const float angle = randomRange(0.0f, 0.6f * 0.999f);
            const float turn = randomRange(0.0f, 6.2831853f);
            const float reach = randomRange(0.05f, 5.9f);
            const Math::Vec3 direction(sinf(angle) * cosf(turn), -cosf(angle),
                    sinf(angle) * sinf(turn));
            if (!inside(map.viewProjection, origin + direction * reach, 0.001f)) ++missed;
        }
        CHECK(missed == 0);
        CHECK(!inside(map.viewProjection, origin + Math::Vec3(0.0f, 1.0f, 0.0f), 0.0f));
    }

    {
        zenapp::clearLights(&many);
        const float left[3] = { -6.0f, 0.0f, -10.0f };
        const float ahead[3] = { 0.0f, 0.0f, -10.0f };
        const float close[3] = { 0.0f, 0.0f, -3.0f };
        const float behind[3] = { 0.0f, 0.0f, 12.0f };
        const int lightLeft = zenapp::addPointLight(&many, left, white, 10.0f, 3.0f);
        const int lightAhead = zenapp::addPointLight(&many, ahead, white, 10.0f, 3.0f);
        const int lightClose = zenapp::addPointLight(&many, close, white, 10.0f, 3.0f);
        const int lightBehind = zenapp::addPointLight(&many, behind, white, 10.0f, 3.0f);
        const Math::Mat4 projection = Math::Mat4::Perspective(0.9f, 1.0f, 0.1f, 100.0f);
        const Math::Frustum forward = Math::Frustum::FromViewProjection(
                projection * Math::Mat4::LookAt(Math::Vec3(0, 0, 0), Math::Vec3(0, 0, -1),
                                     Math::Vec3(0, 1, 0)));
        const Math::Frustum backward = Math::Frustum::FromViewProjection(
                projection * Math::Mat4::LookAt(Math::Vec3(0, 0, 0), Math::Vec3(0, 0, 1),
                                     Math::Vec3(0, 1, 0)));

        zenapp::ShadowSlots slots;
        zenapp::clearShadowSlots(&slots, 2);
        CHECK(slots.count == 2);
        CHECK(zenapp::chooseShadowLights(&many, forward, Math::Vec3(0, 0, 0), 0.3f, 0.0f, &slots));
        CHECK(slots.light[0] == lightClose && slots.light[1] == lightAhead);
        CHECK(!slots.drawn[0] && !slots.drawn[1]);
        slots.drawn[0] = slots.drawn[1] = true;
        many.lights[lightClose].spot[3] = 1.0f;
        many.lights[lightAhead].spot[3] = 7.0f;
        CHECK(!zenapp::chooseShadowLights(&many, forward, Math::Vec3(0, 0, 0), 0.3f, 0.0f, &slots));
        CHECK(slots.drawn[0] && slots.drawn[1]);

        CHECK(zenapp::chooseShadowLights(&many, backward, Math::Vec3(0, 0, 0), 0.3f, 0.0f, &slots));
        CHECK(slots.light[0] == lightBehind && !slots.drawn[0]);
        CHECK(slots.light[1] == lightAhead && slots.drawn[1]);
        CHECK(many.lights[lightClose].spot[3] == 0.0f);
        CHECK(many.lights[lightAhead].spot[3] == 7.0f);
        CHECK(slots.light[0] != lightLeft && slots.light[1] != lightLeft);

        zenapp::clearShadowSlots(&slots, 2);
        many.lights[lightClose].spot[3] = 0.0f;
        many.lights[lightAhead].spot[3] = 0.0f;
        CHECK(zenapp::chooseShadowLights(&many, forward, Math::Vec3(0, 0, 0), 0.3f, 5.0f, &slots));
        CHECK(slots.light[0] == lightClose && slots.light[1] == -1);
        many.lights[lightClose].spot[3] = 1.0f;
        slots.drawn[0] = true;

        const Math::Vec3 walked(0.0f, 0.0f, -8.0f);
        CHECK(zenapp::chooseShadowLights(&many, forward, walked, 0.3f, 4.5f, &slots));
        CHECK(slots.light[0] == lightClose && slots.drawn[0]);
        CHECK(slots.light[1] == lightAhead && !slots.drawn[1]);
        CHECK(many.lights[lightClose].spot[3] == 1.0f);
        slots.drawn[1] = true;
        many.lights[lightAhead].spot[3] = 7.0f;

        CHECK(zenapp::chooseShadowLights(&many, forward, walked, 0.3f, 3.0f, &slots));
        CHECK(slots.light[0] == -1 && !slots.drawn[0]);
        CHECK(many.lights[lightClose].spot[3] == 0.0f);
        CHECK(slots.light[1] == lightAhead && slots.drawn[1]);
        CHECK(many.lights[lightAhead].spot[3] == 7.0f);

        CHECK(zenapp::chooseShadowLights(&many, forward, Math::Vec3(0, 0, 0), 0.3f, 0.0f, &slots));
        CHECK(slots.light[0] == lightClose && !slots.drawn[0] && slots.light[1] == lightAhead);

        zenapp::clearShadowSlots(&slots, 1000);
        CHECK(slots.count == zenapp::ShadowSlots::kMaxSlots);
        CHECK(zenapp::ShadowSlots::kMaxSlots == 32);
    }

    if (failures) printf("%d failed\n", failures);
    else
        printf("ok\n");
    return failures ? 1 : 0;
}
