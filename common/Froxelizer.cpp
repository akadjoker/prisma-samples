#include "Froxelizer.h"

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

struct Vec3
{
    float x, y, z;
};

float clampf(float v, float low, float high)
{
    return v < low ? low : (v > high ? high : v);
}

int clampi(int v, int low, int high)
{
    return v < low ? low : (v > high ? high : v);
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

unsigned popCount(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_popcount(v));
#else
    unsigned count = 0;
    while (v)
    {
        v &= v - 1;
        ++count;
    }
    return count;
#endif
}

unsigned lowestBit(uint32_t v)
{
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_ctz(v));
#else
    unsigned index = 0;
    while (!(v & 1u))
    {
        v >>= 1;
        ++index;
    }
    return index;
#endif
}

unsigned writeLights(const uint32_t* words, uint8_t* out)
{
    unsigned count = 0;
    for (unsigned w = 0; w < 8; ++w)
        for (uint32_t left = words[w]; left; left &= left - 1)
            out[count++] = static_cast<uint8_t>(w * 32u + lowestBit(left));
    return count;
}

unsigned roundTo8(unsigned v)
{
    return (v + 7u) & ~7u;
}

struct Sphere
{
    float x, y, z, w;
};

Sphere spherePlaneIntersection(Sphere s, float px, float py, float pz, float pw)
{
    const float d = s.x * px + s.y * py + s.z * pz + pw;
    const float rr = s.w - d * d;
    s.x -= px * d;
    s.y -= py * d;
    s.z -= pz * d;
    s.w = rr;
    return s;
}

bool sphereConeIntersectionFast(const Sphere& sphere, const Vec3& conePosition,
        const Vec3& coneAxis, float coneSinInverse, float coneCosSquared)
{
    const Vec3 u = { conePosition.x - sphere.w * coneSinInverse * coneAxis.x,
        conePosition.y - sphere.w * coneSinInverse * coneAxis.y,
        conePosition.z - sphere.w * coneSinInverse * coneAxis.z };
    const Vec3 d = { sphere.x - u.x, sphere.y - u.y, sphere.z - u.z };
    const float e = dot(coneAxis, d);
    const float dd = dot(d, d);
    return e * e >= dd * coneCosSquared && e > 0.0f;
}

Vec3 planeIntersection(const float* p0, const float* p1, const float* p2)
{
    const Vec3 n0 = { p0[0], p0[1], p0[2] };
    const Vec3 n1 = { p1[0], p1[1], p1[2] };
    const Vec3 n2 = { p2[0], p2[1], p2[2] };
    const Vec3 c0 = cross(n1, n2);
    const Vec3 c1 = cross(n2, n0);
    const Vec3 c2 = cross(n0, n1);
    const float scale = -1.0f / dot(n0, c0);
    return { (p0[3] * c0.x + p1[3] * c1.x + p2[3] * c2.x) * scale,
        (p0[3] * c0.y + p1[3] * c1.y + p2[3] * c2.y) * scale,
        (p0[3] * c0.z + p1[3] * c1.z + p2[3] * c2.z) * scale };
}

} // namespace

void Froxelizer::setDepthRange(float firstSliceDepth, float lastSliceDistance)
{
    if (firstSliceDepth != firstSliceDepth_ || lastSliceDistance != lastSliceDistance_)
    {
        firstSliceDepth_ = firstSliceDepth;
        lastSliceDistance_ = lastSliceDistance;
        dirty_ = true;
    }
}

void Froxelizer::prepare(unsigned width, unsigned height, const float* projection,
        float nearPlane, float farPlane)
{
    if (!dirty_ && width == width_ && height == height_ && nearPlane == near_ &&
            farPlane == far_ && memcmp(projection, projection_, sizeof(projection_)) == 0)
        return;
    near_ = nearPlane;
    far_ = farPlane;
    rebuild(width, height, projection);
    dirty_ = false;
}

void Froxelizer::rebuild(unsigned width, unsigned height, const float* projection)
{
    width_ = width;
    height_ = height;
    memcpy(projection_, projection, sizeof(projection_));

    float zLightNear = firstSliceDepth_;
    float zLightFar = lastSliceDistance_;
    if (zLightFar == zLightNear)
    {
        zLightNear = near_;
        zLightFar = far_;
    }
    if (zLightFar < zLightNear)
    {
        const float swap = zLightFar;
        zLightFar = zLightNear;
        zLightNear = swap;
    }
    if (zLightNear < near_ || zLightNear >= far_) zLightNear = near_;
    if (zLightFar > far_ || zLightFar <= near_) zLightFar = far_;
    zLightNear = zLightNear < zLightFar ? zLightNear : zLightFar;
    zLightNear_ = zLightNear;
    zLightFar_ = zLightFar;

    const unsigned layoutWidth = width < 16 ? 16 : width;
    const unsigned layoutHeight = height < 16 ? 16 : height;
    const unsigned planeCount = kEntryCount / kSliceCount;
    unsigned countX = static_cast<unsigned>(
            sqrtf(static_cast<float>(planeCount) * layoutWidth / layoutHeight));
    unsigned countY = static_cast<unsigned>(
            sqrtf(static_cast<float>(planeCount) * layoutHeight / layoutWidth));
    countX = countX < 1 ? 1 : countX;
    countY = countY < 1 ? 1 : countY;
    const unsigned sizeX = (layoutWidth + countX - 1) / countX;
    const unsigned sizeY = (layoutHeight + countY - 1) / countY;
    dimension_ = roundTo8(roundTo8(sizeX) >= sizeY ? sizeX : sizeY);
    countX_ = (layoutWidth + dimension_ - 1) / dimension_;
    countY_ = (layoutHeight + dimension_ - 1) / dimension_;
    countZ_ = kSliceCount;

    clipToFroxelX_ = static_cast<float>(width) / static_cast<float>(2 * dimension_);
    clipToFroxelY_ = static_cast<float>(height) / static_cast<float>(2 * dimension_);

    linearizer_ = log2f(zLightFar_ / zLightNear_) / static_cast<float>(countZ_ > 1 ? countZ_ - 1 : 1);
    linearizer_ = linearizer_ < 1e-4f ? 1e-4f : linearizer_;
    distancesZ_.resize(countZ_ + 1);
    distancesZ_[0] = 0.0f;
    for (int i = 1, n = static_cast<int>(countZ_); i <= n; ++i)
        distancesZ_[i] = zLightFar_ * exp2f(static_cast<float>(i - n) * linearizer_);

    const float widthInClip = static_cast<float>(2 * dimension_) / static_cast<float>(width);
    const float heightInClip = static_cast<float>(2 * dimension_) / static_cast<float>(height);
    planesX_.resize(countX_ + 1);
    planesY_.resize(countY_ + 1);
    const float* m = projection_;
    for (unsigned i = 0; i <= countX_; ++i)
    {
        const float x = static_cast<float>(i) * widthInClip - 1.0f;
        const Vec3 p = { -m[0] + x * m[3], -m[4] + x * m[7], -m[8] + x * m[11] };
        const float inverse = 1.0f / sqrtf(dot(p, p));
        planesX_[i] = { p.x * inverse, p.y * inverse, p.z * inverse, 0.0f };
    }
    for (unsigned i = 0; i <= countY_; ++i)
    {
        const float y = static_cast<float>(i) * heightInClip - 1.0f;
        const Vec3 p = { m[1] - y * m[3], m[5] - y * m[7], m[9] - y * m[11] };
        const float inverse = 1.0f / sqrtf(dot(p, p));
        planesY_[i] = { p.x * inverse, p.y * inverse, p.z * inverse, 0.0f };
    }

    boundingSpheres_.resize(countX_ * countY_ * countZ_);
    unsigned fi = 0;
    for (unsigned iz = 0; iz < countZ_; ++iz)
    {
        const float nearZ[4] = { 0.0f, 0.0f, 1.0f, distancesZ_[iz]};
        const float farZ[4] = { 0.0f, 0.0f, -1.0f, -distancesZ_[iz + 1] };
        for (unsigned iy = 0; iy < countY_; ++iy)
        {
            const float bottom[4] = { planesY_[iy].x, planesY_[iy].y, planesY_[iy].z, 0.0f };
            const float top[4] = { -planesY_[iy + 1].x, -planesY_[iy + 1].y, -planesY_[iy + 1].z, 0.0f };
            for (unsigned ix = 0; ix < countX_; ++ix)
            {
                const float left[4] = { planesX_[ix].x, planesX_[ix].y, planesX_[ix].z, 0.0f };
                const float right[4] = { -planesX_[ix + 1].x, -planesX_[ix + 1].y,
                    -planesX_[ix + 1].z, 0.0f };
                const Vec3 corners[8] = { planeIntersection(left, bottom, nearZ),
                    planeIntersection(right, bottom, nearZ), planeIntersection(left, top, nearZ),
                    planeIntersection(right, top, nearZ), planeIntersection(left, bottom, farZ),
                    planeIntersection(right, bottom, farZ), planeIntersection(left, top, farZ),
                    planeIntersection(right, top, farZ) };
                Vec3 center = { 0.0f, 0.0f, 0.0f };
                for (const Vec3& corner: corners)
                {
                    center.x += corner.x * 0.125f;
                    center.y += corner.y * 0.125f;
                    center.z += corner.z * 0.125f;
                }
                float maxDistance = 0.0f;
                for (const Vec3& corner: corners)
                {
                    const Vec3 d = { corner.x - center.x, corner.y - center.y, corner.z - center.z };
                    const float d2 = dot(d, d);
                    maxDistance = d2 > maxDistance ? d2 : maxDistance;
                }
                boundingSpheres_[fi++] = { center.x, center.y, center.z, sqrtf(maxDistance) };
            }
        }
    }

    params_.cellsXY[0] = static_cast<float>(width) / static_cast<float>(dimension_);
    params_.cellsXY[1] = static_cast<float>(height) / static_cast<float>(dimension_);
    params_.countX = countX_;
    params_.countY = countY_;
    params_.countZ = countZ_;
    params_.zLightFar = zLightFar_;
    params_.inverseLinearizer = 1.0f / linearizer_;
}

unsigned Froxelizer::findSliceZ(float viewZ) const
{
    if (!(viewZ < 0.0f)) return 0;
    float s = log2f(-viewZ / zLightFar_) / linearizer_ + static_cast<float>(countZ_);
    s = clampf(s, 0.0f, static_cast<float>(countZ_ - 1));
    return static_cast<unsigned>(s);
}

void Froxelizer::clipToIndices(float clipX, float clipY, unsigned* x, unsigned* y) const
{
    *x = static_cast<unsigned>(clampi(static_cast<int>(clipX * clipToFroxelX_ + clipToFroxelX_), 0,
            static_cast<int>(countX_) - 1));
    *y = static_cast<unsigned>(clampi(static_cast<int>(clipY * clipToFroxelY_ + clipToFroxelY_), 0,
            static_cast<int>(countY_) - 1));
}

void Froxelizer::project(const float* point, float* clip) const
{
    const float* m = projection_;
    const float x = m[0] * point[0] + m[4] * point[1] + m[8] * point[2] + m[12];
    const float y = m[1] * point[0] + m[5] * point[1] + m[9] * point[2] + m[13];
    const float w = m[3] * point[0] + m[7] * point[1] + m[11] * point[2] + m[15];
    clip[0] = x / w;
    clip[1] = y / w;
}

unsigned Froxelizer::froxelIndexFor(float ndcX, float ndcY, float depth) const
{
    const int ix = clampi(static_cast<int>((ndcX * 0.5f + 0.5f) * params_.cellsXY[0]), 0,
            static_cast<int>(countX_) - 1);
    const int iy = clampi(static_cast<int>((ndcY * 0.5f + 0.5f) * params_.cellsXY[1]), 0,
            static_cast<int>(countY_) - 1);
    float slice = log2f(depth / zLightFar_) * params_.inverseLinearizer + static_cast<float>(countZ_);
    slice = clampf(slice, 0.0f, static_cast<float>(countZ_ - 1));
    return static_cast<unsigned>(ix) + static_cast<unsigned>(iy) * countX_ +
           static_cast<unsigned>(slice) * countX_ * countY_;
}

void Froxelizer::froxelizeLight(uint32_t* bits, unsigned light, const float* position,
        const float* axis, float cosSquared, float inverseSin, float radius, bool spot) const
{
    if (position[2] + radius < -zLightFar_) return;

    Sphere s = { position[0], position[1], position[2], radius * radius };

    const float znear = fminf(-near_, position[2] + radius);
    const float zfar = position[2] - radius;
    float pmin[2] = { 1e30f, 1e30f };
    float pmax[2] = { -1e30f, -1e30f };
    for (int corner = 0; corner < 8; ++corner)
    {
        const float point[3] = { position[0] + ((corner & 1) ? radius : -radius),
            position[1] + ((corner & 2) ? radius : -radius), (corner & 4) ? zfar : znear };
        float clip[2];
        project(point, clip);
        pmin[0] = fminf(pmin[0], clip[0]);
        pmin[1] = fminf(pmin[1], clip[1]);
        pmax[0] = fmaxf(pmax[0], clip[0]);
        pmax[1] = fmaxf(pmax[1], clip[1]);
    }

    unsigned x0, y0, x1, y1;
    clipToIndices(pmin[0], pmin[1], &x0, &y0);
    clipToIndices(pmax[0], pmax[1], &x1, &y1);
    const unsigned z0 = findSliceZ(znear);
    const unsigned z1 = findSliceZ(zfar);
    const unsigned zcenter = findSliceZ(s.z);

    const Vec3 coneAxis = spot ? Vec3{ axis[0], axis[1], axis[2] } : Vec3{ 0.0f, 0.0f, 0.0f };
    const Vec3 conePosition = { position[0], position[1], position[2] };

    for (unsigned iz = z0; iz <= z1; ++iz)
    {
        Sphere cz = s;
        if (iz != zcenter)
            cz = spherePlaneIntersection(s, 0.0f, 0.0f, 1.0f,
                    iz < zcenter ? distancesZ_[iz + 1] : distancesZ_[iz]);
        if (!(cz.w > 0.0f)) continue;

        float clip[2];
        const float centerPoint[3] = { cz.x, cz.y, cz.z };
        project(centerPoint, clip);
        unsigned xcenter, ycenter;
        clipToIndices(clip[0], clip[1], &xcenter, &ycenter);

        for (unsigned iy = y0; iy <= y1; ++iy)
        {
            Sphere cy = cz;
            if (iy != ycenter)
            {
                const Float4& plane = iy < ycenter ? planesY_[iy + 1] : planesY_[iy];
                cy = spherePlaneIntersection(cz, plane.x, plane.y, plane.z, plane.w);
            }
            if (!(cy.w > 0.0f)) continue;

            unsigned begin = 0xFFFFFFFFu;
            unsigned end = 0;
            for (unsigned ix = x0; ix < x1 + 1; ++ix)
            {
                if (ix != xcenter)
                {
                    const Float4& plane = ix < xcenter ? planesX_[ix + 1] : planesX_[ix];
                    if (spherePlaneIntersection(cy, plane.x, plane.y, plane.z, plane.w).w > 0.0f)
                    {
                        begin = begin < ix ? begin : ix;
                        end = end > ix ? end : ix;
                    }
                }
                else
                {
                    begin = begin < ix ? begin : ix;
                    end = end > ix ? end : ix;
                }
            }
            if (begin > end) continue;

            for (unsigned ix = begin; ix <= end; ++ix)
            {
                const unsigned fi = ix + iy * countX_ + iz * countX_ * countY_;
                bool hit = true;
                if (spot)
                {
                    const Float4& b = boundingSpheres_[fi];
                    hit = sphereConeIntersectionFast({ b.x, b.y, b.z, b.w }, conePosition, coneAxis,
                            inverseSin, cosSquared);
                }
                if (hit) bits[fi * 8 + (light >> 5)] |= 1u << (light & 31u);
            }
        }
    }
}

void Froxelizer::froxelize(const float* view, const FroxelLight* lights, unsigned count)
{
    count = count > kMaxLights ? kMaxLights : count;
    bits_.resize(static_cast<size_t>(froxelCount()) * 8);
    memset(bits_.data(), 0, bits_.size() * sizeof(uint32_t));

    const float maxInverseSin = 114.59301f;
    const float maxCosSquared = 0.99992385f;
    for (unsigned i = 0; i < count; ++i)
    {
        const FroxelLight& light = lights[i];
        const float position[3] = {
            view[0] * light.position[0] + view[4] * light.position[1] + view[8] * light.position[2] + view[12],
            view[1] * light.position[0] + view[5] * light.position[1] + view[9] * light.position[2] + view[13],
            view[2] * light.position[0] + view[6] * light.position[1] + view[10] * light.position[2] + view[14] };
        float axis[3] = { 0.0f, 0.0f, 0.0f };
        float cosSquared = 0.0f;
        float inverseSin = 0.0f;
        if (light.spot)
        {
            axis[0] = view[0] * light.direction[0] + view[4] * light.direction[1] + view[8] * light.direction[2];
            axis[1] = view[1] * light.direction[0] + view[5] * light.direction[1] + view[9] * light.direction[2];
            axis[2] = view[2] * light.direction[0] + view[6] * light.direction[1] + view[10] * light.direction[2];
            const float halfPi = 1.57079632679f;
            const float minimum = 0.0087266463f;
            const float outer = clampf(fabsf(light.outerAngle), minimum, halfPi);
            const float cosOuter = cosf(outer);
            cosSquared = cosOuter * cosOuter;
            cosSquared = cosSquared < maxCosSquared ? cosSquared : maxCosSquared;
            inverseSin = 1.0f / sinf(outer);
            inverseSin = inverseSin < maxInverseSin ? inverseSin : maxInverseSin;
        }
        froxelizeLight(bits_.data(), i, position, axis, cosSquared, inverseSin, light.radius,
                light.spot);
    }
    assignRecords(bits_.data());
}

void Froxelizer::assignRecords(const uint32_t* bits)
{
    const unsigned froxels = froxelCount();
    uint32_t all[8] = {};
    for (unsigned f = 0; f < froxels; ++f)
        for (unsigned w = 0; w < 8; ++w) all[w] |= bits[f * 8 + w];

    memset(entries_, 0, sizeof(entries_));
    memset(records_, 0, sizeof(records_));
    overflowed_ = false;

    unsigned allCount = 0;
    for (unsigned w = 0; w < 8; ++w) allCount += popCount(all[w]);
    unsigned offset = writeLights(all, records_);

    const auto equal = [&](unsigned a, unsigned b) {
        return memcmp(bits + a * 8, bits + b * 8, 8 * sizeof(uint32_t)) == 0;
    };
    const auto none = [&](unsigned f) {
        for (unsigned w = 0; w < 8; ++w)
            if (bits[f * 8 + w]) return false;
        return true;
    };

    for (unsigned i = 0; i < froxels;)
    {
        unsigned current = i;
        if (none(current))
        {
            entries_[i++] = 0;
            continue;
        }
        unsigned lightsHere = 0;
        for (unsigned w = 0; w < 8; ++w) lightsHere += popCount(bits[current * 8 + w]);

        if (offset + lightsHere >= kRecordCount)
        {
            overflowed_ = true;
            do
            {
                entries_[i] = none(i) ? 0u : (allCount & 0xFFu);
            } while (++i < froxels);
            break;
        }

        const uint32_t entry = (offset << 16) | lightsHere;
        offset += writeLights(bits + current * 8, records_ + offset);

        uint32_t value = entry;
        do
        {
            entries_[i++] = value;
            if (i >= froxels) break;
            if (!equal(i, current) && i >= countX_)
            {
                current = i - countX_;
                value = entries_[i - countX_];
            }
        } while (equal(i, current));
    }
}

} // namespace zenapp
