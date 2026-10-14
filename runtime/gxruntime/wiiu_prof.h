#ifndef WIIU_PROF_H
#define WIIU_PROF_H

// Function timers : every transpiled game function (bb2cpp emits BB_PROF_SCOPE at the top of each) and some
// runtime functions measure the time spent in them, with and without the functions they call. Every 20 s the
// watchdog thread appends a report to sd:/wiiu/apps/scpcb/profile.txt (see Docs/BUILDING.md). A sampling profiler
// doesn't work on the Wii U : a suspended thread only stops at points where it waits, so busy code never shows.
//
// Cost : two OSGetSystemTime calls per call (reading the time base register directly gave inconsistent values
// in Cemu). Build with -DSCPCB_NO_PROFILE to remove.

#include <cstdint>
#include <coreinit/time.h>

int bbProfId( const char *name );

struct BBProfEntry{
	const char *name;
	uint64_t self,incl;		//time base ticks
	uint32_t calls;
};
extern BBProfEntry bb_prof[];

struct BBProfScope{
	BBProfScope *parent;
	uint64_t t0,children;
	int id;
	static BBProfScope *cur;

	BBProfScope( int i ):parent( cur ),t0( OSGetSystemTime() ),children( 0 ),id( i ){
		cur=this;
	}
	~BBProfScope(){
		uint64_t el=(uint64_t)(OSGetSystemTime()-t0);
		BBProfEntry &e=bb_prof[id];
		e.incl+=el;
		e.self+=el>children ? el-children : 0;
		++e.calls;
		if( parent ) parent->children+=el;
		cur=parent;
	}
};

#ifdef SCPCB_NO_PROFILE
#define BB_PROF_SCOPE( name )
#else
#define BB_PROF_CAT2( a,b ) a##b
#define BB_PROF_CAT( a,b ) BB_PROF_CAT2( a,b )
#define BB_PROF_SCOPE( name ) static const int BB_PROF_CAT( _prof_id,__LINE__ )=bbProfId( name ); \
	BBProfScope BB_PROF_CAT( _prof_scope,__LINE__ )( BB_PROF_CAT( _prof_id,__LINE__ ) )
#endif

namespace wiiu {
// Starts the periodic report (watchdog thread)
void profInit();
}

#endif
