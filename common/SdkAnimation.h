#pragma once

#include "SdkMesh.h"

#include <math.h>

namespace zenapp
{

#pragma pack(push, 8)

struct SdkAnimationHeader
{
    uint32_t version;
    uint8_t isBigEndian;
    uint32_t frameTransformType;
    uint32_t numFrames;
    uint32_t numAnimationKeys;
    uint32_t animationFps;
    uint64_t animationDataSize;
    uint64_t animationDataOffset;
};

struct SdkAnimationKey
{
    float translation[3];
    float orientation[4];
    float scaling[3];
};

struct SdkAnimationFrame
{
    char name[100];
    uint64_t dataOffset;
};

#pragma pack(pop)

static_assert(sizeof(SdkAnimationHeader) == 40, "SDK animation header size");
static_assert(sizeof(SdkAnimationKey) == 40, "SDK animation key size");
static_assert(sizeof(SdkAnimationFrame) == 112, "SDK animation frame size");

namespace sdkmath
{

inline void identity(float* m)
{
    memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

inline void multiply(const float* a, const float* b, float* out)
{
    float result[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
            result[r * 4 + c] = sum;
        }
    memcpy(out, result, sizeof(result));
}

inline void translation(float x, float y, float z, float* m)
{
    identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

inline void rotation(const float* q, float* m)
{
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    identity(m);
    m[0] = 1.0f - 2.0f * (y * y + z * z);
    m[1] = 2.0f * (x * y + z * w);
    m[2] = 2.0f * (x * z - y * w);
    m[4] = 2.0f * (x * y - z * w);
    m[5] = 1.0f - 2.0f * (x * x + z * z);
    m[6] = 2.0f * (y * z + x * w);
    m[8] = 2.0f * (x * z + y * w);
    m[9] = 2.0f * (y * z - x * w);
    m[10] = 1.0f - 2.0f * (x * x + y * y);
}

inline void normalizeQuaternion(const float* q, float* out)
{
    float length = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (length < 1e-8f)
    {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    for (int i = 0; i < 4; ++i) out[i] = q[i] / length;
}

inline void invertQuaternion(const float* q, float* out)
{
    const float lengthSquared = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    const float scale = lengthSquared > 1e-12f ? 1.0f / lengthSquared : 0.0f;
    out[0] = -q[0] * scale;
    out[1] = -q[1] * scale;
    out[2] = -q[2] * scale;
    out[3] = q[3] * scale;
}

inline bool invert(const float* m, float* out)
{
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(determinant) < 1e-12f)
    {
        identity(out);
        return false;
    }
    const float scale = 1.0f / determinant;
    for (int i = 0; i < 16; ++i) out[i] = inv[i] * scale;
    return true;
}

} // namespace sdkmath

class SdkAnimation
{
public:
    bool load(const char* path)
    {
        if (!readFile(path, &file_) || file_.size() < sizeof(SdkAnimationHeader)) return false;
        memcpy(&header_, file_.data(), sizeof(SdkAnimationHeader));
        if (header_.numAnimationKeys < 2 || header_.animationFps == 0 || header_.numFrames == 0)
            return false;
        const uint64_t frameBytes =
                static_cast<uint64_t>(header_.numFrames) * sizeof(SdkAnimationFrame);
        if (header_.animationDataOffset > file_.size() ||
                frameBytes > file_.size() - header_.animationDataOffset)
            return false;
        frames_.resize(header_.numFrames);
        memcpy(frames_.data(), file_.data() + header_.animationDataOffset,
                static_cast<size_t>(frameBytes));
        const uint64_t keyBytes =
                static_cast<uint64_t>(header_.numAnimationKeys) * sizeof(SdkAnimationKey);
        for (size_t i = 0; i < frames_.size(); ++i)
        {
            const uint64_t start = frames_[i].dataOffset + sizeof(SdkAnimationHeader);
            if (start > file_.size() || keyBytes > file_.size() - start) return false;
        }
        return true;
    }

    bool bind(const SdkMeshData& mesh)
    {
        const size_t count = mesh.frames.size();
        if (count == 0) return false;
        animationOfFrame_.resize(count);
        for (size_t f = 0; f < count; ++f)
        {
            animationOfFrame_[f] = 0xFFFFFFFFu;
            for (size_t a = 0; a < frames_.size(); ++a)
                if (strncmp(mesh.frames[f].name, frames_[a].name, 100) == 0)
                    animationOfFrame_[f] = static_cast<uint32_t>(a);
        }
        bind_.resize(count * 16);
        inverseBind_.resize(count * 16);
        world_.resize(count * 16);
        final_.resize(count * 16);
        float root[16];
        sdkmath::identity(root);
        bindFrame(mesh, 0, root);
        for (size_t f = 0; f < count; ++f) sdkmath::invert(&bind_[f * 16], &inverseBind_[f * 16]);
        return true;
    }

    void evaluate(const SdkMeshData& mesh, double seconds)
    {
        const uint32_t tick = tickFromTime(seconds);
        if (header_.frameTransformType == 0)
        {
            float root[16];
            sdkmath::identity(root);
            transformFrame(mesh, 0, root, tick);
            for (size_t f = 0; f < mesh.frames.size(); ++f)
                sdkmath::multiply(&inverseBind_[f * 16], &world_[f * 16], &final_[f * 16]);
        }
        else
        {
            for (size_t f = 0; f < mesh.frames.size(); ++f)
                absoluteFrame(static_cast<uint32_t>(f), animationOfFrame_[f], tick);
        }
    }

    unsigned influenceCount(const SdkMeshData& mesh, unsigned meshIndex) const
    {
        return mesh.meshes[meshIndex].numFrameInfluences;
    }

    void influenceMatrices(const SdkMeshData& mesh, unsigned meshIndex, float* out) const
    {
        const unsigned count = influenceCount(mesh, meshIndex);
        const uint32_t first = mesh.meshFirstInfluence[meshIndex];
        for (unsigned i = 0; i < count; ++i)
            memcpy(out + i * 16, &final_[mesh.influenceIds[first + i] * 16], sizeof(float) * 16);
    }

    const float* frameMatrix(unsigned frame) const { return &final_[frame * 16]; }
    const float* worldMatrix(unsigned frame) const { return &world_[frame * 16]; }
    unsigned keyCount() const { return header_.numAnimationKeys; }
    unsigned framesPerSecond() const { return header_.animationFps; }
    float duration() const
    {
        return static_cast<float>(header_.numAnimationKeys - 1) /
               static_cast<float>(header_.animationFps);
    }

private:
    uint32_t tickFromTime(double seconds) const
    {
        const uint32_t tick = static_cast<uint32_t>(header_.animationFps * seconds);
        return tick % (header_.numAnimationKeys - 1) + 1;
    }

    const SdkAnimationKey* key(uint32_t animation, uint32_t tick) const
    {
        return reinterpret_cast<const SdkAnimationKey*>(
                       file_.data() + frames_[animation].dataOffset + sizeof(SdkAnimationHeader)) +
               tick;
    }

    void bindFrame(const SdkMeshData& mesh, uint32_t frame, const float* parent)
    {
        float world[16];
        sdkmath::multiply(mesh.frames[frame].matrix, parent, world);
        memcpy(&bind_[frame * 16], world, sizeof(world));
        if (mesh.frames[frame].siblingFrame != 0xFFFFFFFFu)
            bindFrame(mesh, mesh.frames[frame].siblingFrame, parent);
        if (mesh.frames[frame].childFrame != 0xFFFFFFFFu)
            bindFrame(mesh, mesh.frames[frame].childFrame, world);
    }

    void transformFrame(const SdkMeshData& mesh, uint32_t frame, const float* parent, uint32_t tick)
    {
        float local[16];
        const uint32_t animation = animationOfFrame_[frame];
        if (animation != 0xFFFFFFFFu)
        {
            const SdkAnimationKey* data = key(animation, tick);
            float rotation[16];
            float shift[16];
            float orientation[4];
            sdkmath::normalizeQuaternion(data->orientation, orientation);
            sdkmath::rotation(orientation, rotation);
            sdkmath::translation(data->translation[0], data->translation[1], data->translation[2],
                    shift);
            sdkmath::multiply(rotation, shift, local);
        }
        else
            memcpy(local, mesh.frames[frame].matrix, sizeof(local));

        float world[16];
        sdkmath::multiply(local, parent, world);
        memcpy(&world_[frame * 16], world, sizeof(world));
        if (mesh.frames[frame].siblingFrame != 0xFFFFFFFFu)
            transformFrame(mesh, mesh.frames[frame].siblingFrame, parent, tick);
        if (mesh.frames[frame].childFrame != 0xFFFFFFFFu)
            transformFrame(mesh, mesh.frames[frame].childFrame, world, tick);
    }

    void absoluteFrame(uint32_t frame, uint32_t animation, uint32_t tick)
    {
        if (animation == 0xFFFFFFFFu)
        {
            sdkmath::identity(&final_[frame * 16]);
            return;
        }
        const SdkAnimationKey* data = key(animation, tick);
        const SdkAnimationKey* first = key(animation, 0);
        float back[16];
        float inverseRotation[16];
        float inverseQuaternion[4];
        float toOrigin[16];
        sdkmath::translation(-first->translation[0], -first->translation[1], -first->translation[2],
                back);
        sdkmath::invertQuaternion(first->orientation, inverseQuaternion);
        sdkmath::rotation(inverseQuaternion, inverseRotation);
        sdkmath::multiply(back, inverseRotation, toOrigin);

        float rotation[16];
        float shift[16];
        float fromOrigin[16];
        sdkmath::rotation(data->orientation, rotation);
        sdkmath::translation(data->translation[0], data->translation[1], data->translation[2],
                shift);
        sdkmath::multiply(rotation, shift, fromOrigin);
        sdkmath::multiply(toOrigin, fromOrigin, &final_[frame * 16]);
    }

    ct::Vector<unsigned char> file_;
    SdkAnimationHeader header_ = {};
    ct::Vector<SdkAnimationFrame> frames_;
    ct::Vector<uint32_t> animationOfFrame_;
    ct::Vector<float> bind_;
    ct::Vector<float> inverseBind_;
    ct::Vector<float> world_;
    ct::Vector<float> final_;
};

} // namespace zenapp
