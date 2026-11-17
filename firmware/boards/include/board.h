// Harta pinilor plăcii alese (menuconfig: "Placa cititorului e-ink").
// Fiecare antet de placă definește aceleași nume BOARD_*.
#pragma once

#include "sdkconfig.h"

#if CONFIG_READER_BOARD_DEVKIT
#include "board_devkit.h"
#elif CONFIG_READER_BOARD_PCB_V1
#include "board_pcb_v1.h"
#else
#error "Nicio placă aleasă"
#endif
