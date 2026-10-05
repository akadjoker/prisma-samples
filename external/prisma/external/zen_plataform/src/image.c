/*
 * image.c - BMP load/save into a Framebuffer. No third-party code: BMP is simple
 * enough to read and write by hand. Pixels are 0xAARRGGBB; a 32-bit BMP row is
 * BGRA in memory, which is exactly our little-endian pixel layout. I/O goes
 * through the portable file_* helpers.
 */
#include "platform.h"

#include <stdlib.h>
#include <string.h>

/* ---- little-endian byte access ---- */

static uint32_t rd_u32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static int32_t rd_i32(const uint8_t *p)
{
    return (int32_t)rd_u32(p);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}
static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

#define BMP_HEADER_SIZE 54 /* 14-byte file header + 40-byte info header */

bool framebuffer_load_bmp(Framebuffer *fb, const char *path)
{
    size_t size = 0;
    uint8_t *data = file_read(path, &size);
    if (!data)
        return false;

    bool ok = false;
    if (size < BMP_HEADER_SIZE || data[0] != 'B' || data[1] != 'M')
        goto done;

    uint32_t offset = rd_u32(data + 10);
    int w = rd_i32(data + 18);
    int rawh = rd_i32(data + 22);
    uint16_t bpp = rd_u16(data + 28);
    uint32_t compression = rd_u32(data + 30);

    bool top_down = rawh < 0;
    int h = top_down ? -rawh : rawh;
    if (w <= 0 || h <= 0 || compression != 0 || (bpp != 24 && bpp != 32))
        goto done;

    int bypp = bpp / 8;
    size_t row_size = (((size_t)bpp * w + 31) / 32) * 4; /* rows padded to 4 bytes */
    if (offset + row_size * h > size)
        goto done;
    if (!framebuffer_alloc(fb, w, h))
        goto done;

    const uint8_t *pix = data + offset;
    for (int y = 0; y < h; y++)
    {
        int srcrow = top_down ? y : (h - 1 - y);
        const uint8_t *row = pix + (size_t)srcrow * row_size;
        uint32_t *dst = fb->pixels + (size_t)y * fb->stride;
        for (int x = 0; x < w; x++)
        {
            const uint8_t *p = row + (size_t)x * bypp;
            uint32_t a = bpp == 32 ? p[3] : 0xFF;
            dst[x] = (a << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
    }
    ok = true;

done:
    fs_free(data);
    return ok;
}

bool framebuffer_save_bmp(const Framebuffer *fb, const char *path)
{
    if (!fb || !fb->pixels || fb->width <= 0 || fb->height <= 0)
        return false;

    int w = fb->width, h = fb->height;
    uint32_t img_size = (uint32_t)w * h * 4;
    uint32_t file_size = BMP_HEADER_SIZE + img_size;
    uint8_t *buf = calloc(1, file_size);
    if (!buf)
        return false;

    buf[0] = 'B';
    buf[1] = 'M';
    wr_u32(buf + 2, file_size);
    wr_u32(buf + 10, BMP_HEADER_SIZE);
    wr_u32(buf + 14, 40); /* info header size */
    wr_u32(buf + 18, (uint32_t)w);
    wr_u32(buf + 22, (uint32_t)(-h)); /* negative: top-down */
    wr_u16(buf + 26, 1);              /* planes */
    wr_u16(buf + 28, 32);             /* bits per pixel */
    wr_u32(buf + 30, 0);              /* BI_RGB */
    wr_u32(buf + 34, img_size);
    wr_u32(buf + 38, 2835); /* 72 DPI in pixels/metre */
    wr_u32(buf + 42, 2835);

    /* Our 0xAARRGGBB is BGRA in memory, exactly a 32-bit BMP row. */
    uint8_t *dst = buf + BMP_HEADER_SIZE;
    for (int y = 0; y < h; y++)
        memcpy(dst + (size_t)y * w * 4, fb->pixels + (size_t)y * fb->stride, (size_t)w * 4);

    bool ok = file_write(path, buf, file_size);
    free(buf);
    return ok;
}
