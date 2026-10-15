#include "wiiu_debug.h"

#include <coreinit/context.h>
#include <coreinit/debug.h>
#include <coreinit/exception.h>
#include <coreinit/fastmutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wiiu {

static const char *LOG_DIR="fs:/vol/external01/wiiu/apps/scpcb";
static const char *LOG_PATH="fs:/vol/external01/wiiu/apps/scpcb/log.txt";

// ---- log file ----

static FILE *log_file;
static char log_buffer[64*1024];
static bool log_dirty;
static OSFastMutex log_mutex;

static void fileLogHandler( const char *msg ){
	OSFastMutex_Lock( &log_mutex );
	if( log_file ){
		fputs( msg,log_file );
		size_t n=strlen( msg );
		if( !n || msg[n-1]!='\n' ) fputc( '\n',log_file );
		log_dirty=true;
	}
	OSFastMutex_Unlock( &log_mutex );
}

//Writes the buffered lines to the card (fsync also updates the file size, so the file is complete up to a hang
//that ends with switching the console off). Called by the watchdog thread once a second : doing it for every
//line cost ≈ 4 ms per line on the console, seconds during map generation.
static void flushLog(){
	OSFastMutex_Lock( &log_mutex );
	if( log_file && log_dirty ){
		fflush( log_file );
		fsync( fileno( log_file ) );
		log_dirty=false;
	}
	OSFastMutex_Unlock( &log_mutex );
}

// ---- stack walking ----

static bool isStackAddress( uint32_t a ){
	return a>=0x10000000 && a<0x50000000 && !(a&3);
}

// PowerPC EABI back chain : [sp] = caller's sp, [caller's sp + 4] = saved LR
static int walkStack( uint32_t sp,uint32_t *out,int max ){
	int n=0;
	while( n<max && isStackAddress( sp ) ){
		uint32_t next=*(uint32_t*)sp;
		if( !isStackAddress( next ) || next<=sp ) break;
		out[n++]=*(uint32_t*)(next+4);
		sp=next;
	}
	return n;
}

static int formatContext( char *buf,size_t size,const char *what,const OSContext *ctx ){
	uint32_t trace[12];
	int depth=walkStack( ctx->gpr[1],trace,12 );
	int len=snprintf( buf,size,"%s : pc %08x lr %08x\nstack",what,(unsigned)ctx->srr0,(unsigned)ctx->lr );
	for( int k=0;k<depth && len<(int)size-10;++k ){
		len+=snprintf( buf+len,size-len,"%s%08x",(k==6 ? "\n" : " "),(unsigned)trace[k] );
	}
	return len;
}

// ---- crash handler ----

static void crash( const char *type,OSContext *ctx ){
	char what[64],line[512];
	snprintf( what,sizeof(what),"%s exception (dar %08x)",type,(unsigned)ctx->dar );
	formatContext( line,sizeof(line),what,ctx );
	OSReport( "*** %s\n",line );
	for( int k=0;k<32;k+=4 ){
		OSReport( "*** r%-2d %08x %08x %08x %08x\n",k,(unsigned)ctx->gpr[k],(unsigned)ctx->gpr[k+1],
			(unsigned)ctx->gpr[k+2],(unsigned)ctx->gpr[k+3] );
	}
	char msg[640];
	snprintf( msg,sizeof(msg),"SCP: Containment Breach crashed.\n\n%s\n\nPlease photograph this screen.",line );
	OSFatal( msg );
}

static BOOL onDSI( OSContext *ctx ){ crash( "DSI",ctx );return FALSE; }
static BOOL onISI( OSContext *ctx ){ crash( "ISI",ctx );return FALSE; }
static BOOL onProgram( OSContext *ctx ){ crash( "program",ctx );return FALSE; }
static BOOL onAlignment( OSContext *ctx ){ crash( "alignment",ctx );return FALSE; }

// ---- watchdog + periodic report ----

static OSThread *game_thread;
static volatile uint32_t frames_presented;
static OSThread watchdog_thread __attribute__(( aligned( 16 ) ));
static uint8_t watchdog_stack[32*1024] __attribute__(( aligned( 16 ) ));
static volatile bool watchdog_quit;
static bool watchdog_running;
static void (*report_fn)( FILE *f,uint32_t frames,int seconds );

static const char *PROFILE_PATH="fs:/vol/external01/wiiu/apps/scpcb/profile.txt";
enum{ REPORT_SECONDS=20,HANG_SECONDS=10 };

void debugFramePresented(){
	++frames_presented;
}

void debugSetReport( void (*fn)( FILE *f,uint32_t frames,int seconds ) ){
	report_fn=fn;
}

static int watchdogMain( int,const char ** ){
	uint32_t last=frames_presented,report_frames=frames_presented;
	int quiet=0,report_count=0;
	while( !watchdog_quit ){
		OSSleepTicks( OSSecondsToTicks( 1 ) );
		flushLog();

		if( report_fn && ++report_count>=REPORT_SECONDS ){
			report_count=0;
			if( FILE *f=fopen( PROFILE_PATH,"a" ) ){
				report_fn( f,frames_presented-report_frames,REPORT_SECONDS );
				fclose( f );
			}
			report_frames=frames_presented;
		}

		if( frames_presented!=last ){
			last=frames_presented;
			quiet=0;
			continue;
		}
		if( ++quiet<HANG_SECONDS || !game_thread ) continue;
		quiet=0;

		//suspend the game thread, copy where it is, resume it, and only then log (logging takes locks the
		//game thread may hold)
		OSSuspendThread( game_thread );
		while( !OSIsThreadSuspended( game_thread ) ) OSSleepTicks( OSMillisecondsToTicks( 1 ) );
		char line[256],what[64];
		snprintf( what,sizeof(what),"watchdog: no frame for %d s, game thread",HANG_SECONDS );
		formatContext( line,sizeof(line),what,&game_thread->context );
		OSResumeThread( game_thread );
		for( char *p=line;*p;++p ) if( *p==10 ) *p=' ';
		WHBLogPrint( line );
		flushLog();
	}
	return 0;
}

void debugWatchThread( OSThread *t ){
	game_thread=t;
	if( watchdog_running ) return;
	//another core, higher priority, so it runs even when the game thread spins
	if( OSCreateThread( &watchdog_thread,watchdogMain,0,0,watchdog_stack+sizeof(watchdog_stack),
		sizeof(watchdog_stack),0,OS_THREAD_ATTRIB_AFFINITY_CPU2 ) ){
		OSSetThreadName( &watchdog_thread,"scpcb watchdog" );
		OSResumeThread( &watchdog_thread );
		watchdog_running=true;
	}
}

// ---- init ----

void debugInit(){
	OSFastMutex_Init( &log_mutex,"scpcb log" );
	mkdir( "fs:/vol/external01/wiiu",0777 );
	mkdir( "fs:/vol/external01/wiiu/apps",0777 );
	mkdir( LOG_DIR,0777 );
	log_file=fopen( LOG_PATH,"w" );
	if( log_file ){
		setvbuf( log_file,log_buffer,_IOFBF,sizeof(log_buffer) );
		WHBAddLogHandler( fileLogHandler );
	}
	if( FILE *pf=fopen( PROFILE_PATH,"w" ) ) fclose( pf );

	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES,OS_EXCEPTION_TYPE_DSI,onDSI );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES,OS_EXCEPTION_TYPE_ISI,onISI );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES,OS_EXCEPTION_TYPE_PROGRAM,onProgram );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES,OS_EXCEPTION_TYPE_ALIGNMENT,onAlignment );

	WHBLogPrintf( "scpcb: started, code at %p (link address of debugInit, for addr2line)",(void*)&debugInit );
}

void debugShutdown(){
	watchdog_quit=true;
	if( watchdog_running ) OSJoinThread( &watchdog_thread,0 );
	watchdog_running=false;
	game_thread=0;
	WHBRemoveLogHandler( fileLogHandler );
	OSFastMutex_Lock( &log_mutex );
	if( log_file ) fclose( log_file );
	log_file=0;
	OSFastMutex_Unlock( &log_mutex );
}

}
