# embed_font.cmake - turn a binary font into a C byte array at build time.
# Invoked via `cmake -P` so it runs on the host cmake regardless of the target
# toolchain (safe for Android/Emscripten cross builds). Expects -DSRC and -DDST.
#
#   const unsigned char ui_font_default_data[]; const unsigned int ..._size;

file(READ "${SRC}" hex HEX)
# One regex pass over the hex string: each byte "ab" -> "0xab,". Fast even for
# a ~750 KB font; far cheaper than a per-byte CMake loop.
string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")

get_filename_component(name "${SRC}" NAME)
file(WRITE "${DST}"
"/* Generated from ${name} by cmake/embed_font.cmake. Do not edit. */\n"
"const unsigned char ui_font_default_data[] = {${bytes}};\n"
"const unsigned int ui_font_default_size = sizeof(ui_font_default_data);\n")
