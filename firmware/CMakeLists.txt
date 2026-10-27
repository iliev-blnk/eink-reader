cmake_minimum_required(VERSION 3.22)

# Hărțile pinilor stau într-o componentă separată, ca placa proprie să se poată
# adăuga mai târziu ca a doua placă fără a atinge driverele sau aplicațiile.
set(EXTRA_COMPONENT_DIRS boards)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eink-reader)
