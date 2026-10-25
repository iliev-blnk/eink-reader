# Portable core: no ESP-IDF headers in here, so sim/ can build the same files.
idf_component_register(SRCS "font.c" "gfx.c" "fonts/font_ui16.c"
                       INCLUDE_DIRS "include")
