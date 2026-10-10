#include "prof.h"
#include <gint/timer.h>
#include <gint/clock.h>
#include <gint/mpu/tmu.h>

/* A TMU programmed directly: TCOR = 0xFFFFFFFF at Pphi/4, so it reloads every ~10 minutes on a
   stock calculator and never inside anything we measure. */
static int timer = -1;
static uint32_t pphi;

static int keep_running(void)
{
	return TIMER_CONTINUE;
}

void prof_init(void)
{
	for (int i = 0; i < 3 && timer < 0; i++)
		timer = timer_configure(i | TIMER_Pphi_4, 0xffffffff, GINT_CALL(keep_running));
	if (timer < 0)
		return;
	timer_start(timer);

	/* gint derives Pphi from the clock registers; that is right on the calculator (also when
	   overclocked). An implausible value means an emulator that doesn't model them: assume the
	   stock 29.4912 MHz. */
	pphi = clock_freq()->Pphi_f;
	if (pphi < 4000000 || pphi > 200000000)
		pphi = 29491200;
}

void prof_quit(void)
{
	if (timer >= 0)
		timer_stop(timer);
	timer = -1;
}

uint32_t prof_now(void)
{
	return timer >= 0 ? SH7305_TMU.TMU[timer].TCNT : 0;
}

uint32_t prof_us(uint32_t start, uint32_t end)
{
	/* the counter counts down; unsigned subtraction also covers a reload */
	uint32_t ticks = start - end;
	return (uint64_t)ticks * 4000000 / pphi;
}
