#include "TextOverlay.h"

#include "prisma/rhi/ShaderBlob.h"
#include "text.frag.h"
#include "text.vert.h"
#include "third_party/stb_easy_font.h"

#include <string.h>

namespace zenapp
{

bool TextOverlay::create(prisma::Driver* driver)
{
    ct::Vector<uint16_t> indices;
    indices.resize(static_cast<size_t>(kMaxQuads) * 6);
    for (size_t quad = 0; quad < kMaxQuads; ++quad)
    {
        const uint16_t base = static_cast<uint16_t>(quad * 4);
        const uint16_t order[6] = { base, static_cast<uint16_t>(base + 1),
            static_cast<uint16_t>(base + 2), base, static_cast<uint16_t>(base + 2),
            static_cast<uint16_t>(base + 3) };
        for (int i = 0; i < 6; ++i) indices[quad * 6 + i] = order[i];
    }

    prisma::BufferDesc desc;
    desc.usage = prisma::BufferUsage::Index;
    desc.size = static_cast<std::uint32_t>(indices.size() * sizeof(uint16_t));
    desc.data = indices.data();
    desc.debugName = "overlay indices";
    indexBuffer_ = driver->createBuffer(desc);

    desc = prisma::BufferDesc();
    desc.usage = prisma::BufferUsage::Vertex;
    desc.size = static_cast<std::uint32_t>(kMaxQuads * 4 * sizeof(Vertex));
    desc.update = prisma::BufferUpdate::Stream;
    desc.debugName = "overlay vertices";
    vertexBuffer_ = driver->createBuffer(desc);

    desc = prisma::BufferDesc();
    desc.usage = prisma::BufferUsage::Uniform;
    desc.size = sizeof(screen_);
    desc.update = prisma::BufferUpdate::Stream;
    desc.debugName = "overlay screen";
    screenBuffer_ = driver->createBuffer(desc);

    prisma::ShaderDesc vertexDesc = prisma::shaderDesc(text_vert, driver->caps());
    vertexDesc.debugName = "overlay vertex";
    prisma::ShaderDesc fragmentDesc = prisma::shaderDesc(text_frag, driver->caps());
    fragmentDesc.debugName = "overlay fragment";
    const prisma::ShaderHandle vertexShader = driver->createShader(vertexDesc);
    const prisma::ShaderHandle fragmentShader = driver->createShader(fragmentDesc);

    prisma::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = vertexShader;
    pipelineDesc.fragmentShader = fragmentShader;
    pipelineDesc.vertexBuffers[0].stride = sizeof(Vertex);
    pipelineDesc.vertexBufferCount = 1;
    pipelineDesc.attributeCount = 2;
    pipelineDesc.attributes[0].location = 0;
    pipelineDesc.attributes[0].format = prisma::VertexFormat::Float3;
    pipelineDesc.attributes[0].offset = 0;
    pipelineDesc.attributes[1].location = 1;
    pipelineDesc.attributes[1].format = prisma::VertexFormat::UByte4Norm;
    pipelineDesc.attributes[1].offset = sizeof(float) * 3;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.blend = true;
    pipelineDesc.srcColor = prisma::BlendFactor::SrcAlpha;
    pipelineDesc.dstColor = prisma::BlendFactor::OneMinusSrcAlpha;
    pipelineDesc.srcAlpha = prisma::BlendFactor::One;
    pipelineDesc.dstAlpha = prisma::BlendFactor::OneMinusSrcAlpha;
    pipelineDesc.debugName = "overlay pipeline";
    pipeline_ = driver->createPipeline(pipelineDesc);
    driver->destroy(vertexShader);
    driver->destroy(fragmentShader);

    vertices_.reserve(1024);
    return indexBuffer_.valid() && vertexBuffer_.valid() && screenBuffer_.valid() &&
           pipeline_.valid();
}

void TextOverlay::destroy(prisma::Driver* driver)
{
    driver->destroy(pipeline_);
    driver->destroy(screenBuffer_);
    driver->destroy(vertexBuffer_);
    driver->destroy(indexBuffer_);
    pipeline_ = prisma::PipelineHandle();
}

void TextOverlay::begin(unsigned width, unsigned height)
{
    vertices_.clear();
    screen_[0] = width ? 2.0f / static_cast<float>(width) : 0.0f;
    screen_[1] = height ? 2.0f / static_cast<float>(height) : 0.0f;
}

void TextOverlay::pushQuad(const Vertex* corners)
{
    if (vertices_.size() + 4 > static_cast<size_t>(kMaxQuads) * 4) return;
    for (int i = 0; i < 4; ++i) vertices_.push_back(corners[i]);
}

void TextOverlay::rect(float x, float y, float width, float height, const unsigned char* color)
{
    Vertex corners[4];
    const float xs[4] = { x, x + width, x + width, x };
    const float ys[4] = { y, y, y + height, y + height };
    for (int i = 0; i < 4; ++i)
    {
        corners[i].x = xs[i];
        corners[i].y = ys[i];
        corners[i].z = 0.0f;
        memcpy(corners[i].color, color, 4);
    }
    pushQuad(corners);
}

void TextOverlay::text(float x, float y, const char* text, const unsigned char* color, float scale)
{
    char copy[512];
    strncpy(copy, text, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    static char buffer[64 * 1024];
    unsigned char textColor[4] = { color[0], color[1], color[2], color[3] };
    const int quads = stb_easy_font_print(0.0f, 0.0f, copy, textColor, buffer, sizeof(buffer));
    const Vertex* source = reinterpret_cast<const Vertex*>(buffer);
    for (int q = 0; q < quads; ++q)
    {
        Vertex corners[4];
        for (int i = 0; i < 4; ++i)
        {
            corners[i] = source[q * 4 + i];
            corners[i].x = x + corners[i].x * scale;
            corners[i].y = y + corners[i].y * scale;
        }
        pushQuad(corners);
    }
}

void TextOverlay::upload(prisma::Driver* driver)
{
    if (vertices_.size() == 0) return;
    driver->updateBuffer(vertexBuffer_, 0, vertices_.data(),
            static_cast<std::uint32_t>(vertices_.size() * sizeof(Vertex)));
    driver->updateBuffer(screenBuffer_, 0, screen_, sizeof(screen_));
}

void TextOverlay::draw(prisma::Driver* driver)
{
    if (vertices_.size() == 0) return;
    driver->bindPipeline(pipeline_);
    driver->bindUniformBuffer(0, screenBuffer_, 0, sizeof(screen_));
    driver->bindVertexBuffer(0, vertexBuffer_, 0);
    driver->bindIndexBuffer(indexBuffer_);
    driver->drawIndexed(static_cast<std::uint32_t>(vertices_.size() / 4 * 6), 0);
}

} // namespace zenapp
