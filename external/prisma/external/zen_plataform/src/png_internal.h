#ifndef PNG_INTERNAL_H
#define PNG_INTERNAL_H

#include "platform.h"

/* Encode a Framebuffer as an RGBA8 PNG. Returns a malloc'd buffer (free with
   free), or NULL. The encoder stores the pixels uncompressed (deflate "stored"
   blocks): the file is large, but it needs no compressor and every reader
   accepts it. Meant for the clipboard, not for assets. */
uint8_t *png_encode(const Framebuffer *fb, size_t *out_size);

/* Decode a PNG (any bit depth or colour type) into a new Framebuffer of
   0xAARRGGBB pixels; free it with framebuffer_free. */
bool png_decode(const void *data, size_t size, Framebuffer *out);

#endif /* PNG_INTERNAL_H */
