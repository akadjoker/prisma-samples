#pragma once

#include "prisma/rhi/Caps.h"
#include "prisma/rhi/Types.h"

namespace prisma
{

struct ShaderBlob
{
    ShaderStage stage;
    const std::uint32_t* spirv;
    std::uint32_t spirvSize;
    const char* glsl;
    const char* essl300;
    const char* essl310;
    const char* essl320;
    const char* essl320Inner;
    const ShaderBinding* bindings;
    std::uint32_t bindingCount;
};

inline ShaderDesc shaderDesc(const ShaderBlob& blob, const Caps& caps)
{
    ShaderDesc desc;
    desc.stage = blob.stage;
    desc.spirv = blob.spirv;
    desc.spirvSize = blob.spirvSize;
    desc.bindings = blob.bindings;
    desc.bindingCount = blob.bindingCount;
    if (!caps.gles) desc.source = blob.glsl;
    else if (caps.versionMajor > 3 || caps.versionMinor >= 2)
    {
        desc.source = blob.essl320;
        desc.innerSource = blob.essl320Inner;
    }
    else if (caps.versionMinor == 1)
        desc.source = blob.essl310;
    else
        desc.source = blob.essl300;
    return desc;
}

} // namespace prisma
