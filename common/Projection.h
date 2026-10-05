#pragma once

#include "mathc.h"

namespace zenapp
{

inline Math::Mat4 perspectiveZeroToOne(float fovY, float aspect, float nearPlane, float farPlane)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Perspective(fovY, aspect, nearPlane, farPlane);
}

inline Math::Mat4 orthographicZeroToOne(float left, float right, float bottom, float top,
        float nearPlane, float farPlane)
{
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * Math::Mat4::Ortho(left, right, bottom, top, nearPlane, farPlane);
}

inline Math::Mat4 perspectiveInfiniteZeroToOne(float fovY, float aspect, float nearPlane)
{
    const float epsilon = 1.0f / 65536.0f;
    Math::Mat4 projection = Math::Mat4::Perspective(fovY, aspect, nearPlane, nearPlane * 2.0f);
    projection.col2.z = epsilon - 1.0f;
    projection.col3.z = (epsilon - 2.0f) * nearPlane;
    Math::Mat4 depthZeroToOne = Math::Mat4::Identity();
    depthZeroToOne.col2.z = 0.5f;
    depthZeroToOne.col3.z = 0.5f;
    return depthZeroToOne * projection;
}

} // namespace zenapp
