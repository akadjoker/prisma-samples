#pragma once

#include <ct/vector.hpp>

#include <stdint.h>

namespace zenapp
{

struct FroxelLight
{
    float position[3];
    float radius;
    float direction[3];
    float outerAngle;
    bool spot;
};

struct FroxelParams
{
    float cellsXY[2] = { 1.0f, 1.0f };
    unsigned countX = 1;
    unsigned countY = 1;
    unsigned countZ = 1;
    float zLightFar = 1.0f;
    float inverseLinearizer = 1.0f;
};

class Froxelizer
{
public:
    enum
    {
        kMaxLights = 255,
        kEntryCount = 4096,
        kRecordCount = 16384,
        kSliceCount = 16
    };

    void setDepthRange(float firstSliceDepth, float lastSliceDistance);
    void prepare(unsigned width, unsigned height, const float* projection, float nearPlane,
            float farPlane);
    void froxelize(const float* view, const FroxelLight* lights, unsigned count);

    const FroxelParams& params() const { return params_; }
    const uint32_t* entries() const { return entries_; }
    const uint8_t* records() const { return records_; }
    unsigned froxelCount() const { return countX_ * countY_ * countZ_; }
    bool recordsOverflowed() const { return overflowed_; }
    unsigned froxelIndexFor(float ndcX, float ndcY, float depth) const;

private:
    struct Float4
    {
        float x, y, z, w;
    };

    void rebuild(unsigned width, unsigned height, const float* projection);
    void froxelizeLight(uint32_t* bits, unsigned light, const float* position, const float* axis,
            float cosSquared, float inverseSin, float radius, bool spot) const;
    void assignRecords(const uint32_t* bits);
    unsigned findSliceZ(float viewZ) const;
    void clipToIndices(float clipX, float clipY, unsigned* x, unsigned* y) const;
    void project(const float* point, float* clip) const;

    float firstSliceDepth_ = 5.0f;
    float lastSliceDistance_ = 100.0f;
    float near_ = 0.1f;
    float far_ = 100.0f;
    float zLightNear_ = 5.0f;
    float zLightFar_ = 100.0f;
    float projection_[16] = {};
    unsigned width_ = 0;
    unsigned height_ = 0;
    bool dirty_ = true;
    bool overflowed_ = false;

    unsigned dimension_ = 8;
    unsigned countX_ = 1;
    unsigned countY_ = 1;
    unsigned countZ_ = kSliceCount;
    float clipToFroxelX_ = 0.0f;
    float clipToFroxelY_ = 0.0f;
    float linearizer_ = 1.0f;
    FroxelParams params_;

    ct::Vector<float> distancesZ_;
    ct::Vector<Float4> planesX_;
    ct::Vector<Float4> planesY_;
    ct::Vector<Float4> boundingSpheres_;
    ct::Vector<uint32_t> bits_;

    uint32_t entries_[kEntryCount] = {};
    uint8_t records_[kRecordCount] = {};
};

} // namespace zenapp
