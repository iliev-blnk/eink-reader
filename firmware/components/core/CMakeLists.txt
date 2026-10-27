# Nucleul portabil: fără antete ESP-IDF aici, ca sim/ să compileze aceleași fișiere.
# Singura excepție e inflate_rom.c (miniz din ROM); simulatorul pune zlib în locul lui.
idf_component_register(SRCS "crc32.c" "font.c" "gfx.c" "ibv.c" "fontsel.c" "layout.c" "pageidx.c" "inflate_rom.c"
                            "fonts/font_ui16.c" "fonts/font_ui12.c"
                       INCLUDE_DIRS "include")
