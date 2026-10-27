# Portable core: no ESP-IDF headers in here, so sim/ can build the same files.
# The one exception is inflate_rom.c (the ROM's miniz); the sim swaps in zlib.
idf_component_register(SRCS "crc32.c" "font.c" "gfx.c" "ibv.c" "fontsel.c" "layout.c" "pageidx.c" "inflate_rom.c"
                            "fonts/font_ui16.c" "fonts/font_ui12.c"
                       INCLUDE_DIRS "include")
