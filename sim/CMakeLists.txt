# PC simulator: builds the portable core and renders screens and book pages to PNG.
cmake_minimum_required(VERSION 3.16)
project(eink_sim C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)
find_package(ZLIB REQUIRED)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware)
set(CORE ${FW}/components/core)
add_executable(sim
    sim.c
    png.c
    inflate_zlib.c
    ${CORE}/crc32.c
    ${CORE}/font.c
    ${CORE}/gfx.c
    ${CORE}/ibv.c
    ${CORE}/layout.c
    ${CORE}/fontsel.c
    ${CORE}/pageidx.c
    ${CORE}/fonts/font_ui16.c
    ${CORE}/fonts/font_ui12.c
    ${FW}/main/screen_bringup.c)
target_include_directories(sim PRIVATE ${CORE}/include ${FW}/main)
target_link_libraries(sim PRIVATE ZLIB::ZLIB)
