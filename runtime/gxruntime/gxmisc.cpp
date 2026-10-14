// Small gxruntime classes : gxDir, gxTimer, gxMovie
#include "std.h"
#include "gxdir.h"
#include "gxtimer.h"
#include "gxmovie.h"

#include <coreinit/time.h>
#include <coreinit/thread.h>

// ---- gxDir ----

gxDir::gxDir( const vector<string> &e ):entries(e),next(0){
}

gxDir::~gxDir(){
}

string gxDir::getNextFile(){
	return next<entries.size() ? entries[next++] : "";
}

// ---- gxTimer ----

gxTimer::gxTimer( gxRuntime *rt,int hertz ):runtime(rt){
	period_ticks=OSSecondsToTicks( 1 )/(hertz>0 ? hertz : 1);
	next_tick=OSGetSystemTime()+period_ticks;
}

gxTimer::~gxTimer(){
}

int gxTimer::wait(){
	int64_t now=OSGetSystemTime();
	if( now<next_tick ){
		OSSleepTicks( next_tick-now );
		now=next_tick;
	}
	int n=(int)((now-next_tick)/period_ticks)+1;
	next_tick+=n*period_ticks;
	return n;
}

// ---- gxMovie (not supported) ----

gxMovie::gxMovie( gxGraphics *g ){
}

gxMovie::~gxMovie(){
}
