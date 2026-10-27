// Aplicația de punere în funcțiune (bringup.c): MENIU ținut la o pornire la rece.
#pragma once

#include <stdbool.h>

// Cipul s-a trezit dintr-un somn început de punerea în funcțiune: trezirea revine acolo.
bool bringup_slept(void);

// Se cheamă după buttons_init(). Nu se întoarce: se termină în somnul adânc.
void bringup_run(void) __attribute__((noreturn));
