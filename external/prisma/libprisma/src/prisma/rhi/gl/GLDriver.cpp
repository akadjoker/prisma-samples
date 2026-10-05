#include "prisma/rhi/Driver.h"
#include "prisma/rhi/Format.h"
#include "prisma/rhi/HandleCast.h"
#include "prisma/rhi/gl/GL.h"
#include "prisma/rhi/gl/GLState.h"

#include <ct/vector.hpp>

#include <stdio.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
// WebGL 2 reads a buffer back with getBufferSubData; the ES 3 headers do not declare it
extern "C" void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
#endif

namespace prisma
{

namespace
{

struct GLBuffer
{
    enum
    {
        kVersions = 3
    };

    GLuint id = 0;
    std::uint32_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    IndexFormat indexFormat = IndexFormat::UInt16;
    bool stream = false;
    std::uint32_t current = 0;
    std::uint64_t updatedFrame = ~0ull;
    GLuint versions[kVersions] = {};
};

struct GLBinding
{
    BindingKind kind = BindingKind::UniformBlock;
    std::uint32_t slot = 0;
    char name[48] = {};
};

struct GLShader
{
    GLuint id = 0;
    GLuint innerId = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::uint32_t bindingCount = 0;
    GLBinding bindings[ShaderDesc::kMaxBindings];
};

struct GLTexture
{
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D;
    TextureType type = TextureType::Texture2D;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t samples = 1;
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t usage = 0;
};

struct GLAttachment
{
    std::uint32_t texture = 0;
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
};

struct GLFramebuffer
{
    GLuint id = 0;
    GLAttachment colors[RenderPassDesc::kMaxColorTargets];
    std::uint32_t colorCount = 0;
    GLAttachment depth;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool stencil = false;
    TargetFormats formats;
};

const GLenum kStorageBufferOffsetAlignment = 0x90DF;
const GLenum kTextureMaxAnisotropy = 0x84FE;
const GLenum kMaxTextureMaxAnisotropy = 0x84FF;

bool sameAttachment(const GLAttachment& attachment, const RenderTarget& target)
{
    return attachment.texture == target.texture.bits() && attachment.mip == target.mip &&
           attachment.layer == target.layer;
}

std::uint32_t mipSize(std::uint32_t size, std::uint32_t mip)
{
    const std::uint32_t reduced = size >> mip;
    return reduced > 0 ? reduced : 1;
}

GLenum toGLTarget(TextureType type)
{
    switch (type)
    {
        case TextureType::Texture2D:
            return GL_TEXTURE_2D;
        case TextureType::Texture2DArray:
            return GL_TEXTURE_2D_ARRAY;
        case TextureType::TextureCube:
            return GL_TEXTURE_CUBE_MAP;
        case TextureType::Texture3D:
            return GL_TEXTURE_3D;
        case TextureType::TextureCubeArray:
            return GL_TEXTURE_CUBE_MAP_ARRAY;
    }
    return GL_TEXTURE_2D;
}

std::uint32_t layerCount(const GLTexture& texture, std::uint32_t mip)
{
    switch (texture.type)
    {
        case TextureType::Texture2D:
            return 1;
        case TextureType::Texture2DArray:
            return texture.depth;
        case TextureType::TextureCube:
            return 6;
        case TextureType::Texture3D:
            return mipSize(texture.depth, mip);
        case TextureType::TextureCubeArray:
            return texture.depth * 6;
    }
    return 1;
}

static_assert(static_cast<std::uint32_t>(TargetFormats::kMaxColors) ==
                      static_cast<std::uint32_t>(RenderPassDesc::kMaxColorTargets),
        "pipeline and render pass must agree on the colour target count");

bool sameTargets(const TargetFormats& a, const TargetFormats& b)
{
    if (a.window != b.window) return false;
    if (a.window) return true;
    if (a.colorCount != b.colorCount || a.depth != b.depth || a.samples != b.samples) return false;
    for (std::uint32_t i = 0; i < a.colorCount; ++i)
        if (a.colors[i] != b.colors[i]) return false;
    return true;
}

bool isCube(TextureType type)
{
    return type == TextureType::TextureCube || type == TextureType::TextureCubeArray;
}

bool validSamples(const TextureDesc& desc, std::uint32_t maxSamples)
{
    if (desc.samples == 1) return true;
    return (desc.samples == 2 || desc.samples == 4 || desc.samples == 8) &&
           desc.samples <= maxSamples && desc.type == TextureType::Texture2D &&
           desc.mipLevels == 1 && desc.usage == kTextureRenderTarget && !desc.data;
}

struct GLSampler
{
    GLuint id = 0;
};

struct GLFormat
{
    GLenum internal;
    GLenum format;
    GLenum type;
};

GLFormat toGLFormat(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::None:
            break;
        case TextureFormat::R8:
            return { GL_R8, GL_RED, GL_UNSIGNED_BYTE };
        case TextureFormat::RG8:
            return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8:
            return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case TextureFormat::RGBA8Srgb:
            return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE };
        case TextureFormat::RGB10A2:
            return { GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV };
        case TextureFormat::RGBA16F:
            return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT };
        case TextureFormat::R11G11B10F:
            return { GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV };
        case TextureFormat::R16F:
            return { GL_R16F, GL_RED, GL_HALF_FLOAT };
        case TextureFormat::RG16F:
            return { GL_RG16F, GL_RG, GL_HALF_FLOAT };
        case TextureFormat::R32F:
            return { GL_R32F, GL_RED, GL_FLOAT };
        case TextureFormat::RG32F:
            return { GL_RG32F, GL_RG, GL_FLOAT };
        case TextureFormat::RGBA32F:
            return { GL_RGBA32F, GL_RGBA, GL_FLOAT };
        case TextureFormat::R32UInt:
            return { GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT };
        case TextureFormat::Depth32F:
            return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT };
        case TextureFormat::Depth24Stencil8:
            return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8 };
        case TextureFormat::BC1:
            return { 0x83F1, 0, 0 };
        case TextureFormat::BC1Srgb:
            return { 0x8C4D, 0, 0 };
        case TextureFormat::BC2:
            return { 0x83F2, 0, 0 };
        case TextureFormat::BC2Srgb:
            return { 0x8C4E, 0, 0 };
        case TextureFormat::BC3:
            return { 0x83F3, 0, 0 };
        case TextureFormat::BC3Srgb:
            return { 0x8C4F, 0, 0 };
        case TextureFormat::BC4:
            return { 0x8DBB, 0, 0 };
        case TextureFormat::BC5:
            return { 0x8DBD, 0, 0 };
        case TextureFormat::BC6H:
            return { 0x8E8F, 0, 0 };
        case TextureFormat::BC7:
            return { 0x8E8C, 0, 0 };
        case TextureFormat::BC7Srgb:
            return { 0x8E8D, 0, 0 };
        case TextureFormat::ETC2RGB8:
            return { 0x9274, 0, 0 };
        case TextureFormat::ETC2RGB8Srgb:
            return { 0x9275, 0, 0 };
        case TextureFormat::ETC2RGBA8:
            return { 0x9278, 0, 0 };
        case TextureFormat::ETC2RGBA8Srgb:
            return { 0x9279, 0, 0 };
        case TextureFormat::EACR11:
            return { 0x9270, 0, 0 };
        case TextureFormat::EACRG11:
            return { 0x9272, 0, 0 };
        case TextureFormat::ASTC4x4:
            return { 0x93B0, 0, 0 };
        case TextureFormat::ASTC4x4Srgb:
            return { 0x93D0, 0, 0 };
        case TextureFormat::ASTC6x6:
            return { 0x93B4, 0, 0 };
        case TextureFormat::ASTC6x6Srgb:
            return { 0x93D4, 0, 0 };
        case TextureFormat::ASTC8x8:
            return { 0x93B7, 0, 0 };
        case TextureFormat::ASTC8x8Srgb:
            return { 0x93D7, 0, 0 };
    }
    return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE };
}

GLint toGLMinFilter(Filter filter, MipFilter mip)
{
    const bool linear = filter == Filter::Linear;
    switch (mip)
    {
        case MipFilter::None:
            return linear ? GL_LINEAR : GL_NEAREST;
        case MipFilter::Nearest:
            return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        case MipFilter::Linear:
            return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    }
    return GL_LINEAR;
}

GLint toGLAddress(AddressMode mode)
{
    switch (mode)
    {
        case AddressMode::Repeat:
            return GL_REPEAT;
        case AddressMode::MirroredRepeat:
            return GL_MIRRORED_REPEAT;
        case AddressMode::ClampToEdge:
            return GL_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height)
{
    std::uint32_t size = width > height ? width : height;
    std::uint32_t levels = 1;
    while (size > 1)
    {
        size >>= 1;
        ++levels;
    }
    return levels;
}

const std::uint32_t kQuerySlots = 4;
const GLenum kTimestamp = 0x8E28;

struct GLSwapchain
{
    GLPlatform platform;
    bool drawn = false;
};

struct GLReadback
{
    GLuint buffer = 0;
    GLsync sync = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bytes = 0;
};

struct GLQuery
{
    QueryType type = QueryType::Occlusion;
    GLuint ids[kQuerySlots][2] = {};
    std::uint64_t sequence[kQuerySlots] = {};
    std::uint32_t next = 0;
    std::int32_t active = -1;
};

struct GLBlendTarget
{
    bool blend = false;
    GLenum func[4] = { GL_ONE, GL_ZERO, GL_ONE, GL_ZERO };
    GLenum equation[2] = { GL_FUNC_ADD, GL_FUNC_ADD };
    std::uint8_t mask = kColorAll;
};

struct GLPipeline
{
    GLuint program = 0;
    GLuint vertexArray = 0;
    bool compute = false;
    bool vertexBinding = false;
    GLint patchVertices = 0;
    GLenum topology = GL_TRIANGLES;
    VertexBufferLayout vertexBuffers[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexBufferCount = 0;
    std::uint32_t attributeCount = 0;
    VertexAttribute attributes[PipelineDesc::kMaxAttributes];

    bool depthTest = false;
    bool depthWrite = true;
    GLenum depthFunc = GL_LESS;
    bool cull = false;
    GLenum cullFace = GL_BACK;
    GLenum frontFace = GL_CCW;
    bool independentBlend = false;
    GLBlendTarget targetBlend[TargetFormats::kMaxColors];
    bool blend = false;
    GLenum blendFunc[4] = { GL_ONE, GL_ZERO, GL_ONE, GL_ZERO };
    GLenum blendEquation[2] = { GL_FUNC_ADD, GL_FUNC_ADD };
    std::uint8_t colorMask = kColorAll;
    GLState::Stencil stencil;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    bool wireframe = false;
    bool alphaToCoverage = false;
    TargetFormats targets;
};

struct GLVertexFormat
{
    GLint components;
    GLenum type;
    GLboolean normalized;
    bool integer;
};

GLVertexFormat toGLVertexFormat(VertexFormat format)
{
    switch (format)
    {
        case VertexFormat::Float1:
            return { 1, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float2:
            return { 2, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float3:
            return { 3, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Float4:
            return { 4, GL_FLOAT, GL_FALSE, false };
        case VertexFormat::Half2:
            return { 2, GL_HALF_FLOAT, GL_FALSE, false };
        case VertexFormat::Half4:
            return { 4, GL_HALF_FLOAT, GL_FALSE, false };
        case VertexFormat::UByte4Norm:
            return { 4, GL_UNSIGNED_BYTE, GL_TRUE, false };
        case VertexFormat::Byte4Norm:
            return { 4, GL_BYTE, GL_TRUE, false };
        case VertexFormat::UShort2Norm:
            return { 2, GL_UNSIGNED_SHORT, GL_TRUE, false };
        case VertexFormat::UShort4Norm:
            return { 4, GL_UNSIGNED_SHORT, GL_TRUE, false };
        case VertexFormat::Short2Norm:
            return { 2, GL_SHORT, GL_TRUE, false };
        case VertexFormat::Short4Norm:
            return { 4, GL_SHORT, GL_TRUE, false };
        case VertexFormat::Int1010102Norm:
            return { 4, GL_INT_2_10_10_10_REV, GL_TRUE, false };
        case VertexFormat::UByte4:
            return { 4, GL_UNSIGNED_BYTE, GL_FALSE, true };
        case VertexFormat::UShort4:
            return { 4, GL_UNSIGNED_SHORT, GL_FALSE, true };
    }
    return { 4, GL_FLOAT, GL_FALSE, false };
}

GLenum toGLTopology(Topology topology)
{
    switch (topology)
    {
        case Topology::Triangles:
            return GL_TRIANGLES;
        case Topology::TriangleStrip:
            return GL_TRIANGLE_STRIP;
        case Topology::Lines:
            return GL_LINES;
        case Topology::LineStrip:
            return GL_LINE_STRIP;
        case Topology::Points:
            return GL_POINTS;
        case Topology::LinesAdjacency:
            return GL_LINES_ADJACENCY;
        case Topology::LineStripAdjacency:
            return GL_LINE_STRIP_ADJACENCY;
        case Topology::TrianglesAdjacency:
            return GL_TRIANGLES_ADJACENCY;
        case Topology::TriangleStripAdjacency:
            return GL_TRIANGLE_STRIP_ADJACENCY;
        case Topology::Patches:
            return GL_PATCHES;
    }
    return GL_TRIANGLES;
}

GLenum toGLUpdate(BufferUpdate update)
{
    switch (update)
    {
        case BufferUpdate::Static:
            return GL_STATIC_DRAW;
        case BufferUpdate::Dynamic:
            return GL_DYNAMIC_DRAW;
        case BufferUpdate::Stream:
            return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

GLenum toGLStencilOp(StencilOp op)
{
    switch (op)
    {
        case StencilOp::Keep:
            return GL_KEEP;
        case StencilOp::Zero:
            return GL_ZERO;
        case StencilOp::Replace:
            return GL_REPLACE;
        case StencilOp::IncrementClamp:
            return GL_INCR;
        case StencilOp::DecrementClamp:
            return GL_DECR;
        case StencilOp::Invert:
            return GL_INVERT;
        case StencilOp::IncrementWrap:
            return GL_INCR_WRAP;
        case StencilOp::DecrementWrap:
            return GL_DECR_WRAP;
    }
    return GL_KEEP;
}

GLenum toGLBlendOp(BlendOp op)
{
    switch (op)
    {
        case BlendOp::Add:
            return GL_FUNC_ADD;
        case BlendOp::Subtract:
            return GL_FUNC_SUBTRACT;
        case BlendOp::ReverseSubtract:
            return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:
            return GL_MIN;
        case BlendOp::Max:
            return GL_MAX;
    }
    return GL_FUNC_ADD;
}

GLenum toGLCompare(CompareOp op)
{
    switch (op)
    {
        case CompareOp::Never:
            return GL_NEVER;
        case CompareOp::Less:
            return GL_LESS;
        case CompareOp::Equal:
            return GL_EQUAL;
        case CompareOp::LessEqual:
            return GL_LEQUAL;
        case CompareOp::Greater:
            return GL_GREATER;
        case CompareOp::NotEqual:
            return GL_NOTEQUAL;
        case CompareOp::GreaterEqual:
            return GL_GEQUAL;
        case CompareOp::Always:
            return GL_ALWAYS;
    }
    return GL_LESS;
}

GLenum toGLBlendFactor(BlendFactor factor)
{
    switch (factor)
    {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcColor:
            return GL_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor:
            return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return GL_DST_COLOR;
        case BlendFactor::OneMinusDstColor:
            return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:
            return GL_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha:
            return GL_ONE_MINUS_DST_ALPHA;
    }
    return GL_ONE;
}

class GLDriver final : public Driver
{
public:
    GLDriver(const GLPlatform& platform, void (*log)(const char*), bool debug, int major, int minor)
            : platform_(platform),
              log_(log),
              debug_(debug && glDebugMessageCallback != nullptr)
    {
        if (debug_)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(&GLDriver::debugMessage, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0,
                    nullptr, GL_FALSE);
        }

        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        caps_.maxTextureSize = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS, &value);
        caps_.maxColorTargets = static_cast<std::uint32_t>(value);
        glGetIntegerv(GL_MAX_SAMPLES, &value);
        caps_.maxSamples = value >= 8 ? 8 : value >= 4 ? 4 : value >= 2 ? 2 : 1;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
        caps_.uniformBufferOffsetAlignment = static_cast<std::uint32_t>(value);
#ifdef PRISMA_GLES
        const bool anisotropy = glESExt::EXT_texture_filter_anisotropic;
#else
        const bool anisotropy = true;
#endif
        if (anisotropy) glGetFloatv(kMaxTextureMaxAnisotropy, &caps_.maxAnisotropy);
        if (caps_.maxAnisotropy < 1.0f) caps_.maxAnisotropy = 1.0f;
#ifdef PRISMA_GLES
        caps_.gles = true;
        caps_.compute = major > 3 || (major == 3 && minor >= 1);
#else
        caps_.compute = true;
#endif
        caps_.versionMajor = static_cast<std::uint32_t>(major);
        caps_.versionMinor = static_cast<std::uint32_t>(minor);
        caps_.debugOutput = debug_;
        caps_.occlusionQueries = true;
#ifdef PRISMA_GLES
        caps_.textureBC = (hasExtension("EXT_texture_compression_s3tc") ||
                                  hasExtension("WEBGL_compressed_texture_s3tc")) &&
                          (hasExtension("EXT_texture_compression_s3tc_srgb") ||
                                  hasExtension("WEBGL_compressed_texture_s3tc_srgb")) &&
                          hasExtension("EXT_texture_compression_rgtc") &&
                          hasExtension("EXT_texture_compression_bptc");
#ifdef __EMSCRIPTEN__
        caps_.textureETC2 = hasExtension("WEBGL_compressed_texture_etc");
#else
        caps_.textureETC2 = true;
#endif
        caps_.textureASTC = major > 3 || (major == 3 && minor >= 2) ||
                            hasExtension("KHR_texture_compression_astc_ldr") ||
                            hasExtension("WEBGL_compressed_texture_astc");
#else
        caps_.textureBC = hasExtension("EXT_texture_compression_s3tc") &&
                          (hasExtension("EXT_texture_sRGB") ||
                                  hasExtension("EXT_texture_compression_s3tc_srgb"));
        caps_.textureETC2 = true;
        caps_.textureASTC = hasExtension("KHR_texture_compression_astc_ldr");
#endif
#ifdef PRISMA_GLES
        caps_.cubeArrays = major > 3 || (major == 3 && minor >= 2);
        copyImage_ = caps_.cubeArrays && glCopyImageSubData != nullptr;
#else
        caps_.cubeArrays = true;
        copyImage_ = true;
#endif
        caps_.compressedTextureCopy = copyImage_;
        caps_.indirectDraw = caps_.compute;
#ifdef PRISMA_GLES
        caps_.independentBlend = glBlendFuncSeparatei != nullptr && glColorMaski != nullptr &&
                                 (major > 3 || (major == 3 && minor >= 2));
#else
        caps_.independentBlend = true;
#endif
#ifdef PRISMA_GLES
        caps_.geometryShaders = major > 3 || (major == 3 && minor >= 2);
        caps_.tessellation = caps_.geometryShaders && glPatchParameteri != nullptr;
#else
        caps_.geometryShaders = true;
        caps_.tessellation = true;
#endif
        if (caps_.tessellation)
        {
            glGetIntegerv(GL_MAX_PATCH_VERTICES, &value);
            caps_.maxPatchControlPoints = static_cast<std::uint32_t>(value);
        }
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
        caps_.multipleWindows = true;
#endif
        if (caps_.compute)
        {
            GLint vertexBlocks = 0;
            GLint fragmentBlocks = 0;
            glGetIntegerv(GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS, &vertexBlocks);
            glGetIntegerv(GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS, &fragmentBlocks);
            caps_.storageBuffersInGraphics =
                    vertexBlocks >= static_cast<GLint>(PipelineDesc::kMaxStorageBuffers) &&
                    fragmentBlocks >= static_cast<GLint>(PipelineDesc::kMaxStorageBuffers);
            caps_.storageWritesInGraphics = caps_.storageBuffersInGraphics;
            glGetIntegerv(kStorageBufferOffsetAlignment, &value);
            caps_.storageBufferOffsetAlignment = static_cast<std::uint32_t>(value);
        }
#ifdef PRISMA_GLES
        caps_.timerQueries = glESExt::EXT_disjoint_timer_query;
#else
        caps_.timerQueries = true;
#endif
#ifndef PRISMA_GLES
        caps_.wireframe = true;
#endif
#ifdef PRISMA_GLES
        caps_.floatColorTargets = glESExt::EXT_color_buffer_float;
        caps_.floatLinearFiltering = hasExtension("OES_texture_float_linear");
#else
        caps_.floatColorTargets = true;
        caps_.floatLinearFiltering = true;
#endif

#ifdef PRISMA_GLES
        vertexBinding_ = caps_.compute && glBindVertexBuffer != nullptr &&
                         glVertexAttribFormat != nullptr && glVertexAttribIFormat != nullptr &&
                         glVertexAttribBinding != nullptr && glVertexBindingDivisor != nullptr;
#else
        vertexBinding_ = true;
#endif
        glGenVertexArrays(1, &scratchVertexArray_);
#ifndef PRISMA_GLES
        glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
#endif
    }

    ~GLDriver() override
    {
        if (debug_) glDebugMessageCallback(nullptr, nullptr);
        for (GLPipeline& pipeline: pipelines_)
        {
            glDeleteProgram(pipeline.program);
            glDeleteVertexArrays(1, &pipeline.vertexArray);
        }
        for (GLShader& shader: shaders_) glDeleteShader(shader.id);
        for (std::size_t i = 0; i < framebuffers_.size(); ++i)
            glDeleteFramebuffers(1, &framebuffers_[i].id);
        for (GLTexture& texture: textures_)
        {
            if (texture.samples > 1) glDeleteRenderbuffers(1, &texture.id);
            else
                glDeleteTextures(1, &texture.id);
        }
        for (GLSampler& sampler: samplers_) glDeleteSamplers(1, &sampler.id);
        if (copyFramebuffers_[0]) glDeleteFramebuffers(2, copyFramebuffers_);
        for (GLQuery& query: queries_) glDeleteQueries(kQuerySlots * 2, &query.ids[0][0]);
        for (GLReadback& readback: readbacks_)
        {
            if (readback.sync) glDeleteSync(readback.sync);
            glDeleteBuffers(1, &readback.buffer);
        }
        for (GLBuffer& buffer: buffers_)
            for (GLuint id: buffer.versions)
                if (id) glDeleteBuffers(1, &id);
        glDeleteVertexArrays(1, &scratchVertexArray_);
    }

    DriverType type() const override { return DriverType::OpenGL; }
    const Caps& caps() const override { return caps_; }

    BufferHandle createBuffer(const BufferDesc& desc) override
    {
        if (desc.size == 0) return BufferHandle();
        if ((desc.usage == BufferUsage::Storage || desc.usage == BufferUsage::Indirect) &&
                !caps_.compute)
        {
            log("createBuffer: storage and indirect buffers are not supported");
            return BufferHandle();
        }

        GLBuffer buffer;
        buffer.size = desc.size;
        buffer.usage = desc.usage;
        buffer.indexFormat = desc.indexFormat;
        glGenBuffers(1, &buffer.id);
        buffer.versions[0] = buffer.id;
        buffer.stream = desc.update == BufferUpdate::Stream && desc.usage != BufferUsage::Storage &&
                        desc.usage != BufferUsage::Indirect;
        const GLenum target = bindForEdit(buffer);
        glBufferData(target, desc.size, desc.data, toGLUpdate(desc.update));
        label(GL_BUFFER, buffer.id, desc.debugName);
        return handleCast<BufferHandle>(buffers_.insert(buffer));
    }

    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc) override
    {
        const GLShader* shader = shaders_.get(handleCast<ShaderSlot>(desc.shader));
        if (!caps_.compute || !shader || shader->stage != ShaderStage::Compute ||
                desc.uniformBlockCount > ComputePipelineDesc::kMaxUniformBlocks ||
                desc.textureCount > ComputePipelineDesc::kMaxTextures ||
                desc.storageBufferCount > ComputePipelineDesc::kMaxStorageBuffers ||
                desc.storageTextureCount > ComputePipelineDesc::kMaxStorageTextures)
        {
            log("createComputePipeline: compute not supported, invalid shader or too many "
                "bindings");
            return PipelineHandle();
        }

        GLPipeline pipeline;
        pipeline.compute = true;
        pipeline.program = glCreateProgram();
        glAttachShader(pipeline.program, shader->id);
        glLinkProgram(pipeline.program);
        glDetachShader(pipeline.program, shader->id);
        GLint linked = GL_FALSE;
        glGetProgramiv(pipeline.program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char message[1024];
            glGetProgramInfoLog(pipeline.program, sizeof(message), nullptr, message);
            log(message);
            glDeleteProgram(pipeline.program);
            return PipelineHandle();
        }
        label(GL_PROGRAM, pipeline.program, desc.debugName);

        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
        {
            const GLuint index =
                    glGetUniformBlockIndex(pipeline.program, desc.uniformBlocks[i].name);
            if (index == GL_INVALID_INDEX)
            {
                log("createComputePipeline: uniform block not found in the shader");
                continue;
            }
            glUniformBlockBinding(pipeline.program, index, desc.uniformBlocks[i].slot);
        }
        state_.useProgram(pipeline.program);
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
        {
            const GLint location = glGetUniformLocation(pipeline.program, desc.textures[i].name);
            if (location < 0)
            {
                log("createComputePipeline: texture not found in the shader");
                continue;
            }
            glUniform1i(location, static_cast<GLint>(desc.textures[i].slot));
        }
        bool mapped = mapStorageBuffers(pipeline.program, desc.storageBuffers,
                desc.storageBufferCount, true);
        for (std::uint32_t i = 0; mapped && i < desc.storageTextureCount; ++i)
            mapped = mapStorageTexture(pipeline.program, desc.storageTextures[i].name,
                    desc.storageTextures[i].slot, true);
        mapped = mapped && applyBindings(pipeline.program, *shader);
        if (!mapped)
        {
            state_.programDeleted(pipeline.program);
            glDeleteProgram(pipeline.program);
            return PipelineHandle();
        }
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    void updateBuffer(BufferHandle handle, std::uint32_t offset, const void* data,
            std::uint32_t size) override
    {
        if (passActive_)
        {
            log("updateBuffer: not allowed inside a render pass");
            return;
        }
        GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || !data || static_cast<std::uint64_t>(offset) + size > buffer->size)
        {
            log("updateBuffer: invalid buffer handle or range");
            return;
        }
        if (buffer->stream && buffer->updatedFrame != frameNumber_)
            rotateStreamBuffer(buffer, offset, size);
        const GLenum target = bindForEdit(*buffer);
        glBufferSubData(target, offset, size, data);
    }

    ShaderHandle createShader(const ShaderDesc& desc) override
    {
        if (!desc.source)
        {
            log("createShader: no source for this OpenGL version");
            return ShaderHandle();
        }
        if (desc.bindingCount > ShaderDesc::kMaxBindings)
        {
            log("createShader: too many bindings");
            return ShaderHandle();
        }

        if (desc.stage == ShaderStage::Compute && !caps_.compute)
        {
            log("createShader: compute shaders are not supported");
            return ShaderHandle();
        }
        if ((desc.stage == ShaderStage::Geometry && !caps_.geometryShaders) ||
                ((desc.stage == ShaderStage::TessControl || desc.stage == ShaderStage::TessEval) &&
                        !caps_.tessellation))
        {
            log("createShader: geometry and tessellation shaders are not supported");
            return ShaderHandle();
        }
        GLShader shader;
        shader.stage = desc.stage;
        const GLenum type = desc.stage == ShaderStage::Vertex        ? GL_VERTEX_SHADER
                            : desc.stage == ShaderStage::Fragment    ? GL_FRAGMENT_SHADER
                            : desc.stage == ShaderStage::Geometry    ? GL_GEOMETRY_SHADER
                            : desc.stage == ShaderStage::TessControl ? GL_TESS_CONTROL_SHADER
                            : desc.stage == ShaderStage::TessEval    ? GL_TESS_EVALUATION_SHADER
                                                                     : GL_COMPUTE_SHADER;
        shader.id = compileShader(type, desc.source, desc.debugName);
        if (!shader.id) return ShaderHandle();
        if (desc.innerSource &&
                (desc.stage == ShaderStage::Vertex || desc.stage == ShaderStage::TessEval))
        {
            shader.innerId = compileShader(type, desc.innerSource, desc.debugName);
            if (!shader.innerId)
            {
                glDeleteShader(shader.id);
                return ShaderHandle();
            }
        }
        for (std::uint32_t i = 0; i < desc.bindingCount; ++i)
        {
            GLBinding& binding = shader.bindings[shader.bindingCount];
            if (!desc.bindings[i].name || strlen(desc.bindings[i].name) >= sizeof(binding.name))
            {
                log("createShader: a binding has no name, or its name is too long");
                glDeleteShader(shader.id);
                if (shader.innerId) glDeleteShader(shader.innerId);
                return ShaderHandle();
            }
            binding.kind = desc.bindings[i].kind;
            binding.slot = desc.bindings[i].slot;
            strcpy(binding.name, desc.bindings[i].name);
            ++shader.bindingCount;
        }
        return handleCast<ShaderHandle>(shaders_.insert(shader));
    }

    TextureHandle createTexture(const TextureDesc& desc) override
    {
        const std::uint32_t largest = desc.width > desc.height ? desc.width : desc.height;
        if (desc.format == TextureFormat::None || desc.width == 0 || desc.height == 0 ||
                desc.depth == 0 || largest > caps_.maxTextureSize ||
                (isCube(desc.type) && desc.width != desc.height) ||
                (desc.type == TextureType::TextureCubeArray && !caps_.cubeArrays))
        {
            log("createTexture: invalid format or size, or cube map arrays are not supported");
            return TextureHandle();
        }
        if (!supportedFormat(desc))
        {
            log("createTexture: compressed format not supported, or used as a render target or "
                "as a 3D texture");
            return TextureHandle();
        }
        if (!validSamples(desc, caps_.maxSamples))
        {
            log("createTexture: a multisampled texture must be a 2D render target only, with one "
                "mip, no data and a supported sample count");
            return TextureHandle();
        }

        if ((desc.usage & kTextureRenderTarget) && isFloatFormat(desc.format) &&
                !caps_.floatColorTargets)
        {
            log("createTexture: floating point render targets are not supported");
            return TextureHandle();
        }
        if ((desc.usage & kTextureStorage) &&
                (!caps_.compute || desc.samples != 1 || !isStorageFormat(desc.format, caps_.gles)))
        {
            log("createTexture: this format cannot be used as a storage texture");
            return TextureHandle();
        }

        GLTexture texture;
        texture.type = desc.type;
        texture.target = toGLTarget(desc.type);
        texture.width = desc.width;
        texture.height = desc.height;
        texture.depth = desc.type == TextureType::Texture2D || desc.type == TextureType::TextureCube
                                ? 1
                                : desc.depth;
        texture.format = desc.format;
        texture.usage = desc.usage;

        const std::uint32_t volume = desc.type == TextureType::Texture3D ? texture.depth : 1;
        const std::uint32_t fullChain = fullMipCount(largest > volume ? largest : volume, 1);
        texture.mipLevels = desc.mipLevels == 0 ? fullChain : desc.mipLevels;
        if (texture.mipLevels > fullChain) texture.mipLevels = fullChain;

        const GLFormat format = toGLFormat(desc.format);
        if (desc.samples > 1)
        {
            GLint supported = 0;
            glGetInternalformativ(GL_RENDERBUFFER, format.internal, GL_SAMPLES, 1, &supported);
            if (supported < static_cast<GLint>(desc.samples))
            {
                log("createTexture: sample count not supported for this format");
                return TextureHandle();
            }
            texture.samples = desc.samples;
            texture.mipLevels = 1;
            glGenRenderbuffers(1, &texture.id);
            glBindRenderbuffer(GL_RENDERBUFFER, texture.id);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, static_cast<GLsizei>(desc.samples),
                    format.internal, static_cast<GLsizei>(desc.width),
                    static_cast<GLsizei>(desc.height));
            label(GL_RENDERBUFFER, texture.id, desc.debugName);
            return handleCast<TextureHandle>(textures_.insert(texture));
        }
        glGenTextures(1, &texture.id);
        bindForEdit(texture);
        if (desc.type != TextureType::Texture2D && desc.type != TextureType::TextureCube)
            glTexStorage3D(texture.target, static_cast<GLsizei>(texture.mipLevels), format.internal,
                    static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                    static_cast<GLsizei>(desc.type == TextureType::TextureCubeArray
                                                 ? texture.depth * 6
                                                 : texture.depth));
        else
            glTexStorage2D(texture.target, static_cast<GLsizei>(texture.mipLevels), format.internal,
                    static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));

        if (desc.data && !isDepthFormat(desc.format))
        {
            const std::uint32_t images =
                    desc.type == TextureType::Texture3D ? 1 : layerCount(texture, 0);
            const std::size_t imageBytes = levelBytes(desc.format, desc.width, desc.height);
            for (std::uint32_t i = 0; i < images; ++i)
                uploadLevel(texture, 0, i, static_cast<const char*>(desc.data) + i * imageBytes);
            if (desc.generateMipmaps && texture.mipLevels > 1 && !isCompressedFormat(desc.format))
                glGenerateMipmap(texture.target);
        }
        label(GL_TEXTURE, texture.id, desc.debugName);
        return handleCast<TextureHandle>(textures_.insert(texture));
    }

    void updateTexture(TextureHandle handle, std::uint32_t mip, std::uint32_t layer,
            const void* data) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(handle));
        if (passActive_ || !texture || !data || isDepthFormat(texture->format) ||
                texture->samples > 1 || mip >= texture->mipLevels ||
                (texture->type != TextureType::Texture3D && layer >= layerCount(*texture, mip)))
        {
            log("updateTexture: invalid handle, mip or layer, or called inside a render pass");
            return;
        }
        uploadLevel(*texture, mip, layer, data);
    }

    void updateTextureRegion(TextureHandle handle, const TextureRegion& region,
            const void* data) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(handle));
        if (passActive_ || !texture || !data || isDepthFormat(texture->format) ||
                texture->samples > 1 || region.mip >= texture->mipLevels ||
                region.layer >= layerCount(*texture, region.mip) ||
                !validRegion(texture->format, mipSize(texture->width, region.mip),
                        mipSize(texture->height, region.mip), region))
        {
            log("updateTextureRegion: invalid handle, mip, layer or rectangle, or called inside a "
                "render pass");
            return;
        }
        upload(*texture, region.mip, region.layer, region.x, region.y, region.width, region.height,
                1, data);
    }

    void generateMipmaps(TextureHandle handle) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(handle));
        if (passActive_ || !texture || isDepthFormat(texture->format) || texture->samples > 1 ||
                isCompressedFormat(texture->format))
        {
            log("generateMipmaps: invalid handle or format, or called inside a render pass");
            return;
        }
        if (texture->mipLevels < 2) return;
        bindForEdit(*texture);
        glGenerateMipmap(texture->target);
    }

    void copyTexture(const TextureCopy& copy) override
    {
        const GLTexture* source = textures_.get(handleCast<TextureSlot>(copy.source));
        const GLTexture* destination = textures_.get(handleCast<TextureSlot>(copy.destination));
        if (passActive_ || !validCopy(source, destination, copy,
                                   source ? layerCount(*source, copy.sourceMip) : 0,
                                   destination ? layerCount(*destination, copy.destinationMip) : 0))
        {
            log("copyTexture: invalid textures, formats or rectangles, or called inside a render "
                "pass");
            return;
        }
        const GLint sourceX = static_cast<GLint>(copy.sourceX);
        const GLint sourceY = static_cast<GLint>(copy.sourceY);
        const GLint destinationX = static_cast<GLint>(copy.destinationX);
        const GLint destinationY = static_cast<GLint>(copy.destinationY);
        const GLsizei width = static_cast<GLsizei>(copy.width);
        const GLsizei height = static_cast<GLsizei>(copy.height);
        if (copyImage_)
        {
            glCopyImageSubData(source->id, source->target, static_cast<GLint>(copy.sourceMip),
                    sourceX, sourceY, static_cast<GLint>(copy.sourceLayer), destination->id,
                    destination->target, static_cast<GLint>(copy.destinationMip), destinationX,
                    destinationY, static_cast<GLint>(copy.destinationLayer), width, height, 1);
            return;
        }
        if (isCompressedFormat(source->format))
        {
            log("copyTexture: compressed textures cannot be copied on this OpenGL ES version");
            return;
        }

        const bool depth = isDepthFormat(source->format);
        const bool stencil = source->format == TextureFormat::Depth24Stencil8;
        const GLenum point = stencil ? GL_DEPTH_STENCIL_ATTACHMENT
                             : depth ? GL_DEPTH_ATTACHMENT
                                     : GL_COLOR_ATTACHMENT0;
        const GLbitfield mask = stencil ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
                                : depth ? GL_DEPTH_BUFFER_BIT
                                        : GL_COLOR_BUFFER_BIT;
        RenderTarget from;
        from.mip = copy.sourceMip;
        from.layer = copy.sourceLayer;
        RenderTarget to;
        to.mip = copy.destinationMip;
        to.layer = copy.destinationLayer;
        if (!copyFramebuffers_[0]) glGenFramebuffers(2, copyFramebuffers_);
        state_.bindFramebuffer(copyFramebuffers_[0]);
        attach(point, *source, from);
        state_.bindFramebuffer(copyFramebuffers_[1]);
        attach(point, *destination, to);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, copyFramebuffers_[0]);
        state_.scissorTest(false);
        writeAllColors();
        state_.depthMask(true);
        state_.stencilWriteMask(0xFF);
        glBlitFramebuffer(sourceX, sourceY, sourceX + width, sourceY + height, destinationX,
                destinationY, destinationX + width, destinationY + height, mask, GL_NEAREST);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, point, GL_TEXTURE_2D, 0, 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, point, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, copyFramebuffers_[1]);
    }

    void copyBuffer(BufferHandle sourceHandle, std::uint32_t sourceOffset,
            BufferHandle destinationHandle, std::uint32_t destinationOffset,
            std::uint32_t size) override
    {
        const GLBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        const GLBuffer* destination = buffers_.get(handleCast<BufferSlot>(destinationHandle));
        if (passActive_ || !source || !destination ||
                !validBufferCopy(true, source == destination, source->usage == BufferUsage::Index,
                        destination->usage == BufferUsage::Index, source->size, sourceOffset,
                        destination->size, destinationOffset, size))
        {
            log("copyBuffer: invalid buffers or ranges, index and other buffers mixed, or called "
                "inside a render pass");
            return;
        }
        glBindBuffer(GL_COPY_READ_BUFFER, source->id);
        glBindBuffer(GL_COPY_WRITE_BUFFER, destination->id);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, sourceOffset,
                destinationOffset, size);
    }

    SamplerHandle createSampler(const SamplerDesc& desc) override
    {
        GLSampler sampler;
        glGenSamplers(1, &sampler.id);
        glSamplerParameteri(sampler.id, GL_TEXTURE_MIN_FILTER,
                toGLMinFilter(desc.minFilter, desc.mipFilter));
        glSamplerParameteri(sampler.id, GL_TEXTURE_MAG_FILTER,
                desc.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_S, toGLAddress(desc.addressU));
        glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_T, toGLAddress(desc.addressV));
        glSamplerParameteri(sampler.id, GL_TEXTURE_WRAP_R, toGLAddress(desc.addressW));
        if (desc.compare)
        {
            glSamplerParameteri(sampler.id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glSamplerParameteri(sampler.id, GL_TEXTURE_COMPARE_FUNC,
                    static_cast<GLint>(toGLCompare(desc.compareOp)));
        }
        if (desc.maxAnisotropy > 1.0f && caps_.maxAnisotropy > 1.0f)
            glSamplerParameterf(sampler.id, kTextureMaxAnisotropy,
                    desc.maxAnisotropy < caps_.maxAnisotropy ? desc.maxAnisotropy
                                                             : caps_.maxAnisotropy);
        label(GL_SAMPLER, sampler.id, desc.debugName);
        return handleCast<SamplerHandle>(samplers_.insert(sampler));
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override
    {
        const GLShader* vertex = shaders_.get(handleCast<ShaderSlot>(desc.vertexShader));
        const GLShader* fragment = shaders_.get(handleCast<ShaderSlot>(desc.fragmentShader));
        const GLShader* geometry =
                desc.geometryShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.geometryShader))
                        : nullptr;
        const GLShader* control =
                desc.tessControlShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.tessControlShader))
                        : nullptr;
        const GLShader* evaluation =
                desc.tessEvalShader.valid()
                        ? shaders_.get(handleCast<ShaderSlot>(desc.tessEvalShader))
                        : nullptr;
        const bool patches = desc.topology == Topology::Patches;
        if (desc.independentBlend && !caps_.independentBlend)
        {
            log("createPipeline: a blend state for each target is not supported");
            return PipelineHandle();
        }
        const bool badGeometry =
                (desc.geometryShader.valid() &&
                        (!geometry || geometry->stage != ShaderStage::Geometry)) ||
                (isAdjacency(desc.topology) && !geometry) ||
                (desc.tessControlShader.valid() &&
                        (!control || control->stage != ShaderStage::TessControl)) ||
                (desc.tessEvalShader.valid() &&
                        (!evaluation || evaluation->stage != ShaderStage::TessEval)) ||
                patches != (control && evaluation) ||
                (control != nullptr) != (evaluation != nullptr) ||
                (patches && (desc.patchControlPoints == 0 ||
                                    desc.patchControlPoints > caps_.maxPatchControlPoints));
        if (!vertex || !fragment || badGeometry ||
                desc.attributeCount > PipelineDesc::kMaxAttributes ||
                desc.uniformBlockCount > PipelineDesc::kMaxUniformBlocks ||
                desc.textureCount > PipelineDesc::kMaxTextures ||
                desc.storageBufferCount > PipelineDesc::kMaxStorageBuffers ||
                (desc.storageBufferCount > 0 && !caps_.storageBuffersInGraphics) ||
                desc.targets.colorCount > TargetFormats::kMaxColors || !validVertexInput(desc))
        {
            log("createPipeline: invalid shader handle or too many attributes or uniform blocks");
            return PipelineHandle();
        }

        GLPipeline pipeline;
        pipeline.program = glCreateProgram();
        const bool extraStages = geometry || control;
        const GLuint vertexId = extraStages && vertex->innerId ? vertex->innerId : vertex->id;
        const GLuint evaluationId = evaluation && geometry && evaluation->innerId
                                            ? evaluation->innerId
                                    : evaluation ? evaluation->id
                                                 : 0;
        glAttachShader(pipeline.program, vertexId);
        glAttachShader(pipeline.program, fragment->id);
        if (geometry) glAttachShader(pipeline.program, geometry->id);
        if (control) glAttachShader(pipeline.program, control->id);
        if (evaluation) glAttachShader(pipeline.program, evaluationId);
        glLinkProgram(pipeline.program);
        glDetachShader(pipeline.program, vertexId);
        glDetachShader(pipeline.program, fragment->id);
        if (geometry) glDetachShader(pipeline.program, geometry->id);
        if (control) glDetachShader(pipeline.program, control->id);
        if (evaluation) glDetachShader(pipeline.program, evaluationId);

        GLint linked = GL_FALSE;
        glGetProgramiv(pipeline.program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char message[1024];
            glGetProgramInfoLog(pipeline.program, sizeof(message), nullptr, message);
            log(message);
            glDeleteProgram(pipeline.program);
            return PipelineHandle();
        }

        pipeline.topology = toGLTopology(desc.topology);
        pipeline.patchVertices = patches ? static_cast<GLint>(desc.patchControlPoints) : 0;
        pipeline.vertexBufferCount = desc.vertexBufferCount;
        for (std::uint32_t i = 0; i < desc.vertexBufferCount; ++i)
            pipeline.vertexBuffers[i] = desc.vertexBuffers[i];
        pipeline.attributeCount = desc.attributeCount;
        pipeline.targets = desc.targets;
        pipeline.depthTest = desc.depthTest;
        pipeline.depthWrite = desc.depthWrite;
        pipeline.depthFunc = toGLCompare(desc.depthCompare);
        pipeline.cull = desc.cullMode != CullMode::None;
        pipeline.cullFace = desc.cullMode == CullMode::Front ? GL_FRONT : GL_BACK;
        pipeline.frontFace = desc.frontFace == FrontFace::Clockwise ? GL_CW : GL_CCW;
        pipeline.independentBlend = desc.independentBlend;
        for (std::uint32_t i = 0; i < TargetFormats::kMaxColors; ++i)
        {
            const BlendState& state = desc.targetBlend[i];
            GLBlendTarget& target = pipeline.targetBlend[i];
            target.blend = state.blend;
            target.func[0] = toGLBlendFactor(state.srcColor);
            target.func[1] = toGLBlendFactor(state.dstColor);
            target.func[2] = toGLBlendFactor(state.srcAlpha);
            target.func[3] = toGLBlendFactor(state.dstAlpha);
            target.equation[0] = toGLBlendOp(state.colorBlendOp);
            target.equation[1] = toGLBlendOp(state.alphaBlendOp);
            target.mask = state.colorMask;
        }
        pipeline.blend = desc.blend;
        pipeline.blendFunc[0] = toGLBlendFactor(desc.srcColor);
        pipeline.blendFunc[1] = toGLBlendFactor(desc.dstColor);
        pipeline.blendFunc[2] = toGLBlendFactor(desc.srcAlpha);
        pipeline.blendFunc[3] = toGLBlendFactor(desc.dstAlpha);
        pipeline.blendEquation[0] = toGLBlendOp(desc.colorBlendOp);
        pipeline.blendEquation[1] = toGLBlendOp(desc.alphaBlendOp);
        pipeline.colorMask = desc.colorMask;
        pipeline.stencil.enabled = desc.stencilTest;
        const StencilFace* faces[2] = { &desc.stencilFront, &desc.stencilBack };
        for (int face = 0; face < 2; ++face)
        {
            pipeline.stencil.compare[face] = toGLCompare(faces[face]->compare);
            pipeline.stencil.failOp[face] = toGLStencilOp(faces[face]->failOp);
            pipeline.stencil.depthFailOp[face] = toGLStencilOp(faces[face]->depthFailOp);
            pipeline.stencil.passOp[face] = toGLStencilOp(faces[face]->passOp);
        }
        pipeline.stencil.readMask = desc.stencilReadMask;
        pipeline.stencil.writeMask = desc.stencilWriteMask;
        pipeline.depthBiasConstant = desc.depthBiasConstant;
        pipeline.depthBiasSlope = desc.depthBiasSlope;
        pipeline.wireframe = desc.wireframe && caps_.wireframe;
        pipeline.alphaToCoverage = desc.alphaToCoverage;

        for (std::uint32_t i = 0; i < desc.uniformBlockCount; ++i)
        {
            const UniformBlockBinding& block = desc.uniformBlocks[i];
            const GLuint index = glGetUniformBlockIndex(pipeline.program, block.name);
            if (index == GL_INVALID_INDEX)
            {
                log("createPipeline: uniform block not found in the shaders");
                continue;
            }
            glUniformBlockBinding(pipeline.program, index, block.slot);
        }

        if (desc.textureCount > 0) state_.useProgram(pipeline.program);
        for (std::uint32_t i = 0; i < desc.textureCount; ++i)
        {
            const TextureBinding& binding = desc.textures[i];
            const GLint location = glGetUniformLocation(pipeline.program, binding.name);
            if (location < 0)
            {
                log("createPipeline: texture not found in the shaders");
                continue;
            }
            glUniform1i(location, static_cast<GLint>(binding.slot));
        }
        if (!mapStorageBuffers(pipeline.program, desc.storageBuffers, desc.storageBufferCount,
                    true) ||
                !applyBindings(pipeline.program, *vertex) ||
                !applyBindings(pipeline.program, *fragment) ||
                (geometry && !applyBindings(pipeline.program, *geometry)) ||
                (control && !applyBindings(pipeline.program, *control)) ||
                (evaluation && !applyBindings(pipeline.program, *evaluation)))
        {
            state_.programDeleted(pipeline.program);
            glDeleteProgram(pipeline.program);
            return PipelineHandle();
        }
        glGenVertexArrays(1, &pipeline.vertexArray);
        state_.bindVertexArray(pipeline.vertexArray);
        label(GL_PROGRAM, pipeline.program, desc.debugName);
        pipeline.vertexBinding = vertexBinding_;
        for (std::uint32_t i = 0; i < desc.attributeCount; ++i)
            if (desc.attributes[i].offset > 2047) pipeline.vertexBinding = false;
        for (std::uint32_t i = 0; i < desc.attributeCount; ++i)
        {
            const VertexAttribute& attribute = desc.attributes[i];
            pipeline.attributes[i] = attribute;
            glEnableVertexAttribArray(attribute.location);
            if (!pipeline.vertexBinding)
            {
                glVertexAttribDivisor(attribute.location,
                        desc.vertexBuffers[attribute.buffer].step == VertexStep::Instance ? 1 : 0);
                continue;
            }
            const GLVertexFormat format = toGLVertexFormat(attribute.format);
            if (format.integer)
                glVertexAttribIFormat(attribute.location, format.components, format.type,
                        attribute.offset);
            else
                glVertexAttribFormat(attribute.location, format.components, format.type,
                        format.normalized, attribute.offset);
            glVertexAttribBinding(attribute.location, attribute.buffer);
        }
        for (std::uint32_t i = 0; pipeline.vertexBinding && i < desc.vertexBufferCount; ++i)
            glVertexBindingDivisor(i, desc.vertexBuffers[i].step == VertexStep::Instance ? 1 : 0);
        return handleCast<PipelineHandle>(pipelines_.insert(pipeline));
    }

    void destroy(BufferHandle handle) override
    {
        const BufferSlot slot = handleCast<BufferSlot>(handle);
        const GLBuffer* buffer = buffers_.get(slot);
        if (!buffer) return;
        for (GLuint id: buffer->versions)
        {
            if (!id) continue;
            glDeleteBuffers(1, &id);
            state_.bufferDeleted(id);
        }
        buffers_.erase(slot);
    }

    void destroy(ShaderHandle handle) override
    {
        const ShaderSlot slot = handleCast<ShaderSlot>(handle);
        const GLShader* shader = shaders_.get(slot);
        if (!shader) return;
        glDeleteShader(shader->id);
        if (shader->innerId) glDeleteShader(shader->innerId);
        shaders_.erase(slot);
    }

    void destroy(PipelineHandle handle) override
    {
        const PipelineSlot slot = handleCast<PipelineSlot>(handle);
        const GLPipeline* pipeline = pipelines_.get(slot);
        if (!pipeline) return;
        glDeleteProgram(pipeline->program);
        glDeleteVertexArrays(1, &pipeline->vertexArray);
        state_.programDeleted(pipeline->program);
        state_.vertexArrayDeleted(pipeline->vertexArray);
        pipelines_.erase(slot);
    }

    void destroy(TextureHandle handle) override
    {
        const TextureSlot slot = handleCast<TextureSlot>(handle);
        const GLTexture* texture = textures_.get(slot);
        if (!texture) return;
        for (std::size_t i = framebuffers_.size(); i > 0; --i)
        {
            if (!framebufferUses(framebuffers_[i - 1], handle.bits())) continue;
            glDeleteFramebuffers(1, &framebuffers_[i - 1].id);
            state_.framebufferDeleted(framebuffers_[i - 1].id);
            framebuffers_.erase(framebuffers_.begin() + (i - 1));
        }
        if (texture->samples > 1) glDeleteRenderbuffers(1, &texture->id);
        else
        {
            glDeleteTextures(1, &texture->id);
            state_.textureDeleted(texture->id);
        }
        textures_.erase(slot);
    }

    void destroy(SamplerHandle handle) override
    {
        const SamplerSlot slot = handleCast<SamplerSlot>(handle);
        const GLSampler* sampler = samplers_.get(slot);
        if (!sampler) return;
        glDeleteSamplers(1, &sampler->id);
        state_.samplerDeleted(sampler->id);
        samplers_.erase(slot);
    }

    QueryHandle createQuery(QueryType type) override
    {
        if (type == QueryType::Time && !caps_.timerQueries) return QueryHandle();
        GLQuery query;
        query.type = type;
        glGenQueries(kQuerySlots * 2, &query.ids[0][0]);
        return handleCast<QueryHandle>(queries_.insert(query));
    }

    void destroy(QueryHandle handle) override
    {
        const QuerySlot slot = handleCast<QuerySlot>(handle);
        GLQuery* query = queries_.get(slot);
        if (!query) return;
        if (query->active >= 0 && query->type == QueryType::Occlusion)
        {
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            occlusionActive_ = false;
            openOcclusion_ = QueryHandle();
        }
        glDeleteQueries(kQuerySlots * 2, &query->ids[0][0]);
        queries_.erase(slot);
    }

    void beginQuery(QueryHandle handle) override
    {
        GLQuery* query = queries_.get(handleCast<QuerySlot>(handle));
        if (!query || query->active >= 0 ||
                (query->type == QueryType::Occlusion && (!passActive_ || occlusionActive_)))
        {
            log("beginQuery: invalid or already open query, or an occlusion query outside a "
                "render pass");
            return;
        }
        const std::uint32_t slot = query->next;
        query->next = (slot + 1) % kQuerySlots;
        query->active = static_cast<std::int32_t>(slot);
        query->sequence[slot] = 0;
        if (query->type == QueryType::Occlusion)
        {
            glBeginQuery(GL_ANY_SAMPLES_PASSED, query->ids[slot][0]);
            occlusionActive_ = true;
            openOcclusion_ = handle;
        }
        else
            writeTimestamp(query->ids[slot][0]);
    }

    void endQuery(QueryHandle handle) override
    {
        GLQuery* query = queries_.get(handleCast<QuerySlot>(handle));
        if (!query || query->active < 0)
        {
            log("endQuery: the query is not open");
            return;
        }
        const std::uint32_t slot = static_cast<std::uint32_t>(query->active);
        query->active = -1;
        query->sequence[slot] = ++querySequence_;
        if (query->type == QueryType::Occlusion)
        {
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            occlusionActive_ = false;
            openOcclusion_ = QueryHandle();
        }
        else
            writeTimestamp(query->ids[slot][1]);
    }

    bool queryResult(QueryHandle handle, std::uint64_t* result) override
    {
        const GLQuery* query = queries_.get(handleCast<QuerySlot>(handle));
        if (!query || !result) return false;

        const int last = query->type == QueryType::Occlusion ? 0 : 1;
        int best = -1;
        for (std::uint32_t slot = 0; slot < kQuerySlots; ++slot)
        {
            if (query->sequence[slot] == 0) continue;
            GLuint available = 0;
            glGetQueryObjectuiv(query->ids[slot][last], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available && (best < 0 || query->sequence[slot] > query->sequence[best]))
                best = static_cast<int>(slot);
        }
        if (best < 0) return false;

        if (query->type == QueryType::Occlusion)
        {
            GLuint passed = 0;
            glGetQueryObjectuiv(query->ids[best][0], GL_QUERY_RESULT, &passed);
            *result = passed ? 1 : 0;
        }
        else
            *result = readTimestamp(query->ids[best][1]) - readTimestamp(query->ids[best][0]);
        return true;
    }

    void beginFrame() override
    {
        if (frameOpen_)
        {
            log("beginFrame: the previous frame was not presented");
            return;
        }
        frameOpen_ = true;
        if (!surfacesUsed_) return;
        platform_.makeCurrent(platform_.user);
        currentSurface_ = SwapchainHandle();
    }

    void beginRenderPass(const RenderPassDesc& desc) override
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (computeActive_)
        {
            log("beginRenderPass: a compute pass is still open");
            return;
        }
        passActive_ = false;
        passOffscreen_ = desc.colorCount > 0 || desc.depth.texture.valid();
        passColorCount_ = desc.colorCount;
        passHasDepth_ = true;
        passStencil_ = false;
        passColorStore_ = desc.colorStore;
        passDepthStore_ = desc.depthStore;

        if (passOffscreen_)
        {
            const GLFramebuffer* found = findFramebuffer(desc);
            if (!found) found = createFramebuffer(desc);
            if (!found)
            {
                log("beginRenderPass: invalid or unsupported render targets");
                return;
            }
            const GLFramebuffer framebuffer = *found;
            if (!prepareResolves(desc, framebuffer))
            {
                log("beginRenderPass: invalid resolve targets");
                return;
            }
            state_.bindFramebuffer(framebuffer.id);
            width = framebuffer.width;
            height = framebuffer.height;
            passHasDepth_ = desc.depth.texture.valid();
            passStencil_ = framebuffer.stencil;
            passFormats_ = framebuffer.formats;
        }
        else
        {
            const GLPlatform* surface = useSurface(desc.swapchain, true);
            if (!surface)
            {
                log("beginRenderPass: invalid swapchain");
                return;
            }
            surface->framebufferSize(surface->user, &width, &height);
            state_.bindFramebuffer(0);
            passFormats_ = TargetFormats();
            passStencil_ = true;
        }
        passActive_ = true;
        pipeline_ = PipelineHandle();
        for (std::uint32_t i = 0; i < PipelineDesc::kMaxVertexBuffers; ++i)
        {
            vertexBuffers_[i] = BufferHandle();
            vertexOffsets_[i] = 0;
        }
        indexBuffer_ = BufferHandle();
        vertexDirty_ = true;
        indexDirty_ = true;
        state_.framebufferSrgb(passOffscreen_);
        stencilReference_ = 0;
        passWidth_ = width;
        passHeight_ = height;
        state_.viewport(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
        state_.depthRange(0.0f, 1.0f);
        state_.scissorTest(false);

        GLbitfield mask = 0;
        if (desc.colorLoad == LoadOp::Clear && (!passOffscreen_ || desc.colorCount > 0))
        {
            writeAllColors();
            state_.clearColor(desc.clearColor);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (desc.depthLoad == LoadOp::Clear && passHasDepth_)
        {
            state_.depthMask(true);
            state_.clearDepth(desc.clearDepth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (desc.stencilLoad == LoadOp::Clear && passHasDepth_ && passStencil_)
        {
            state_.stencilWriteMask(0xFF);
            state_.clearStencil(desc.clearStencil);
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        if (mask) glClear(mask);
    }

    void setViewport(const Viewport& viewport) override
    {
        if (!passActive_) return;
        const std::int32_t width = static_cast<std::int32_t>(viewport.width + 0.5f);
        const std::int32_t height = static_cast<std::int32_t>(viewport.height + 0.5f);
        const std::int32_t x = static_cast<std::int32_t>(viewport.x + 0.5f);
        const std::int32_t top = static_cast<std::int32_t>(viewport.y + 0.5f);
        state_.viewport(x, static_cast<std::int32_t>(passHeight_) - (top + height), width, height);
        state_.depthRange(viewport.minDepth, viewport.maxDepth);
    }

    void setScissor(const Rect& rect) override
    {
        if (!passActive_) return;
        const bool whole = rect.x <= 0 && rect.y <= 0 &&
                           rect.x + static_cast<std::int64_t>(rect.width) >= passWidth_ &&
                           rect.y + static_cast<std::int64_t>(rect.height) >= passHeight_;
        state_.scissorTest(!whole);
        if (whole) return;
        const std::int32_t height = static_cast<std::int32_t>(rect.height);
        state_.scissor(rect.x, static_cast<std::int32_t>(passHeight_) - (rect.y + height),
                static_cast<std::int32_t>(rect.width), height);
    }

    void setStencilReference(std::uint32_t reference) override { stencilReference_ = reference; }

    void bindPipeline(PipelineHandle handle) override
    {
        pipeline_ = handle;
        vertexDirty_ = true;
        indexDirty_ = true;
    }

    void bindVertexBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset) override
    {
        if (slot >= PipelineDesc::kMaxVertexBuffers)
        {
            log("bindVertexBuffer: slot out of range");
            return;
        }
        vertexBuffers_[slot] = handle;
        vertexOffsets_[slot] = offset;
        vertexDirty_ = true;
    }

    void bindIndexBuffer(BufferHandle handle) override
    {
        indexBuffer_ = handle;
        indexDirty_ = true;
    }

    void bindUniformBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Uniform || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= GLState::kMaxUniformSlots)
        {
            log("bindUniformBuffer: invalid buffer handle, range or slot");
            return;
        }
        state_.bindUniformBufferRange(slot, buffer->id, offset, size);
    }

    void bindTexture(std::uint32_t slot, TextureHandle textureHandle,
            SamplerHandle samplerHandle) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(textureHandle));
        const GLSampler* sampler = samplers_.get(handleCast<SamplerSlot>(samplerHandle));
        if (!texture || !sampler || texture->samples > 1 || slot >= GLState::kMaxTextureUnits)
        {
            log("bindTexture: invalid texture handle, sampler handle or slot");
            return;
        }
        state_.bindTexture(slot, texture->target, texture->id);
        state_.bindSampler(slot, sampler->id);
    }

    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex,
            std::uint32_t instanceCount) override
    {
        const GLPipeline* pipeline =
                prepareDraw(static_cast<std::uint64_t>(firstVertex) + vertexCount, instanceCount);
        if (!pipeline) return;
        if (instanceCount == 1)
            glDrawArrays(pipeline->topology, static_cast<GLint>(firstVertex),
                    static_cast<GLsizei>(vertexCount));
        else
            glDrawArraysInstanced(pipeline->topology, static_cast<GLint>(firstVertex),
                    static_cast<GLsizei>(vertexCount), static_cast<GLsizei>(instanceCount));
    }

    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
            std::uint32_t instanceCount) override
    {
        const GLPipeline* pipeline = prepareDraw(0, instanceCount);
        if (!pipeline) return;

        const GLBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexed: no valid index buffer bound");
            return;
        }
        if (indexDirty_)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->id);
            indexDirty_ = false;
        }

        const bool wide = indices->indexFormat == IndexFormat::UInt32;
        const std::size_t indexSize = wide ? 4 : 2;
        const std::size_t offset = static_cast<std::size_t>(firstIndex) * indexSize;
        if (offset + static_cast<std::size_t>(indexCount) * indexSize > indices->size)
        {
            log("drawIndexed: index range is outside the index buffer");
            return;
        }
        const GLenum type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        if (instanceCount == 1)
            glDrawElements(pipeline->topology, static_cast<GLsizei>(indexCount), type,
                    reinterpret_cast<const void*>(offset));
        else
            glDrawElementsInstanced(pipeline->topology, static_cast<GLsizei>(indexCount), type,
                    reinterpret_cast<const void*>(offset), static_cast<GLsizei>(instanceCount));
    }

    void drawIndirect(BufferHandle handle, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride) override
    {
        const GLBuffer* arguments =
                indirectBuffer(handle, offset, drawCount, stride, sizeof(DrawIndirectCommand));
        if (!arguments) return;
        const GLPipeline* pipeline = prepareDraw(0, 1);
        if (!pipeline) return;

        const std::size_t step = stride ? stride : sizeof(DrawIndirectCommand);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, arguments->id);
#ifdef PRISMA_GLES
        for (std::uint32_t i = 0; i < drawCount; ++i)
            glDrawArraysIndirect(pipeline->topology,
                    reinterpret_cast<const void*>(offset + i * step));
#else
        glMultiDrawArraysIndirect(pipeline->topology,
                reinterpret_cast<const void*>(static_cast<std::size_t>(offset)),
                static_cast<GLsizei>(drawCount), static_cast<GLsizei>(step));
#endif
    }

    void drawIndexedIndirect(BufferHandle handle, std::uint32_t offset, std::uint32_t drawCount,
            std::uint32_t stride) override
    {
        const GLBuffer* arguments = indirectBuffer(handle, offset, drawCount, stride,
                sizeof(DrawIndexedIndirectCommand));
        if (!arguments) return;
        const GLPipeline* pipeline = prepareDraw(0, 1);
        if (!pipeline) return;

        const GLBuffer* indices = buffers_.get(handleCast<BufferSlot>(indexBuffer_));
        if (!indices || indices->usage != BufferUsage::Index)
        {
            log("drawIndexedIndirect: no valid index buffer bound");
            return;
        }
        if (indexDirty_)
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->id);
            indexDirty_ = false;
        }
        const GLenum type =
                indices->indexFormat == IndexFormat::UInt32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const std::size_t step = stride ? stride : sizeof(DrawIndexedIndirectCommand);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, arguments->id);
#ifdef PRISMA_GLES
        for (std::uint32_t i = 0; i < drawCount; ++i)
            glDrawElementsIndirect(pipeline->topology, type,
                    reinterpret_cast<const void*>(offset + i * step));
#else
        glMultiDrawElementsIndirect(pipeline->topology, type,
                reinterpret_cast<const void*>(static_cast<std::size_t>(offset)),
                static_cast<GLsizei>(drawCount), static_cast<GLsizei>(step));
#endif
    }

    void beginComputePass() override
    {
        if (passActive_ || computeActive_ || !caps_.compute)
        {
            log("beginComputePass: compute not supported, or another pass is still open");
            return;
        }
        computeActive_ = true;
        pipeline_ = PipelineHandle();
    }

    void bindStorageBuffer(std::uint32_t slot, BufferHandle handle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const GLBuffer* buffer = buffers_.get(handleCast<BufferSlot>(handle));
        if (!buffer || buffer->usage != BufferUsage::Storage || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > buffer->size ||
                slot >= PipelineDesc::kMaxStorageBuffers ||
                offset % caps_.storageBufferOffsetAlignment != 0)
        {
            log("bindStorageBuffer: invalid buffer handle, range, alignment or slot");
            return;
        }
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, slot, buffer->id, offset, size);
        if (passActive_) storageUsedInPass_ = true;
    }

    void bindStorageTexture(std::uint32_t slot, TextureHandle handle, std::uint32_t mip,
            StorageAccess access) override
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(handle));
        if (!texture || !(texture->usage & kTextureStorage) || mip >= texture->mipLevels ||
                slot >= ComputePipelineDesc::kMaxStorageTextures)
        {
            log("bindStorageTexture: invalid slot or mip, or the texture was not created with "
                "kTextureStorage");
            return;
        }
        const GLenum mode = access == StorageAccess::Read    ? GL_READ_ONLY
                            : access == StorageAccess::Write ? GL_WRITE_ONLY
                                                             : GL_READ_WRITE;
        glBindImageTexture(slot, texture->id, static_cast<GLint>(mip),
                texture->type != TextureType::Texture2D ? GL_TRUE : GL_FALSE, 0, mode,
                toGLFormat(texture->format).internal);
    }

    void dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override
    {
        if (!prepareDispatch()) return;
        glDispatchCompute(x, y, z);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    void dispatchIndirect(BufferHandle handle, std::uint32_t offset) override
    {
        const GLBuffer* arguments = buffers_.get(handleCast<BufferSlot>(handle));
        if (!arguments ||
                (arguments->usage != BufferUsage::Indirect &&
                        arguments->usage != BufferUsage::Storage) ||
                offset % 4 != 0 ||
                static_cast<std::uint64_t>(offset) + sizeof(DispatchIndirectCommand) >
                        arguments->size)
        {
            log("dispatchIndirect: invalid buffer or offset");
            return;
        }
        if (!prepareDispatch()) return;
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, arguments->id);
        glDispatchComputeIndirect(offset);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    void endComputePass() override { computeActive_ = false; }

    void endRenderPass() override
    {
        if (!passActive_) return;
        if (occlusionActive_)
        {
            log("endRenderPass: an occlusion query is still open");
            endQuery(openOcclusion_);
        }
        passActive_ = false;
        if (storageUsedInPass_ && caps_.compute) glMemoryBarrier(GL_ALL_BARRIER_BITS);
        storageUsedInPass_ = false;
        if (passOffscreen_) resolvePass();

        GLenum attachments[RenderPassDesc::kMaxColorTargets + 1];
        GLsizei count = 0;
        if (passColorStore_ == StoreOp::Discard)
        {
            if (!passOffscreen_) attachments[count++] = GL_COLOR;
            for (std::uint32_t i = 0; passOffscreen_ && i < passColorCount_; ++i)
                attachments[count++] = GL_COLOR_ATTACHMENT0 + i;
        }
        if (passDepthStore_ == StoreOp::Discard && passHasDepth_)
        {
            if (!passOffscreen_) attachments[count++] = GL_DEPTH;
            else
                attachments[count++] =
                        passStencil_ ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
        }
        if (count > 0) glInvalidateFramebuffer(GL_FRAMEBUFFER, count, attachments);
    }
    void endFrame() override {}

    void present() override
    {
        leaveSurface();
        if (currentSurface_.valid() || surfacesUsed_)
        {
            platform_.makeCurrent(platform_.user);
            currentSurface_ = SwapchainHandle();
        }
        if (!mainSwapped_) platform_.swapBuffers(platform_.user);
        glFlush();
        mainSwapped_ = false;
        mainDrawn_ = false;
        frameOpen_ = false;
        ++frameNumber_;
    }

    SwapchainHandle createSwapchain(const SwapchainDesc& desc) override
    {
        if (!caps_.multipleWindows || !desc.gl.makeCurrent || !desc.gl.swapBuffers ||
                !desc.gl.framebufferSize)
        {
            log("createSwapchain: several windows are not supported, or platform functions are "
                "missing");
            return SwapchainHandle();
        }
        GLSwapchain swapchain;
        swapchain.platform = desc.gl;
        surfacesUsed_ = true;
        platform_.makeCurrent(platform_.user);
        currentSurface_ = SwapchainHandle();
        return handleCast<SwapchainHandle>(swapchains_.insert(swapchain));
    }

    void destroy(SwapchainHandle handle) override
    {
        const SwapchainSlot slot = handleCast<SwapchainSlot>(handle);
        if (!swapchains_.contains(slot)) return;
        swapchains_.erase(slot);
        platform_.makeCurrent(platform_.user);
        currentSurface_ = SwapchainHandle();
    }

    void leaveSurface()
    {
        GLSwapchain* current = swapchains_.get(handleCast<SwapchainSlot>(currentSurface_));
        if (current && current->drawn)
        {
            current->platform.swapBuffers(current->platform.user);
            current->drawn = false;
        }
        if (!currentSurface_.valid() && mainDrawn_)
        {
            platform_.swapBuffers(platform_.user);
            mainDrawn_ = false;
            mainSwapped_ = true;
        }
    }

    const GLPlatform* useSurface(SwapchainHandle handle, bool draw)
    {
        const GLPlatform* surface = &platform_;
        GLSwapchain* swapchain = nullptr;
        if (handle.valid())
        {
            swapchain = swapchains_.get(handleCast<SwapchainSlot>(handle));
            if (!swapchain) return nullptr;
            surface = &swapchain->platform;
        }
        if (!(currentSurface_ == handle))
        {
            leaveSurface();
            surface->makeCurrent(surface->user);
            currentSurface_ = handle;
        }
        if (draw && swapchain) swapchain->drawn = true;
        if (draw && !swapchain) mainDrawn_ = true;
        return surface;
    }

    bool readPixels(const RenderTarget& source, const Rect& rect, void* rgba) override
    {
        if (!rgba || !readRows(source, rect, rgba, 0)) return false;
        flipRows(static_cast<unsigned char*>(rgba), rect.width, rect.height);
        return true;
    }

    ReadbackHandle requestReadback(const RenderTarget& source, const Rect& rect) override
    {
        GLReadback readback;
        readback.width = rect.width;
        readback.height = rect.height;
        readback.bytes = rect.width * rect.height * 4;
        glGenBuffers(1, &readback.buffer);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
        glBufferData(GL_PIXEL_PACK_BUFFER,
                static_cast<GLsizeiptr>(rect.width) * static_cast<GLsizeiptr>(rect.height) * 4,
                nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (!readRows(source, rect, nullptr, readback.buffer))
        {
            glDeleteBuffers(1, &readback.buffer);
            return ReadbackHandle();
        }
        readback.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        return handleCast<ReadbackHandle>(readbacks_.insert(readback));
    }

    ReadbackHandle requestBufferReadback(BufferHandle sourceHandle, std::uint32_t offset,
            std::uint32_t size) override
    {
        const GLBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        if (passActive_ || !source || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > source->size)
        {
            log("requestBufferReadback: invalid buffer or range, or called inside a render pass");
            return ReadbackHandle();
        }
        GLReadback readback;
        readback.bytes = size;
        glGenBuffers(1, &readback.buffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, readback.buffer);
        glBufferData(GL_COPY_WRITE_BUFFER, size, nullptr, GL_STREAM_READ);
        copyFrom(*source, offset, size);
        readback.sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        return handleCast<ReadbackHandle>(readbacks_.insert(readback));
    }

    bool readBuffer(BufferHandle sourceHandle, std::uint32_t offset, std::uint32_t size,
            void* data) override
    {
        const GLBuffer* source = buffers_.get(handleCast<BufferSlot>(sourceHandle));
        if (passActive_ || !source || !data || size == 0 ||
                static_cast<std::uint64_t>(offset) + size > source->size)
        {
            log("readBuffer: invalid buffer or range, or called inside a render pass");
            return false;
        }
        if (caps_.compute) glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glBindBuffer(GL_COPY_READ_BUFFER, source->id);
        const void* mapped = glMapBufferRange(GL_COPY_READ_BUFFER, offset, size, GL_MAP_READ_BIT);
        if (!mapped) return false;
        memcpy(data, mapped, size);
        glUnmapBuffer(GL_COPY_READ_BUFFER);
        return true;
    }

    void copyFrom(const GLBuffer& source, std::uint32_t offset, std::uint32_t size)
    {
        if (caps_.compute) glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glBindBuffer(GL_COPY_READ_BUFFER, source.id);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, offset, 0, size);
    }

    bool readbackResult(ReadbackHandle handle, void* rgba) override
    {
        GLReadback* readback = readbacks_.get(handleCast<ReadbackSlot>(handle));
        if (!readback || !rgba) return false;
        if (readback->sync)
        {
            const GLenum status = glClientWaitSync(readback->sync, 0, 0);
            if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) return false;
            glDeleteSync(readback->sync);
            readback->sync = nullptr;
        }
        const GLsizeiptr bytes = readback->bytes;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, readback->buffer);
#ifdef __EMSCRIPTEN__
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, bytes, rgba);
#else
        const void* pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        if (!pixels)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            return false;
        }
        memcpy(rgba, pixels, static_cast<std::size_t>(bytes));
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
#endif
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        if (readback->height > 0)
            flipRows(static_cast<unsigned char*>(rgba), readback->width, readback->height);
        return true;
    }

    void destroy(ReadbackHandle handle) override
    {
        const ReadbackSlot slot = handleCast<ReadbackSlot>(handle);
        const GLReadback* readback = readbacks_.get(slot);
        if (!readback) return;
        if (readback->sync) glDeleteSync(readback->sync);
        glDeleteBuffers(1, &readback->buffer);
        readbacks_.erase(slot);
    }

    static void flipRows(unsigned char* pixels, std::uint32_t width, std::uint32_t height)
    {
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
        for (std::uint32_t top = 0, bottom = height - 1; top < bottom; ++top, --bottom)
            for (std::size_t i = 0; i < rowBytes; ++i)
            {
                const unsigned char swapped = pixels[top * rowBytes + i];
                pixels[top * rowBytes + i] = pixels[bottom * rowBytes + i];
                pixels[bottom * rowBytes + i] = swapped;
            }
    }

    bool readRows(const RenderTarget& source, const Rect& rect, void* rgba, GLuint packBuffer)
    {
        if (passActive_ || rect.width == 0 || rect.height == 0 || rect.x < 0 || rect.y < 0)
        {
            log("readPixels: invalid rectangle, or called inside a render pass");
            return false;
        }

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        GLuint temporary = 0;
        if (!source.texture.valid())
        {
            const GLPlatform* surface = useSurface(source.swapchain, false);
            if (!surface)
            {
                log("readPixels: invalid swapchain");
                return false;
            }
            surface->framebufferSize(surface->user, &width, &height);
            state_.bindFramebuffer(0);
        }
        else
        {
            const GLTexture* texture = textures_.get(handleCast<TextureSlot>(source.texture));
            if (!texture || texture->samples > 1 ||
                    (texture->format != TextureFormat::RGBA8 &&
                            texture->format != TextureFormat::RGBA8Srgb) ||
                    source.mip >= texture->mipLevels ||
                    source.layer >= layerCount(*texture, source.mip))
            {
                log("readPixels: the texture must be RGBA8 and the mip and layer must exist");
                return false;
            }
            width = mipSize(texture->width, source.mip);
            height = mipSize(texture->height, source.mip);
            glGenFramebuffers(1, &temporary);
            state_.bindFramebuffer(temporary);
            attach(GL_COLOR_ATTACHMENT0, *texture, source);
        }

        const bool inside = static_cast<std::uint64_t>(rect.x) + rect.width <= width &&
                            static_cast<std::uint64_t>(rect.y) + rect.height <= height;
        if (inside)
        {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            if (packBuffer) glBindBuffer(GL_PIXEL_PACK_BUFFER, packBuffer);
            glReadPixels(rect.x,
                    static_cast<GLint>(height) - (rect.y + static_cast<GLint>(rect.height)),
                    static_cast<GLsizei>(rect.width), static_cast<GLsizei>(rect.height), GL_RGBA,
                    GL_UNSIGNED_BYTE, packBuffer ? nullptr : rgba);
            if (packBuffer) glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        else
            log("readPixels: the rectangle is outside the source");

        if (temporary)
        {
            state_.bindFramebuffer(0);
            glDeleteFramebuffers(1, &temporary);
            state_.framebufferDeleted(temporary);
        }
        return inside;
    }

private:
    using BufferSlot = ct::Handle32<GLBuffer>;
    using ShaderSlot = ct::Handle32<GLShader>;
    using PipelineSlot = ct::Handle32<GLPipeline>;
    using TextureSlot = ct::Handle32<GLTexture>;
    using SamplerSlot = ct::Handle32<GLSampler>;
    using QuerySlot = ct::Handle32<GLQuery>;
    using ReadbackSlot = ct::Handle32<GLReadback>;
    using SwapchainSlot = ct::Handle32<GLSwapchain>;

    static void writeTimestamp(GLuint id)
    {
#ifdef PRISMA_GLES
        glQueryCounterEXT(id, kTimestamp);
#else
        glQueryCounter(id, kTimestamp);
#endif
    }

    static std::uint64_t readTimestamp(GLuint id)
    {
        GLuint64 value = 0;
#ifdef PRISMA_GLES
        glGetQueryObjectui64vEXT(id, GL_QUERY_RESULT, &value);
#else
        glGetQueryObjectui64v(id, GL_QUERY_RESULT, &value);
#endif
        return value;
    }

    static bool framebufferUses(const GLFramebuffer& framebuffer, std::uint32_t texture)
    {
        if (framebuffer.depth.texture == texture) return true;
        for (std::uint32_t i = 0; i < framebuffer.colorCount; ++i)
            if (framebuffer.colors[i].texture == texture) return true;
        return false;
    }


    bool supportedFormat(const TextureDesc& desc) const
    {
        const FormatFamily family = formatFamily(desc.format);
        if (family == FormatFamily::Plain) return true;
        if ((desc.usage & kTextureRenderTarget) || desc.type == TextureType::Texture3D)
            return false;
        if (family == FormatFamily::BC) return caps_.textureBC;
        if (family == FormatFamily::ETC2) return caps_.textureETC2;
        return caps_.textureASTC;
    }

    GLuint compileShader(GLenum type, const char* source, const char* name)
    {
        const GLuint id = glCreateShader(type);
        label(GL_SHADER, id, name);
        glShaderSource(id, 1, &source, nullptr);
        glCompileShader(id);
        GLint compiled = GL_FALSE;
        glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
        if (compiled) return id;
        char message[1024];
        glGetShaderInfoLog(id, sizeof(message), nullptr, message);
        log(message);
        glDeleteShader(id);
        return 0;
    }

    bool mapStorageTexture(GLuint program, const char* name, std::uint32_t slot, bool required)
    {
        const GLint location = glGetUniformLocation(program, name);
        if (location < 0)
        {
            if (required) log("createComputePipeline: storage texture not found in the shader");
            return true;
        }
#ifdef PRISMA_GLES
        GLint unit = -1;
        glGetUniformiv(program, location, &unit);
        if (unit != static_cast<GLint>(slot))
        {
            log("createComputePipeline: on OpenGL ES a storage texture must declare "
                "layout(binding = slot)");
            return false;
        }
#else
        glUniform1i(location, static_cast<GLint>(slot));
#endif
        return true;
    }

    bool applyBindings(GLuint program, const GLShader& shader)
    {
        if (shader.bindingCount == 0) return true;
        state_.useProgram(program);
        for (std::uint32_t i = 0; i < shader.bindingCount; ++i)
        {
            const GLBinding& binding = shader.bindings[i];
            switch (binding.kind)
            {
                case BindingKind::UniformBlock:
                {
                    const GLuint index = glGetUniformBlockIndex(program, binding.name);
                    if (index != GL_INVALID_INDEX)
                        glUniformBlockBinding(program, index, binding.slot);
                    break;
                }
                case BindingKind::Texture:
                {
                    const GLint location = glGetUniformLocation(program, binding.name);
                    if (location >= 0) glUniform1i(location, static_cast<GLint>(binding.slot));
                    break;
                }
                case BindingKind::StorageBuffer:
                {
                    StorageBinding block;
                    block.name = binding.name;
                    block.slot = binding.slot;
                    if (!caps_.compute || !mapStorageBuffers(program, &block, 1, false))
                        return false;
                    break;
                }
                case BindingKind::StorageTexture:
                    if (!caps_.compute ||
                            !mapStorageTexture(program, binding.name, binding.slot, false))
                        return false;
                    break;
            }
        }
        return true;
    }

    bool mapStorageBuffers(GLuint program, const StorageBinding* blocks, std::uint32_t count,
            bool required)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const GLuint index =
                    glGetProgramResourceIndex(program, GL_SHADER_STORAGE_BLOCK, blocks[i].name);
            if (index == GL_INVALID_INDEX)
            {
                if (required) log("createPipeline: storage block not found in the shaders");
                continue;
            }
#ifdef PRISMA_GLES
            const GLenum property = GL_BUFFER_BINDING;
            GLint binding = -1;
            glGetProgramResourceiv(program, GL_SHADER_STORAGE_BLOCK, index, 1, &property, 1,
                    nullptr, &binding);
            if (binding != static_cast<GLint>(blocks[i].slot))
            {
                log("createPipeline: on OpenGL ES a storage block must declare "
                    "layout(binding = slot)");
                return false;
            }
#else
            glShaderStorageBlockBinding(program, index, blocks[i].slot);
#endif
        }
        return true;
    }

    const GLBuffer* indirectBuffer(BufferHandle handle, std::uint32_t offset,
            std::uint32_t drawCount, std::uint32_t stride, std::uint32_t commandSize)
    {
        const GLBuffer* arguments = buffers_.get(handleCast<BufferSlot>(handle));
        const bool usable = caps_.indirectDraw && arguments &&
                            (arguments->usage == BufferUsage::Indirect ||
                                    arguments->usage == BufferUsage::Storage);
        if (!validIndirect(usable, usable ? arguments->size : 0, offset, drawCount, stride,
                    commandSize))
        {
            log("drawIndirect: indirect draws not supported, or invalid buffer, offset, count or "
                "stride");
            return nullptr;
        }
        return arguments;
    }

    const GLPipeline* prepareDispatch()
    {
        const GLPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!computeActive_ || !pipeline || !pipeline->compute)
        {
            log("dispatch: no active compute pass or no compute pipeline bound in this pass");
            return nullptr;
        }
        state_.useProgram(pipeline->program);
        return pipeline;
    }

    static bool hasExtension(const char* name)
    {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i)
        {
            const char* extension = reinterpret_cast<const char*>(
                    glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (!extension) continue;
            if (strcmp(extension, name) == 0) return true;
            if (strncmp(extension, "GL_", 3) == 0 && strcmp(extension + 3, name) == 0) return true;
        }
        return false;
    }

    void uploadLevel(const GLTexture& texture, std::uint32_t mip, std::uint32_t layer,
            const void* data)
    {
        const bool volume = texture.type == TextureType::Texture3D;
        upload(texture, mip, volume ? 0 : layer, 0, 0, mipSize(texture.width, mip),
                mipSize(texture.height, mip), volume ? mipSize(texture.depth, mip) : 1, data);
    }

    void upload(const GLTexture& texture, std::uint32_t mip, std::uint32_t layer, std::uint32_t x,
            std::uint32_t y, std::uint32_t regionWidth, std::uint32_t regionHeight,
            std::uint32_t slices, const void* data)
    {
        const GLFormat format = toGLFormat(texture.format);
        const bool compressed = isCompressedFormat(texture.format);
        const bool flat =
                texture.type == TextureType::Texture2D || texture.type == TextureType::TextureCube;
        const GLenum target = texture.type == TextureType::TextureCube
                                      ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer
                                      : texture.target;
        const GLsizei bytes = static_cast<GLsizei>(
                levelBytes(texture.format, regionWidth, regionHeight) * slices);
        const GLint level = static_cast<GLint>(mip);
        const GLint left = static_cast<GLint>(x);
        const GLint bottom = static_cast<GLint>(y);
        const GLint front = static_cast<GLint>(layer);
        const GLsizei width = static_cast<GLsizei>(regionWidth);
        const GLsizei height = static_cast<GLsizei>(regionHeight);
        const GLsizei depth = static_cast<GLsizei>(slices);

        bindForEdit(texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (flat && compressed)
            glCompressedTexSubImage2D(target, level, left, bottom, width, height, format.internal,
                    bytes, data);
        else if (flat)
            glTexSubImage2D(target, level, left, bottom, width, height, format.format, format.type,
                    data);
        else if (compressed)
            glCompressedTexSubImage3D(target, level, left, bottom, front, width, height, depth,
                    format.internal, bytes, data);
        else
            glTexSubImage3D(target, level, left, bottom, front, width, height, depth, format.format,
                    format.type, data);
    }

    const GLFramebuffer* findFramebuffer(const RenderPassDesc& desc) const
    {
        for (std::size_t i = 0; i < framebuffers_.size(); ++i)
        {
            const GLFramebuffer& framebuffer = framebuffers_[i];
            if (framebuffer.colorCount != desc.colorCount ||
                    !sameAttachment(framebuffer.depth, desc.depth))
                continue;
            bool same = true;
            for (std::uint32_t c = 0; c < desc.colorCount; ++c)
                if (!sameAttachment(framebuffer.colors[c], desc.colors[c])) same = false;
            if (same) return &framebuffer;
        }
        return nullptr;
    }

    const GLTexture* attachable(const RenderTarget& target, bool depth) const
    {
        const GLTexture* texture = textures_.get(handleCast<TextureSlot>(target.texture));
        if (!texture || !(texture->usage & kTextureRenderTarget) ||
                isDepthFormat(texture->format) != depth || target.mip >= texture->mipLevels ||
                target.layer >= layerCount(*texture, target.mip))
            return nullptr;
        return texture;
    }

    bool prepareResolves(const RenderPassDesc& desc, const GLFramebuffer& pass)
    {
        passFramebuffer_ = pass.id;
        depthResolve_ = 0;
        for (std::uint32_t i = 0; i < RenderPassDesc::kMaxColorTargets; ++i) colorResolves_[i] = 0;
        for (std::uint32_t i = 0; i <= desc.colorCount; ++i)
        {
            const bool isDepth = i == desc.colorCount;
            const RenderTarget& target = isDepth ? desc.depthResolve : desc.resolves[i];
            if (!target.texture.valid()) continue;

            const GLTexture* texture = attachable(target, isDepth);
            const TextureFormat format = isDepth ? pass.formats.depth : pass.formats.colors[i];
            if (!texture || pass.formats.samples < 2 || texture->samples != 1 ||
                    texture->format != format ||
                    mipSize(texture->width, target.mip) != pass.width ||
                    mipSize(texture->height, target.mip) != pass.height)
                return false;

            RenderPassDesc single;
            if (isDepth) single.depth = target;
            else
            {
                single.colors[0] = target;
                single.colorCount = 1;
            }
            const GLFramebuffer* framebuffer = findFramebuffer(single);
            if (!framebuffer) framebuffer = createFramebuffer(single);
            if (!framebuffer) return false;
            if (isDepth) depthResolve_ = framebuffer->id;
            else
                colorResolves_[i] = framebuffer->id;
        }
        return true;
    }

    void resolvePass()
    {
        bool any = depthResolve_ != 0;
        for (std::uint32_t i = 0; i < passColorCount_; ++i) any = any || colorResolves_[i] != 0;
        if (!any) return;

        state_.scissorTest(false);
        writeAllColors();
        state_.depthMask(true);
        state_.stencilWriteMask(0xFF);
        const GLint width = static_cast<GLint>(passWidth_);
        const GLint height = static_cast<GLint>(passHeight_);
        for (std::uint32_t i = 0; i < passColorCount_; ++i)
        {
            if (!colorResolves_[i]) continue;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, colorResolves_[i]);
            glReadBuffer(GL_COLOR_ATTACHMENT0 + i);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
        }
        if (depthResolve_)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthResolve_);
            glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                    passStencil_ ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
                                 : GL_DEPTH_BUFFER_BIT,
                    GL_NEAREST);
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, passFramebuffer_);
    }

    static void attach(GLenum point, const GLTexture& texture, const RenderTarget& target)
    {
        if (texture.samples > 1)
        {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, point, GL_RENDERBUFFER, texture.id);
            return;
        }
        const GLint mip = static_cast<GLint>(target.mip);
        switch (texture.type)
        {
            case TextureType::Texture2D:
                glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, texture.id, mip);
                break;
            case TextureType::TextureCube:
                glFramebufferTexture2D(GL_FRAMEBUFFER, point,
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + target.layer, texture.id, mip);
                break;
            case TextureType::Texture2DArray:
            case TextureType::Texture3D:
            case TextureType::TextureCubeArray:
                glFramebufferTextureLayer(GL_FRAMEBUFFER, point, texture.id, mip,
                        static_cast<GLint>(target.layer));
                break;
        }
    }

    const GLFramebuffer* createFramebuffer(const RenderPassDesc& desc)
    {
        if (desc.colorCount > RenderPassDesc::kMaxColorTargets ||
                desc.colorCount > caps_.maxColorTargets)
            return nullptr;

        GLFramebuffer framebuffer;
        framebuffer.colorCount = desc.colorCount;
        framebuffer.formats.window = false;
        framebuffer.formats.colorCount = desc.colorCount;

        const GLTexture* colors[RenderPassDesc::kMaxColorTargets] = {};
        for (std::uint32_t i = 0; i < desc.colorCount; ++i)
        {
            colors[i] = attachable(desc.colors[i], false);
            if (!colors[i] || (i > 0 && colors[i]->samples != colors[0]->samples)) return nullptr;
            framebuffer.formats.samples = colors[i]->samples;
            framebuffer.colors[i].texture = desc.colors[i].texture.bits();
            framebuffer.colors[i].mip = desc.colors[i].mip;
            framebuffer.colors[i].layer = desc.colors[i].layer;
            framebuffer.formats.colors[i] = colors[i]->format;
            framebuffer.width = mipSize(colors[i]->width, desc.colors[i].mip);
            framebuffer.height = mipSize(colors[i]->height, desc.colors[i].mip);
        }

        const GLTexture* depth = nullptr;
        if (desc.depth.texture.valid())
        {
            depth = attachable(desc.depth, true);
            if (!depth || (desc.colorCount > 0 && depth->samples != colors[0]->samples))
                return nullptr;
            framebuffer.formats.samples = depth->samples;
            framebuffer.depth.texture = desc.depth.texture.bits();
            framebuffer.depth.mip = desc.depth.mip;
            framebuffer.depth.layer = desc.depth.layer;
            framebuffer.width = mipSize(depth->width, desc.depth.mip);
            framebuffer.height = mipSize(depth->height, desc.depth.mip);
            framebuffer.stencil = depth->format == TextureFormat::Depth24Stencil8;
            framebuffer.formats.depth = depth->format;
        }

        glGenFramebuffers(1, &framebuffer.id);
        state_.bindFramebuffer(framebuffer.id);
        GLenum drawBuffers[RenderPassDesc::kMaxColorTargets] = { GL_NONE, GL_NONE, GL_NONE,
            GL_NONE };
        for (std::uint32_t i = 0; i < desc.colorCount; ++i)
        {
            attach(GL_COLOR_ATTACHMENT0 + i, *colors[i], desc.colors[i]);
            drawBuffers[i] = GL_COLOR_ATTACHMENT0 + i;
        }
        if (depth)
            attach(framebuffer.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, *depth,
                    desc.depth);
        glDrawBuffers(desc.colorCount > 0 ? static_cast<GLsizei>(desc.colorCount) : 1, drawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            state_.bindFramebuffer(0);
            glDeleteFramebuffers(1, &framebuffer.id);
            state_.framebufferDeleted(framebuffer.id);
            return nullptr;
        }
        framebuffers_.push_back(framebuffer);
        return &framebuffers_[framebuffers_.size() - 1];
    }

    static bool validVertexInput(const PipelineDesc& desc)
    {
        if (desc.vertexBufferCount > PipelineDesc::kMaxVertexBuffers) return false;
        for (std::uint32_t i = 0; i < desc.attributeCount && i < PipelineDesc::kMaxAttributes; ++i)
            if (desc.attributes[i].buffer >= desc.vertexBufferCount) return false;
        return true;
    }

    void rotateStreamBuffer(GLBuffer* buffer, std::uint32_t offset, std::uint32_t size)
    {
        const std::uint32_t next = (buffer->current + 1) % GLBuffer::kVersions;
        const std::uint32_t end = offset + size;
        const bool whole = offset == 0 && size == buffer->size;
        const bool created = buffer->versions[next] == 0;
        if (created) glGenBuffers(1, &buffer->versions[next]);
        glBindBuffer(GL_COPY_WRITE_BUFFER, buffer->versions[next]);
        if (created || whole)
            glBufferData(GL_COPY_WRITE_BUFFER, buffer->size, nullptr, GL_STREAM_DRAW);
        if (!whole)
        {
            glBindBuffer(GL_COPY_READ_BUFFER, buffer->versions[buffer->current]);
            if (offset > 0)
                glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, offset);
            if (end < buffer->size)
                glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, end, end,
                        buffer->size - end);
        }
        buffer->current = next;
        buffer->id = buffer->versions[next];
        buffer->updatedFrame = frameNumber_;
    }

    void bindForEdit(const GLTexture& texture)
    {
        state_.bindTexture(0, texture.target, texture.id);
        state_.activeTexture(0);
    }

    GLenum bindForEdit(const GLBuffer& buffer)
    {
        switch (buffer.usage)
        {
            case BufferUsage::Vertex:
                state_.bindArrayBuffer(buffer.id);
                return GL_ARRAY_BUFFER;
            case BufferUsage::Index:
                state_.bindVertexArray(scratchVertexArray_);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.id);
                return GL_ELEMENT_ARRAY_BUFFER;
            case BufferUsage::Uniform:
                state_.bindUniformBuffer(buffer.id);
                return GL_UNIFORM_BUFFER;
            case BufferUsage::Storage:
            case BufferUsage::Indirect:
                glBindBuffer(GL_COPY_WRITE_BUFFER, buffer.id);
                return GL_COPY_WRITE_BUFFER;
        }
        return GL_ARRAY_BUFFER;
    }

    void writeAllColors()
    {
        state_.colorMask(kColorAll);
        independentKnown_ = 0;
    }

    void applyIndependentBlend(const GLPipeline& pipeline)
    {
        state_.invalidateBlend();
        const std::uint32_t count = passFormats_.window ? 1 : passFormats_.colorCount;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const GLBlendTarget& target = pipeline.targetBlend[i];
            GLBlendTarget& cached = independentCache_[i];
            const bool known = i < independentKnown_;
            if (!known || cached.blend != target.blend)
            {
                if (target.blend) glEnablei(GL_BLEND, i);
                else
                    glDisablei(GL_BLEND, i);
                cached.blend = target.blend;
            }
            if (!known || (target.blend && memcmp(cached.func, target.func, sizeof(target.func))))
            {
                glBlendFuncSeparatei(i, target.func[0], target.func[1], target.func[2],
                        target.func[3]);
                memcpy(cached.func, target.func, sizeof(target.func));
            }
            if (!known ||
                    (target.blend &&
                            memcmp(cached.equation, target.equation, sizeof(target.equation))))
            {
                glBlendEquationSeparatei(i, target.equation[0], target.equation[1]);
                memcpy(cached.equation, target.equation, sizeof(target.equation));
            }
            if (!known || cached.mask != target.mask)
            {
                glColorMaski(i, (target.mask & 1) != 0, (target.mask & 2) != 0,
                        (target.mask & 4) != 0, (target.mask & 8) != 0);
                cached.mask = target.mask;
            }
        }
        if (count > independentKnown_) independentKnown_ = count;
    }

    const GLPipeline* prepareDraw(std::uint64_t vertexEnd, std::uint32_t instanceCount)
    {
        const GLPipeline* pipeline = pipelines_.get(handleCast<PipelineSlot>(pipeline_));
        if (!passActive_ || !pipeline || pipeline->compute || instanceCount == 0)
        {
            log("draw: no active render pass, no pipeline bound in this pass or no instances");
            return nullptr;
        }
        if (!sameTargets(pipeline->targets, passFormats_))
        {
            log("draw: the pipeline was created for different render targets than this pass");
            return nullptr;
        }

        const GLBuffer* buffers[PipelineDesc::kMaxVertexBuffers] = {};
        for (std::uint32_t i = 0; i < pipeline->vertexBufferCount; ++i)
        {
            buffers[i] = buffers_.get(handleCast<BufferSlot>(vertexBuffers_[i]));
            if (!buffers[i] || (buffers[i]->usage != BufferUsage::Vertex &&
                                       buffers[i]->usage != BufferUsage::Storage))
            {
                log("draw: a vertex buffer the pipeline needs is not bound");
                return nullptr;
            }
            const VertexBufferLayout& layout = pipeline->vertexBuffers[i];
            const std::uint64_t count =
                    layout.step == VertexStep::Instance ? instanceCount : vertexEnd;
            if (vertexOffsets_[i] + count * layout.stride > buffers[i]->size)
            {
                log("draw: vertex or instance range is outside the vertex buffer");
                return nullptr;
            }
        }

        state_.useProgram(pipeline->program);
        state_.depthTest(pipeline->depthTest);
        state_.depthMask(pipeline->depthWrite);
        if (pipeline->depthTest) state_.depthFunc(pipeline->depthFunc);
        state_.cullFace(pipeline->cull, pipeline->cullFace);
        state_.frontFace(pipeline->frontFace);
        if (pipeline->independentBlend) applyIndependentBlend(*pipeline);
        else
        {
            independentKnown_ = 0;
            state_.blend(pipeline->blend);
            if (pipeline->blend)
            {
                state_.blendFunc(pipeline->blendFunc[0], pipeline->blendFunc[1],
                        pipeline->blendFunc[2], pipeline->blendFunc[3]);
                state_.blendEquation(pipeline->blendEquation[0], pipeline->blendEquation[1]);
            }
            state_.colorMask(pipeline->colorMask);
        }
        GLState::Stencil stencil = pipeline->stencil;
        stencil.reference = stencilReference_;
        state_.stencil(stencil);
        state_.depthBias(pipeline->depthBiasConstant, pipeline->depthBiasSlope);
        state_.wireframe(pipeline->wireframe);
        state_.alphaToCoverage(pipeline->alphaToCoverage);

        state_.bindVertexArray(pipeline->vertexArray);
        if (vertexDirty_ && pipeline->vertexBinding)
        {
            for (std::uint32_t i = 0; i < pipeline->vertexBufferCount; ++i)
                glBindVertexBuffer(i, buffers[i]->id, vertexOffsets_[i],
                        static_cast<GLsizei>(pipeline->vertexBuffers[i].stride));
            vertexDirty_ = false;
        }
        if (vertexDirty_)
        {
            for (std::uint32_t i = 0; i < pipeline->attributeCount; ++i)
            {
                const VertexAttribute& attribute = pipeline->attributes[i];
                const VertexBufferLayout& layout = pipeline->vertexBuffers[attribute.buffer];
                const GLVertexFormat format = toGLVertexFormat(attribute.format);
                const std::size_t offset =
                        static_cast<std::size_t>(vertexOffsets_[attribute.buffer]) +
                        attribute.offset;
                state_.bindArrayBuffer(buffers[attribute.buffer]->id);
                if (format.integer)
                    glVertexAttribIPointer(attribute.location, format.components, format.type,
                            static_cast<GLsizei>(layout.stride),
                            reinterpret_cast<const void*>(offset));
                else
                    glVertexAttribPointer(attribute.location, format.components, format.type,
                            format.normalized, static_cast<GLsizei>(layout.stride),
                            reinterpret_cast<const void*>(offset));
            }
            vertexDirty_ = false;
        }
        if (pipeline->patchVertices && pipeline->patchVertices != patchVertices_)
        {
            glPatchParameteri(GL_PATCH_VERTICES, pipeline->patchVertices);
            patchVertices_ = pipeline->patchVertices;
        }
        return pipeline;
    }

    void log(const char* message) const
    {
        if (log_) log_(message);
    }

    void label(GLenum kind, GLuint id, const char* name) const
    {
        if (debug_ && name) glObjectLabel(kind, id, -1, name);
    }

    static void GLAPIENTRY debugMessage(GLenum, GLenum type, GLuint, GLenum, GLsizei,
            const GLchar* message, const void* user)
    {
        const char* kind = "warning";
        if (type == GL_DEBUG_TYPE_ERROR) kind = "error";
        if (type == GL_DEBUG_TYPE_PERFORMANCE) kind = "performance";

        char text[1200];
        snprintf(text, sizeof(text), "GL %s: %s", kind, message);
        static_cast<const GLDriver*>(user)->log(text);
    }

    GLPlatform platform_;
    void (*log_)(const char*);
    bool debug_;
    GLState state_;
    Caps caps_;
    GLuint scratchVertexArray_ = 0;

    ct::SlotMap32<GLBuffer> buffers_;
    ct::SlotMap32<GLShader> shaders_;
    ct::SlotMap32<GLPipeline> pipelines_;
    ct::SlotMap32<GLTexture> textures_;
    ct::SlotMap32<GLSampler> samplers_;
    ct::SlotMap32<GLQuery> queries_;
    ct::SlotMap32<GLReadback> readbacks_;
    ct::SlotMap32<GLSwapchain> swapchains_;
    SwapchainHandle currentSurface_;
    GLint patchVertices_ = 0;
    bool storageUsedInPass_ = false;
    std::uint32_t independentKnown_ = 0;
    GLBlendTarget independentCache_[TargetFormats::kMaxColors];
    bool surfacesUsed_ = false;
    bool mainDrawn_ = false;
    bool frameOpen_ = false;
    std::uint64_t frameNumber_ = 0;
    bool mainSwapped_ = false;
    std::uint64_t querySequence_ = 0;
    bool occlusionActive_ = false;
    QueryHandle openOcclusion_;
    ct::Vector<GLFramebuffer> framebuffers_;
    GLuint colorResolves_[RenderPassDesc::kMaxColorTargets] = {};
    GLuint copyFramebuffers_[2] = { 0, 0 };
    bool computeActive_ = false;
    bool copyImage_ = false;
    bool vertexBinding_ = false;
    GLuint depthResolve_ = 0;
    GLuint passFramebuffer_ = 0;

    bool passActive_ = false;
    std::uint32_t stencilReference_ = 0;
    TargetFormats passFormats_;
    std::uint32_t passWidth_ = 0;
    std::uint32_t passHeight_ = 0;
    bool passOffscreen_ = false;
    std::uint32_t passColorCount_ = 0;
    bool passHasDepth_ = false;
    bool passStencil_ = false;
    StoreOp passColorStore_ = StoreOp::Store;
    StoreOp passDepthStore_ = StoreOp::Store;

    PipelineHandle pipeline_;
    BufferHandle vertexBuffers_[PipelineDesc::kMaxVertexBuffers];
    std::uint32_t vertexOffsets_[PipelineDesc::kMaxVertexBuffers] = {};
    BufferHandle indexBuffer_;
    bool vertexDirty_ = true;
    bool indexDirty_ = true;
};

} // namespace

Driver* createGLDriver(const DriverDesc& desc, DriverError* error)
{
    const GLPlatform* gl = desc.gl;
    if (!gl || !gl->makeCurrent || !gl->swapBuffers || !gl->framebufferSize)
    {
        *error = DriverError::MissingPlatform;
        return nullptr;
    }
    if (!gl->makeCurrent(gl->user))
    {
        *error = DriverError::ContextFailed;
        return nullptr;
    }
#ifdef PRISMA_GLES
    glesSetProcAddressLoader(gl->getProcAddress);
    const bool loaded = initOpenGLExtensions();
    const int major = glESExt::majorVersion;
    const int minor = glESExt::minorVersion;
    const bool versionOk = major >= 3;
#else
    const bool loaded = initOpenGLExtensions(false);
    const int major = glExt::majorVersion;
    const int minor = glExt::minorVersion;
    const bool versionOk = major > 4 || (major == 4 && minor >= 6);
#endif
    if (!loaded)
    {
        *error = DriverError::LoaderFailed;
        return nullptr;
    }
    if (!versionOk)
    {
        *error = DriverError::VersionTooLow;
        return nullptr;
    }
    *error = DriverError::None;
    return new GLDriver(*gl, desc.log, desc.debug, major, minor);
}

} // namespace prisma
