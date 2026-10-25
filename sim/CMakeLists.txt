# PC simulator: builds the portable core and renders screens to PBM/PNG.
cmake_minimum_required(VERSION 3.16)
project(eink_sim C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware)
add_executable(sim
    sim.c
    ${FW}/components/core/font.c
    ${FW}/components/core/gfx.c
    ${FW}/components/core/fonts/font_ui16.c
    ${FW}/main/screen_bringup.c)
target_include_directories(sim PRIVATE ${FW}/components/core/include ${FW}/main)
