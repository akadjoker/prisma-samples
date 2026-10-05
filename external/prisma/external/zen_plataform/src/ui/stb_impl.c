/*
 * stb_impl.c - the single translation unit that instantiates the vendored stb
 * single-header libraries. Everything else includes the headers declaration-only.
 *
 * stb_truetype: glyph rasterization for the TTF font path (ui_font.c).
 * stb_image:    decode the emoji PNG atlas (ui_emoji.c).
 * Both are public domain; see the headers for their license notice.
 */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG /* the emoji atlas is PNG; keep the decoder small */
#include "stb_image.h"
