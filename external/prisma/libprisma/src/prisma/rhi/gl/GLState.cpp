#include "prisma/rhi/gl/GLState.h"

#include "prisma/rhi/gl/GL.h"

namespace prisma
{

void GLState::reset()
{
    known_ = 0;
    knownUniformSlots_ = 0;
    knownTextureUnits_ = 0;
    knownSamplerUnits_ = 0;
}

bool GLState::same(std::uint32_t bit, bool& stored, bool value)
{
    if ((known_ & bit) && stored == value) return true;
    stored = value;
    known_ |= bit;
    return false;
}

bool GLState::same(std::uint32_t bit, std::uint32_t& stored, std::uint32_t value)
{
    if ((known_ & bit) && stored == value) return true;
    stored = value;
    known_ |= bit;
    return false;
}

void GLState::bindFramebuffer(std::uint32_t framebuffer)
{
    if (same(kFramebuffer, framebuffer_, framebuffer)) return;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void GLState::viewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    if ((known_ & kViewport) && viewport_[0] == x && viewport_[1] == y && viewport_[2] == width &&
            viewport_[3] == height)
        return;
    glViewport(x, y, width, height);
    viewport_[0] = x;
    viewport_[1] = y;
    viewport_[2] = width;
    viewport_[3] = height;
    known_ |= kViewport;
}

void GLState::depthRange(float minDepth, float maxDepth)
{
    if ((known_ & kDepthRange) && depthRange_[0] == minDepth && depthRange_[1] == maxDepth) return;
    glDepthRangef(minDepth, maxDepth);
    depthRange_[0] = minDepth;
    depthRange_[1] = maxDepth;
    known_ |= kDepthRange;
}

void GLState::scissorTest(bool enabled)
{
    if (same(kScissorTest, scissorTest_, enabled)) return;
    if (enabled) glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
}

void GLState::framebufferSrgb(bool enabled)
{
#ifdef PRISMA_GLES
    (void) enabled;
#else
    if (same(kFramebufferSrgb, framebufferSrgb_, enabled)) return;
    if (enabled) glEnable(GL_FRAMEBUFFER_SRGB);
    else
        glDisable(GL_FRAMEBUFFER_SRGB);
#endif
}

void GLState::scissor(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
{
    if ((known_ & kScissor) && scissor_[0] == x && scissor_[1] == y && scissor_[2] == width &&
            scissor_[3] == height)
        return;
    glScissor(x, y, width, height);
    scissor_[0] = x;
    scissor_[1] = y;
    scissor_[2] = width;
    scissor_[3] = height;
    known_ |= kScissor;
}

void GLState::clearColor(const float color[4])
{
    if ((known_ & kClearColor) && clearColor_[0] == color[0] && clearColor_[1] == color[1] &&
            clearColor_[2] == color[2] && clearColor_[3] == color[3])
        return;
    glClearColor(color[0], color[1], color[2], color[3]);
    for (int i = 0; i < 4; ++i) clearColor_[i] = color[i];
    known_ |= kClearColor;
}

void GLState::clearDepth(float depth)
{
    if ((known_ & kClearDepth) && clearDepth_ == depth) return;
    glClearDepthf(depth);
    clearDepth_ = depth;
    known_ |= kClearDepth;
}

void GLState::useProgram(std::uint32_t program)
{
    if (same(kProgram, program_, program)) return;
    glUseProgram(program);
}

void GLState::bindVertexArray(std::uint32_t vertexArray)
{
    if (same(kVertexArray, vertexArray_, vertexArray)) return;
    glBindVertexArray(vertexArray);
}

void GLState::bindArrayBuffer(std::uint32_t buffer)
{
    if (same(kArrayBuffer, arrayBuffer_, buffer)) return;
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
}

void GLState::bindUniformBuffer(std::uint32_t buffer)
{
    if (same(kUniformBuffer, uniformBuffer_, buffer)) return;
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
}

void GLState::bindUniformBufferRange(std::uint32_t slot, std::uint32_t buffer, std::uint32_t offset,
        std::uint32_t size)
{
    if (slot >= kMaxUniformSlots) return;
    UniformRange& range = uniformSlots_[slot];
    const std::uint32_t bit = 1u << slot;
    if ((knownUniformSlots_ & bit) && range.buffer == buffer && range.offset == offset &&
            range.size == size)
        return;
    glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffer, offset, size);
    range.buffer = buffer;
    range.offset = offset;
    range.size = size;
    knownUniformSlots_ |= bit;
    uniformBuffer_ = buffer;
    known_ |= kUniformBuffer;
}

void GLState::bindTexture(std::uint32_t unit, std::uint32_t target, std::uint32_t texture)
{
    if (unit >= kMaxTextureUnits) return;
    const std::uint32_t bit = 1u << unit;
    if ((knownTextureUnits_ & bit) && textures_[unit] == texture) return;
    activeTexture(unit);
    glBindTexture(target, texture);
    textures_[unit] = texture;
    knownTextureUnits_ |= bit;
}

void GLState::activeTexture(std::uint32_t unit)
{
    if (same(kActiveUnit, activeUnit_, unit)) return;
    glActiveTexture(GL_TEXTURE0 + unit);
}

void GLState::bindSampler(std::uint32_t unit, std::uint32_t sampler)
{
    if (unit >= kMaxTextureUnits) return;
    const std::uint32_t bit = 1u << unit;
    if ((knownSamplerUnits_ & bit) && samplers_[unit] == sampler) return;
    glBindSampler(unit, sampler);
    samplers_[unit] = sampler;
    knownSamplerUnits_ |= bit;
}

void GLState::depthTest(bool enabled)
{
    if (same(kDepthTest, depthTest_, enabled)) return;
    if (enabled) glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
}

void GLState::depthMask(bool enabled)
{
    if (same(kDepthMask, depthMask_, enabled)) return;
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void GLState::depthFunc(std::uint32_t func)
{
    if (same(kDepthFunc, depthFunc_, func)) return;
    glDepthFunc(func);
}

void GLState::cullFace(bool enabled, std::uint32_t face)
{
    if (!same(kCullEnabled, cullEnabled_, enabled))
    {
        if (enabled) glEnable(GL_CULL_FACE);
        else
            glDisable(GL_CULL_FACE);
    }
    if (enabled && !same(kCullFace, cullFace_, face)) glCullFace(face);
}

void GLState::frontFace(std::uint32_t mode)
{
    if (same(kFrontFace, frontFace_, mode)) return;
    glFrontFace(mode);
}

void GLState::blend(bool enabled)
{
    if (same(kBlend, blend_, enabled)) return;
    if (enabled) glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}

void GLState::blendFunc(std::uint32_t srcColor, std::uint32_t dstColor, std::uint32_t srcAlpha,
        std::uint32_t dstAlpha)
{
    if ((known_ & kBlendFunc) && blendFunc_[0] == srcColor && blendFunc_[1] == dstColor &&
            blendFunc_[2] == srcAlpha && blendFunc_[3] == dstAlpha)
        return;
    glBlendFuncSeparate(srcColor, dstColor, srcAlpha, dstAlpha);
    blendFunc_[0] = srcColor;
    blendFunc_[1] = dstColor;
    blendFunc_[2] = srcAlpha;
    blendFunc_[3] = dstAlpha;
    known_ |= kBlendFunc;
}

void GLState::blendEquation(std::uint32_t color, std::uint32_t alpha)
{
    if ((known_ & kBlendEquation) && blendEquation_[0] == color && blendEquation_[1] == alpha)
        return;
    glBlendEquationSeparate(color, alpha);
    blendEquation_[0] = color;
    blendEquation_[1] = alpha;
    known_ |= kBlendEquation;
}

void GLState::colorMask(std::uint8_t mask)
{
    if (same(kColorMask, colorMask_, mask)) return;
    glColorMask((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0);
}

void GLState::stencil(const Stencil& stencil)
{
    bool equal = (known_ & kStencil) && stencil_.enabled == stencil.enabled;
    if (equal && stencil.enabled)
    {
        equal = stencil_.reference == stencil.reference && stencil_.readMask == stencil.readMask &&
                stencil_.writeMask == stencil.writeMask;
        for (int face = 0; equal && face < 2; ++face)
            equal = stencil_.compare[face] == stencil.compare[face] &&
                    stencil_.failOp[face] == stencil.failOp[face] &&
                    stencil_.depthFailOp[face] == stencil.depthFailOp[face] &&
                    stencil_.passOp[face] == stencil.passOp[face];
    }
    if (equal) return;

    if (!(known_ & kStencil) || stencil_.enabled != stencil.enabled)
    {
        if (stencil.enabled) glEnable(GL_STENCIL_TEST);
        else
            glDisable(GL_STENCIL_TEST);
    }
    if (stencil.enabled)
    {
        const GLenum faces[2] = { GL_FRONT, GL_BACK };
        for (int face = 0; face < 2; ++face)
        {
            glStencilFuncSeparate(faces[face], stencil.compare[face],
                    static_cast<GLint>(stencil.reference), stencil.readMask);
            glStencilOpSeparate(faces[face], stencil.failOp[face], stencil.depthFailOp[face],
                    stencil.passOp[face]);
        }
        glStencilMask(stencil.writeMask);
        stencil_ = stencil;
    }
    stencil_.enabled = stencil.enabled;
    known_ |= kStencil;
}

void GLState::stencilWriteMask(std::uint32_t mask)
{
    glStencilMask(mask);
    stencil_.writeMask = mask;
    if (stencil_.enabled) known_ &= ~static_cast<std::uint32_t>(kStencil);
}

void GLState::clearStencil(std::uint32_t value)
{
    if (same(kClearStencil, clearStencil_, value)) return;
    glClearStencil(static_cast<GLint>(value));
}

void GLState::depthBias(float constant, float slope)
{
    if ((known_ & kDepthBias) && depthBias_[0] == constant && depthBias_[1] == slope) return;
    if (constant != 0.0f || slope != 0.0f)
    {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(slope, constant);
    }
    else
        glDisable(GL_POLYGON_OFFSET_FILL);
    depthBias_[0] = constant;
    depthBias_[1] = slope;
    known_ |= kDepthBias;
}

void GLState::invalidateBlend() { known_ &= ~(kBlend | kBlendFunc | kBlendEquation | kColorMask); }

void GLState::alphaToCoverage(bool enabled)
{
    if (same(kAlphaToCoverage, alphaToCoverage_, enabled)) return;
    if (enabled) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

void GLState::wireframe(bool enabled)
{
#ifdef PRISMA_GLES
    (void) enabled;
#else
    if (same(kWireframe, wireframe_, enabled)) return;
    glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
#endif
}

void GLState::programDeleted(std::uint32_t program)
{
    if (program_ == program) known_ &= ~static_cast<std::uint32_t>(kProgram);
}

void GLState::vertexArrayDeleted(std::uint32_t vertexArray)
{
    if (vertexArray_ == vertexArray) known_ &= ~static_cast<std::uint32_t>(kVertexArray);
}

void GLState::bufferDeleted(std::uint32_t buffer)
{
    if (arrayBuffer_ == buffer) known_ &= ~static_cast<std::uint32_t>(kArrayBuffer);
    if (uniformBuffer_ == buffer) known_ &= ~static_cast<std::uint32_t>(kUniformBuffer);
    for (std::uint32_t slot = 0; slot < kMaxUniformSlots; ++slot)
        if (uniformSlots_[slot].buffer == buffer) knownUniformSlots_ &= ~(1u << slot);
}

void GLState::framebufferDeleted(std::uint32_t framebuffer)
{
    if (framebuffer_ == framebuffer) known_ &= ~static_cast<std::uint32_t>(kFramebuffer);
}

void GLState::textureDeleted(std::uint32_t texture)
{
    for (std::uint32_t unit = 0; unit < kMaxTextureUnits; ++unit)
        if (textures_[unit] == texture) knownTextureUnits_ &= ~(1u << unit);
}

void GLState::samplerDeleted(std::uint32_t sampler)
{
    for (std::uint32_t unit = 0; unit < kMaxTextureUnits; ++unit)
        if (samplers_[unit] == sampler) knownSamplerUnits_ &= ~(1u << unit);
}

} // namespace prisma
