#include "gba.h"
void timer_init(void) {
  /* TM0: prescaler 1024 -> 16384 Hz. TM1 cascades on TM0 overflow,
     so timer_ticks() is a 32-bit tick at ~16384 Hz. */
  REG_TM0DATA = 0;
  REG_TM1DATA = 0;
  REG_TM0CNT = (3) | (1 << 7);
  REG_TM1CNT = (1 << 2) | (1 << 7);
}
u32 timer_ticks(void) {
  u32 hi1, lo, hi2;
  do {
    hi1 = REG_TM1DATA;
    lo = REG_TM0DATA;
    hi2 = REG_TM1DATA;
  } while (hi1 != hi2);
  return (hi1 << 16) | lo;
}
