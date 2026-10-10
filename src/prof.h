// Microsecond timing: a TMU left running at Pphi/4 (about 7.4 MHz on a stock fx-CG50).
#pragma once
#include <stdint.h>

void prof_init(void);
void prof_quit(void);
// A timestamp; the counter counts down
uint32_t prof_now(void);
// Microseconds between two timestamps
uint32_t prof_us(uint32_t start, uint32_t end);
