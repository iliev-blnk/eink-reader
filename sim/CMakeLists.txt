# Simulatorul de pe PC: compilează nucleul portabil și desenează ecrane și pagini de carte în PNG.
cmake_minimum_required(VERSION 3.16)
project(eink_sim C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)
find_package(ZLIB REQUIRED)
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../firmware)
set(CORE ${FW}/components/core)
set(UI ${FW}/components/ui)
add_executable(sim
    sim.c
    sim_ui.c
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
    ${UI}/ui.c
    ${UI}/perflog.c
    ${UI}/settings.c
    ${UI}/sidecar.c
    ${UI}/app_library.c
    ${UI}/app_reader.c
    ${FW}/main/screen_bringup.c)
target_include_directories(sim PRIVATE ${CORE}/include ${UI}/include ${UI} ${FW}/main)
target_link_libraries(sim PRIVATE ZLIB::ZLIB)
