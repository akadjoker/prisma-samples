#include "GltfGpu.h"

#include "TextureLoader.h"

#include <math.h>
#include <string.h>

namespace zenapp
{

namespace
{

enum Role
{
    kRoleBase,
    kRoleSurface,
    kRoleNormal,
    kRoleOcclusion,
    kRoleEmissive
};

bool roleIsSrgb(int role)
{
    return role == kRoleBase || role == kRoleEmissive;
}

int textureForRole(const GltfMaterial& material, int role)
{
    switch (role)
    {
        case kRoleBase: return material.baseColorTexture;
        case kRoleSurface: return material.surfaceTexture;
        case kRoleNormal: return material.normalTexture;
        case kRoleOcclusion: return material.occlusionTexture;
        default: return material.emissiveTexture;
    }
}

prisma::TextureHandle loadModelTexture(prisma::Driver* driver, const GltfTexture& texture,
        bool srgb, const GltfGpuOptions& options)
{
    if (options.preferDds && texture.ddsPath.size() > 0)
    {
        const prisma::TextureHandle handle = loadTexture(driver, texture.ddsPath.c_str(), srgb,
                true, options.skipMips);
        if (handle.valid()) return handle;
    }
    if (texture.path.size() > 0 && !endsWith(texture.path.c_str(), ".dds"))
        return loadTexture(driver, texture.path.c_str(), srgb, true);
    if (texture.embedded.size() > 0)
        return createTextureFromImage(driver, texture.embedded.data(), texture.embedded.size(),
                srgb, true, false, "embedded");
    if (texture.path.size() > 0) return loadTexture(driver, texture.path.c_str(), srgb, true);
    return prisma::TextureHandle();
}

} // namespace

bool createGltfGpu(prisma::Driver* driver, const GltfModel& model, const GltfGpuOptions& options,
        GltfGpu* out)
{
    *out = GltfGpu();
    if (model.vertices.size() == 0 || model.indices.size() == 0) return false;

    prisma::BufferDesc bufferDesc;
    bufferDesc.usage = prisma::BufferUsage::Vertex;
    bufferDesc.size = static_cast<std::uint32_t>(model.vertices.size() * sizeof(GltfVertex));
    bufferDesc.data = model.vertices.data();
    bufferDesc.debugName = "gltf vertices";
    out->vertexBuffer = driver->createBuffer(bufferDesc);
    bufferDesc.usage = prisma::BufferUsage::Index;
    bufferDesc.size = static_cast<std::uint32_t>(model.indices.size() * sizeof(uint32_t));
    bufferDesc.data = model.indices.data();
    bufferDesc.indexFormat = prisma::IndexFormat::UInt32;
    bufferDesc.debugName = "gltf indices";
    out->indexBuffer = driver->createBuffer(bufferDesc);

    const unsigned alignment = driver->caps().uniformBufferOffsetAlignment;
    out->materialStride = (sizeof(GltfMaterialUniforms) + alignment - 1) / alignment * alignment;
    const size_t materialCount = model.materials.size() + 1;
    ct::Vector<unsigned char> bytes;
    bytes.resize(static_cast<size_t>(out->materialStride) * materialCount);
    memset(bytes.data(), 0, bytes.size());
    for (size_t i = 0; i < materialCount; ++i)
    {
        GltfMaterialUniforms uniforms;
        memset(&uniforms, 0, sizeof(uniforms));
        if (i < model.materials.size())
        {
            const GltfMaterial& m = model.materials[i];
            memcpy(uniforms.baseColor, m.baseColor, sizeof(m.baseColor));
            if (m.specularGlossiness)
            {
                uniforms.surface[0] = m.glossiness;
                memcpy(uniforms.specular, m.specular, sizeof(m.specular));
            }
            else
            {
                uniforms.surface[0] = m.metallic;
                uniforms.surface[1] = m.roughness;
            }
            uniforms.surface[3] = m.alphaCutoff;
            uniforms.specular[3] = m.normalScale;
            memcpy(uniforms.emissive, m.emissive, sizeof(m.emissive));
            uniforms.emissive[3] = m.transmission;
            uniforms.flags[0] = m.baseColorTexture >= 0 ? 1.0f : 0.0f;
            uniforms.flags[1] = m.surfaceTexture >= 0 ? 1.0f : 0.0f;
            uniforms.flags[2] = m.normalTexture >= 0 ? 1.0f : 0.0f;
            uniforms.flags[3] = m.occlusionTexture >= 0 ? 1.0f : 0.0f;
            uniforms.modes[0] = m.emissiveTexture >= 0 ? 1.0f : 0.0f;
            uniforms.modes[1] = m.specularGlossiness ? 1.0f : 0.0f;
            uniforms.modes[2] = m.doubleSided ? 1.0f : 0.0f;
            uniforms.modes[3] = m.alpha == GltfMaterial::Alpha::Mask ? 1.0f
                                : m.alpha == GltfMaterial::Alpha::Blend ? 2.0f
                                                                        : 0.0f;
        }
        else
        {
            uniforms.baseColor[0] = uniforms.baseColor[1] = uniforms.baseColor[2] = 0.8f;
            uniforms.baseColor[3] = 1.0f;
            uniforms.surface[1] = 1.0f;
            uniforms.specular[3] = 1.0f;
        }
        memcpy(bytes.data() + i * out->materialStride, &uniforms, sizeof(uniforms));
    }
    bufferDesc = prisma::BufferDesc();
    bufferDesc.usage = prisma::BufferUsage::Uniform;
    bufferDesc.size = static_cast<std::uint32_t>(bytes.size());
    bufferDesc.data = bytes.data();
    bufferDesc.debugName = "gltf materials";
    out->materialBuffer = driver->createBuffer(bufferDesc);

    float anisotropy = driver->caps().maxAnisotropy;
    anisotropy = anisotropy > options.anisotropy ? options.anisotropy : anisotropy;
    anisotropy = anisotropy < 1.0f ? 1.0f : anisotropy;
    prisma::SamplerDesc samplerDesc;
    samplerDesc.maxAnisotropy = anisotropy;
    samplerDesc.debugName = "gltf repeat";
    out->repeatSampler = driver->createSampler(samplerDesc);
    samplerDesc.addressU = prisma::AddressMode::ClampToEdge;
    samplerDesc.addressV = prisma::AddressMode::ClampToEdge;
    samplerDesc.debugName = "gltf clamp";
    out->clampSampler = driver->createSampler(samplerDesc);

    const unsigned char white[4] = { 255, 255, 255, 255 };
    prisma::TextureDesc fallbackDesc;
    fallbackDesc.width = 1;
    fallbackDesc.height = 1;
    fallbackDesc.data = white;
    fallbackDesc.debugName = "gltf fallback";
    out->fallback = driver->createTexture(fallbackDesc);

    out->textures.resize(model.textures.size());
    out->textureRepeat.resize(model.textures.size());
    ct::Vector<int> roleOf;
    roleOf.resize(model.textures.size());
    for (size_t i = 0; i < model.textures.size(); ++i) roleOf[i] = -1;
    for (const GltfMaterial& material: model.materials)
        for (int role = 0; role < GltfGpu::kTextureSlotCount; ++role)
        {
            const int index = textureForRole(material, role);
            if (index >= 0 && roleOf[index] < 0) roleOf[index] = role;
        }
    for (size_t i = 0; i < model.textures.size(); ++i)
    {
        out->textureRepeat[i] = model.textures[i].repeat;
        if (roleOf[i] < 0) continue;
        out->textures[i] = loadModelTexture(driver, model.textures[i], roleIsSrgb(roleOf[i]), options);
        if (out->textures[i].valid())
            ++out->texturesLoaded;
        else
            ++out->texturesFailed;
    }
    return out->vertexBuffer.valid() && out->indexBuffer.valid() && out->materialBuffer.valid() &&
           out->repeatSampler.valid() && out->clampSampler.valid() && out->fallback.valid();
}

void destroyGltfGpu(prisma::Driver* driver, GltfGpu* gpu)
{
    for (size_t i = 0; i < gpu->textures.size(); ++i) driver->destroy(gpu->textures[i]);
    driver->destroy(gpu->fallback);
    driver->destroy(gpu->repeatSampler);
    driver->destroy(gpu->clampSampler);
    driver->destroy(gpu->materialBuffer);
    driver->destroy(gpu->indexBuffer);
    driver->destroy(gpu->vertexBuffer);
    *gpu = GltfGpu();
}

void bindGltfMaterial(prisma::Driver* driver, const GltfGpu& gpu, const GltfModel& model,
        int material)
{
    const size_t index = material >= 0 ? static_cast<size_t>(material) : model.materials.size();
    driver->bindUniformBuffer(GltfGpu::kMaterialBlockSlot, gpu.materialBuffer,
            static_cast<std::uint32_t>(index * gpu.materialStride), sizeof(GltfMaterialUniforms));
    for (int role = 0; role < GltfGpu::kTextureSlotCount; ++role)
    {
        prisma::TextureHandle texture = gpu.fallback;
        prisma::SamplerHandle sampler = gpu.repeatSampler;
        if (material >= 0)
        {
            const int source = textureForRole(model.materials[material], role);
            if (source >= 0 && gpu.textures[source].valid())
            {
                texture = gpu.textures[source];
                if (!gpu.textureRepeat[source]) sampler = gpu.clampSampler;
            }
        }
        driver->bindTexture(GltfGpu::kTextureSlotBase + role, texture, sampler);
    }
}

void bindGltfGeometry(prisma::Driver* driver, const GltfGpu& gpu)
{
    driver->bindIndexBuffer(gpu.indexBuffer);
}

void drawGltfPrimitive(prisma::Driver* driver, const GltfGpu& gpu, const GltfPrimitive& primitive)
{
    driver->bindVertexBuffer(0, gpu.vertexBuffer,
            static_cast<std::uint32_t>(primitive.firstVertex * sizeof(GltfVertex)));
    driver->drawIndexed(primitive.indexCount, primitive.firstIndex);
}

} // namespace zenapp
