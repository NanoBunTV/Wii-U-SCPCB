// Wii U gxTimer : fixed-rate timer for CreateTimer / WaitTimer
#ifndef GXTIMER_H
#define GXTIMER_H

#include <cstdint>

class gxRuntime;

class gxTimer{
public:
	gxTimer( gxRuntime *rt,int hertz );
	~gxTimer();

private:
	gxRuntime *runtime;
	int64_t period_ticks;
	int64_t next_tick;

	/***** GX INTERFACE *****/
public:
	int wait();
};

#endif
