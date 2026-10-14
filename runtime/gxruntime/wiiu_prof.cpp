#include "wiiu_prof.h"
#include "wiiu_debug.h"

#include <coreinit/time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

enum{ MAX_ENTRIES=1024 };

BBProfEntry bb_prof[MAX_ENTRIES];
static int prof_count=1;		//entry 0 : overflow bucket
BBProfScope *BBProfScope::cur;

int bbProfId( const char *name ){
	if( prof_count>=MAX_ENTRIES ) return 0;
	bb_prof[prof_count].name=name;
	return prof_count++;
}

namespace wiiu {

static int report_index;

static void report( FILE *f,uint32_t frames,int seconds ){
	//copy and reset (the game thread keeps adding : the numbers may be off by a call or two)
	int n=prof_count;
	static BBProfEntry snap[MAX_ENTRIES];
	memcpy( snap,bb_prof,n*sizeof(BBProfEntry) );
	for( int k=0;k<n;++k ){ bb_prof[k].self=bb_prof[k].incl=0;bb_prof[k].calls=0; }

	std::vector<int> order;
	for( int k=1;k<n;++k ) if( snap[k].calls ) order.push_back( k );
	std::sort( order.begin(),order.end(),[]( int a,int b ){ return snap[a].self>snap[b].self; } );

	float per=frames ? 1.0f/frames : 1.0f;
	fprintf( f,"report %d : %d s, %u frames (%.1f fps). Per frame : self ms, inclusive ms, calls%c",
		report_index++,seconds,(unsigned)frames,(float)frames/seconds,10 );
	for( size_t k=0;k<order.size() && k<80;++k ){
		const BBProfEntry &e=snap[order[k]];
		fprintf( f,"%8.2f %8.2f %8.1f  %s%c",OSTicksToMicroseconds( e.self )/1000.0f*per,
			OSTicksToMicroseconds( e.incl )/1000.0f*per,e.calls*per,e.name,10 );
	}
	fputc( 10,f );
}

void profInit(){
	debugSetReport( report );
}

}
