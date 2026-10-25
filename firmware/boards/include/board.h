// Pin map of the selected board (menuconfig: "E-ink reader board").
// Each board header defines the same BOARD_* names.
#pragma once

#include "sdkconfig.h"

#if CONFIG_READER_BOARD_DEVKIT
#include "board_devkit.h"
#else
#error "No board selected"
#endif
