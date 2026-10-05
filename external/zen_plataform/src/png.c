/*
 * png.c - the little PNG support the clipboard needs: a minimal encoder and a
 * decoder (stb_image, PNG only, compiled static so it does not clash with the copy
 * zen_ui instantiates).
 */
#include "png_internal.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR    /* HDR and the linear helpers pull in pow(): the core links no libm */
#define STBI_NO_LINEAR
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS (1 << 15)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/* ---- encoder ---- */

static uint32_t g_crc_table[256];
static bool g_crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        g_crc_table[n] = c;
    }
    g_crc_ready = true;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        crc = g_crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Writes "length type data crc" and returns the new write position. */
static uint8_t *put_chunk(uint8_t *out, const char type[4], const uint8_t *data, size_t len)
{
    put_be32(out, (uint32_t)len);
    memcpy(out + 4, type, 4);
    if (len)
        memcpy(out + 8, data, len);
    uint32_t crc = crc_update(0xFFFFFFFFu, out + 4, 4 + len) ^ 0xFFFFFFFFu;
    put_be32(out + 8 + len, crc);
    return out + 12 + len;
}

uint8_t *png_encode(const Framebuffer *fb, size_t *out_size)
{
    if (!fb || !fb->pixels || fb->width <= 0 || fb->height <= 0 || !out_size)
        return NULL;
    if (!g_crc_ready)
        crc_init();

    size_t w = (size_t)fb->width, h = (size_t)fb->height;
    size_t row = 1 + w * 4;     /* filter byte + RGBA */
    size_t raw = row * h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4; /* zlib header, stored blocks, adler32 */
    if (zlen > 0x7FFFFFFFu)
        return NULL;

    uint8_t *z = malloc(zlen);
    uint8_t *file = malloc(8 + 25 + (12 + zlen) + 12);
    if (!z || !file)
    {
        free(z);
        free(file);
        return NULL;
    }

    /* zlib stream: stored blocks over the filtered scanlines, then Adler-32. */
    uint8_t *zp = z;
    *zp++ = 0x78;
    *zp++ = 0x01;
    uint32_t a = 1, b = 0;
    size_t left = raw;
    size_t y = 0, x = 0; /* scanline and byte within it, of the raw stream */
    while (left)
    {
        size_t n = left > 65535 ? 65535 : left;
        *zp++ = (uint8_t)(left == n); /* BFINAL, BTYPE = 00 */
        *zp++ = (uint8_t)(n & 0xFF);
        *zp++ = (uint8_t)(n >> 8);
        *zp++ = (uint8_t)(~n & 0xFF);
        *zp++ = (uint8_t)((~n >> 8) & 0xFF);
        for (size_t i = 0; i < n; i++)
        {
            uint8_t v;
            if (x == 0)
                v = 0; /* filter: none */
            else
            {
                size_t px = (x - 1) / 4;
                uint32_t c = fb->pixels[y * (size_t)fb->stride + px];
                switch ((x - 1) % 4)
                {
                case 0:
                    v = (uint8_t)(c >> 16);
                    break;
                case 1:
                    v = (uint8_t)(c >> 8);
                    break;
                case 2:
                    v = (uint8_t)c;
                    break;
                default:
                    v = (uint8_t)(c >> 24);
                    break;
                }
            }
            *zp++ = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
            if (++x == row)
            {
                x = 0;
                y++;
            }
        }
        left -= n;
    }
    put_be32(zp, (b << 16) | a);
    zp += 4;

    uint8_t *p = file;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memcpy(p, sig, 8);
    p += 8;
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)w);
    put_be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 6;  /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    p = put_chunk(p, "IHDR", ihdr, 13);
    p = put_chunk(p, "IDAT", z, (size_t)(zp - z));
    p = put_chunk(p, "IEND", NULL, 0);

    free(z);
    *out_size = (size_t)(p - file);
    return file;
}

/* ---- decoder ---- */

bool png_decode(const void *data, size_t size, Framebuffer *out)
{
    if (!data || !out || size == 0 || size > 0x7FFFFFFFu)
        return false;
    int w, h, comp;
    unsigned char *rgba = stbi_load_from_memory((const unsigned char *)data, (int)size, &w, &h, &comp, 4);
    if (!rgba)
        return false;
    if (!framebuffer_alloc(out, w, h))
    {
        stbi_image_free(rgba);
        return false;
    }
    for (int i = 0; i < w * h; i++)
    {
        const unsigned char *s = rgba + (size_t)i * 4;
        out->pixels[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
    }
    stbi_image_free(rgba);
    return true;
}
