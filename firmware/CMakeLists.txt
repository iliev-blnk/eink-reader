cmake_minimum_required(VERSION 3.22)

# Board pin maps live in their own component so the custom PCB can be added
# later as a second board without touching drivers or apps.
set(EXTRA_COMPONENT_DIRS boards)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eink-reader)
