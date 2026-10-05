#pragma once

#include "prisma/rhi/Driver.h"

#include <ct/vector.hpp>

#include <stdint.h>

namespace zenapp
{

class TextOverlay
{
public:
    enum
    {
        kMaxQuads = 8192
    };

    bool create(prisma::Driver* driver);
    void destroy(prisma::Driver* driver);

    void begin(unsigned width, unsigned height);
    void rect(float x, float y, float width, float height, const unsigned char* color);
    void text(float x, float y, const char* text, const unsigned char* color, float scale = 2.0f);
    void upload(prisma::Driver* driver);
    void draw(prisma::Driver* driver);

private:
    struct Vertex
    {
        float x, y, z;
        unsigned char color[4];
    };

    void pushQuad(const Vertex* corners);

    prisma::BufferHandle vertexBuffer_;
    prisma::BufferHandle indexBuffer_;
    prisma::BufferHandle screenBuffer_;
    prisma::PipelineHandle pipeline_;
    ct::Vector<Vertex> vertices_;
    float screen_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

} // namespace zenapp
