#pragma once

#include <ct/slotmap32.hpp>

#include <cstdint>

namespace prisma
{

enum class DriverType : std::uint8_t
{
    Null,
    OpenGL,
    Vulkan
};

enum class DriverError : std::uint8_t
{
    None,
    NotCompiled,
    MissingPlatform,
    ContextFailed,
    LoaderFailed,
    VersionTooLow
};

struct GLPlatform
{
    void* user = nullptr;
    bool (*makeCurrent)(void* user) = nullptr;
    void (*swapBuffers)(void* user) = nullptr;
    void (*framebufferSize)(void* user, std::uint32_t* width, std::uint32_t* height) = nullptr;
    void* (*getProcAddress)(const char* name) = nullptr;
};

struct VulkanPlatform
{
    void* user = nullptr;
    const char* const* (*instanceExtensions)(void* user, std::uint32_t* count) = nullptr;
    bool (*createSurface)(void* user, void* instance, std::uint64_t* surface) = nullptr;
    void (*framebufferSize)(void* user, std::uint32_t* width, std::uint32_t* height) = nullptr;
};

struct DriverDesc
{
    DriverType type = DriverType::Null;
    const GLPlatform* gl = nullptr;
    const VulkanPlatform* vulkan = nullptr;
    void (*log)(const char* message) = nullptr;
    bool debug = false;
};

struct BufferTag;
struct ShaderTag;
struct PipelineTag;
struct TextureTag;
struct SamplerTag;
struct QueryTag;
struct ReadbackTag;
struct SwapchainTag;

using BufferHandle = ct::Handle32<BufferTag>;
using ShaderHandle = ct::Handle32<ShaderTag>;
using PipelineHandle = ct::Handle32<PipelineTag>;
using TextureHandle = ct::Handle32<TextureTag>;
using SamplerHandle = ct::Handle32<SamplerTag>;
using QueryHandle = ct::Handle32<QueryTag>;
using ReadbackHandle = ct::Handle32<ReadbackTag>;
using SwapchainHandle = ct::Handle32<SwapchainTag>;

struct SwapchainDesc
{
    GLPlatform gl;
    VulkanPlatform vulkan;
};

enum class QueryType : std::uint8_t
{
    Occlusion,
    Time
};

enum class BufferUsage : std::uint8_t
{
    Vertex,
    Index,
    Uniform,
    Storage,
    Indirect
};

enum class IndexFormat : std::uint8_t
{
    UInt16,
    UInt32
};

enum class BufferUpdate : std::uint8_t
{
    Static,
    Dynamic,
    Stream
};

struct BufferDesc
{
    BufferUsage usage = BufferUsage::Vertex;
    std::uint32_t size = 0;
    const void* data = nullptr;
    BufferUpdate update = BufferUpdate::Static;
    IndexFormat indexFormat = IndexFormat::UInt16;
    const char* debugName = nullptr;
};

enum class TextureFormat : std::uint8_t
{
    None,
    R8,
    RG8,
    RGBA8,
    RGBA8Srgb,
    RGB10A2,
    RGBA16F,
    R11G11B10F,
    R16F,
    RG16F,
    R32F,
    RG32F,
    RGBA32F,
    R32UInt,
    Depth32F,
    Depth24Stencil8,
    BC1,
    BC1Srgb,
    BC2,
    BC2Srgb,
    BC3,
    BC3Srgb,
    BC4,
    BC5,
    BC6H,
    BC7,
    BC7Srgb,
    ETC2RGB8,
    ETC2RGB8Srgb,
    ETC2RGBA8,
    ETC2RGBA8Srgb,
    EACR11,
    EACRG11,
    ASTC4x4,
    ASTC4x4Srgb,
    ASTC6x6,
    ASTC6x6Srgb,
    ASTC8x8,
    ASTC8x8Srgb
};

enum TextureUsage : std::uint32_t
{
    kTextureSampled = 1u << 0,
    kTextureRenderTarget = 1u << 1,
    kTextureStorage = 1u << 2
};

enum class TextureType : std::uint8_t
{
    Texture2D,
    Texture2DArray,
    TextureCube,
    Texture3D,
    TextureCubeArray
};

struct TextureDesc
{
    TextureType type = TextureType::Texture2D;
    TextureFormat format = TextureFormat::RGBA8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t samples = 1;
    std::uint32_t usage = kTextureSampled;
    const void* data = nullptr;
    bool generateMipmaps = false;
    const char* debugName = nullptr;
};

struct TextureRegion
{
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct TextureCopy
{
    TextureHandle source;
    std::uint32_t sourceMip = 0;
    std::uint32_t sourceLayer = 0;
    std::uint32_t sourceX = 0;
    std::uint32_t sourceY = 0;
    TextureHandle destination;
    std::uint32_t destinationMip = 0;
    std::uint32_t destinationLayer = 0;
    std::uint32_t destinationX = 0;
    std::uint32_t destinationY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class CompareOp : std::uint8_t
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class Filter : std::uint8_t
{
    Nearest,
    Linear
};

enum class MipFilter : std::uint8_t
{
    None,
    Nearest,
    Linear
};

enum class AddressMode : std::uint8_t
{
    Repeat,
    MirroredRepeat,
    ClampToEdge
};

struct SamplerDesc
{
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    MipFilter mipFilter = MipFilter::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    bool compare = false;
    CompareOp compareOp = CompareOp::LessEqual;
    float maxAnisotropy = 1.0f;
    const char* debugName = nullptr;
};

enum class ShaderStage : std::uint8_t
{
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEval
};

enum class BindingKind : std::uint8_t
{
    UniformBlock,
    Texture,
    StorageBuffer,
    StorageTexture
};

struct ShaderBinding
{
    BindingKind kind = BindingKind::UniformBlock;
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

struct ShaderDesc
{
    enum : std::uint32_t
    {
        kMaxBindings = 24
    };

    ShaderStage stage = ShaderStage::Vertex;
    const char* source = nullptr;
    const char* innerSource = nullptr;
    const void* spirv = nullptr;
    std::uint32_t spirvSize = 0;
    const ShaderBinding* bindings = nullptr;
    std::uint32_t bindingCount = 0;
    const char* debugName = nullptr;
};

enum class VertexFormat : std::uint8_t
{
    Float1,
    Float2,
    Float3,
    Float4,
    Half2,
    Half4,
    UByte4Norm,
    Byte4Norm,
    UShort2Norm,
    UShort4Norm,
    Short2Norm,
    Short4Norm,
    Int1010102Norm,
    UByte4,
    UShort4
};

enum class VertexStep : std::uint8_t
{
    Vertex,
    Instance
};

struct VertexBufferLayout
{
    std::uint32_t stride = 0;
    VertexStep step = VertexStep::Vertex;
};

struct VertexAttribute
{
    std::uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
    std::uint32_t buffer = 0;
};

enum class Topology : std::uint8_t
{
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
    Points,
    LinesAdjacency,
    LineStripAdjacency,
    TrianglesAdjacency,
    TriangleStripAdjacency,
    Patches
};

enum class CullMode : std::uint8_t
{
    None,
    Front,
    Back
};

enum class FrontFace : std::uint8_t
{
    CounterClockwise,
    Clockwise
};

enum class BlendFactor : std::uint8_t
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusDstColor,
    DstAlpha,
    OneMinusDstAlpha
};

enum class StencilOp : std::uint8_t
{
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
};

struct StencilFace
{
    CompareOp compare = CompareOp::Always;
    StencilOp failOp = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp passOp = StencilOp::Keep;
};

enum class BlendOp : std::uint8_t
{
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

enum ColorMask : std::uint8_t
{
    kColorRed = 1u << 0,
    kColorGreen = 1u << 1,
    kColorBlue = 1u << 2,
    kColorAlpha = 1u << 3,
    kColorAll = 15
};

struct UniformBlockBinding
{
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

struct TargetFormats
{
    enum : std::uint32_t
    {
        kMaxColors = 4
    };

    bool window = true;
    std::uint32_t samples = 1;
    TextureFormat colors[kMaxColors] = { TextureFormat::None, TextureFormat::None,
        TextureFormat::None, TextureFormat::None };
    std::uint32_t colorCount = 0;
    TextureFormat depth = TextureFormat::None;
};

struct TextureBinding
{
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

struct StorageBinding
{
    const char* name = nullptr;
    std::uint32_t slot = 0;
};

enum class StorageAccess : std::uint8_t
{
    Read,
    Write,
    ReadWrite
};

struct DrawIndirectCommand
{
    std::uint32_t vertexCount = 0;
    std::uint32_t instanceCount = 1;
    std::uint32_t firstVertex = 0;
    std::uint32_t firstInstance = 0;
};

struct DrawIndexedIndirectCommand
{
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 1;
    std::uint32_t firstIndex = 0;
    std::int32_t baseVertex = 0;
    std::uint32_t firstInstance = 0;
};

struct DispatchIndirectCommand
{
    std::uint32_t x = 1;
    std::uint32_t y = 1;
    std::uint32_t z = 1;
};

struct ComputePipelineDesc
{
    enum : std::uint32_t
    {
        kMaxUniformBlocks = 12,
        kMaxTextures = 16,
        kMaxStorageBuffers = 4,
        kMaxStorageTextures = 4
    };

    ShaderHandle shader;
    UniformBlockBinding uniformBlocks[kMaxUniformBlocks];
    std::uint32_t uniformBlockCount = 0;
    TextureBinding textures[kMaxTextures];
    std::uint32_t textureCount = 0;
    StorageBinding storageBuffers[kMaxStorageBuffers];
    std::uint32_t storageBufferCount = 0;
    StorageBinding storageTextures[kMaxStorageTextures];
    std::uint32_t storageTextureCount = 0;
    const char* debugName = nullptr;
};

struct BlendState
{
    bool blend = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendOp alphaBlendOp = BlendOp::Add;
    std::uint8_t colorMask = kColorAll;
};

struct PipelineDesc
{
    enum : std::uint32_t
    {
        kMaxAttributes = 16,
        kMaxVertexBuffers = 4,
        kMaxUniformBlocks = 12,
        kMaxTextures = 16,
        kMaxStorageBuffers = 4
    };

    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    ShaderHandle geometryShader;
    ShaderHandle tessControlShader;
    ShaderHandle tessEvalShader;
    std::uint32_t patchControlPoints = 0;
    VertexAttribute attributes[kMaxAttributes];
    std::uint32_t attributeCount = 0;
    VertexBufferLayout vertexBuffers[kMaxVertexBuffers];
    std::uint32_t vertexBufferCount = 0;
    Topology topology = Topology::Triangles;
    TargetFormats targets;

    UniformBlockBinding uniformBlocks[kMaxUniformBlocks];
    std::uint32_t uniformBlockCount = 0;

    TextureBinding textures[kMaxTextures];
    std::uint32_t textureCount = 0;
    StorageBinding storageBuffers[kMaxStorageBuffers];
    std::uint32_t storageBufferCount = 0;

    bool depthTest = false;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::Less;

    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;

    bool blend = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendOp alphaBlendOp = BlendOp::Add;
    std::uint8_t colorMask = kColorAll;
    bool independentBlend = false;
    BlendState targetBlend[TargetFormats::kMaxColors];

    bool stencilTest = false;
    StencilFace stencilFront;
    StencilFace stencilBack;
    std::uint8_t stencilReadMask = 0xFF;
    std::uint8_t stencilWriteMask = 0xFF;

    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    bool wireframe = false;
    bool alphaToCoverage = false;

    const char* debugName = nullptr;
};

struct Viewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

struct Rect
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

enum class LoadOp : std::uint8_t
{
    Load,
    Clear,
    DontCare
};

enum class StoreOp : std::uint8_t
{
    Store,
    Discard
};

struct RenderTarget
{
    TextureHandle texture;
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
    SwapchainHandle swapchain;
};

struct RenderPassDesc
{
    enum : std::uint32_t
    {
        kMaxColorTargets = 4
    };

    RenderTarget colors[kMaxColorTargets];
    std::uint32_t colorCount = 0;
    RenderTarget depth;
    RenderTarget resolves[kMaxColorTargets];
    RenderTarget depthResolve;
    SwapchainHandle swapchain;

    LoadOp colorLoad = LoadOp::Clear;
    LoadOp depthLoad = LoadOp::Clear;
    LoadOp stencilLoad = LoadOp::Clear;
    StoreOp colorStore = StoreOp::Store;
    StoreOp depthStore = StoreOp::Store;
    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float clearDepth = 1.0f;
    std::uint32_t clearStencil = 0;
};

} // namespace prisma
