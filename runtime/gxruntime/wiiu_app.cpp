#include "wiiu_app.h"
#include "wiiu_debug.h"

#include <coreinit/event.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>
#include <whb/log.h>
#include <whb/proc.h>

#include <cstdlib>
#include <malloc.h>

namespace wiiu {

enum{ GAME_STACK_SIZE=8*1024*1024 };

static OSThread game_thread __attribute__(( aligned( 16 ) ));
static int (*game_fn)();
static int game_ret;

static volatile bool release_requested;	//main -> game : pause, the system wants the screen
static volatile bool exit_requested;	//main -> game : the system is closing the app
static volatile bool game_paused;
static volatile bool game_finished;		//the program has ended (game thread waits in appWaitForExit)
static OSEvent ev_paused;				//game -> main : paused or finished
static OSEvent ev_resume;				//main -> game : back in the foreground, or closing

bool appClosing(){
	return exit_requested;
}

bool appPoll(){
	if( exit_requested ) return false;
	if( release_requested ){
		GX2DrawDone();		//the GPU must be idle before the system takes it
		game_paused=true;
		OSSignalEvent( &ev_paused );
		while( release_requested && !exit_requested ) OSWaitEvent( &ev_resume );
		game_paused=false;
		WHBLogPrintf( "app: %s",exit_requested ? "closing" : "back in the foreground" );
		if( exit_requested ) return false;
	}
	return true;
}

void appWaitForExit(){
	if( !exit_requested ) GX2DrawDone();
	game_finished=true;
	OSSignalEvent( &ev_paused );
	while( !exit_requested ) OSWaitEvent( &ev_resume );
}

static int gameThreadMain( int,const char ** ){
	game_ret=game_fn();
	if( !game_finished ){
		//game() returned without appWaitForExit (e.g. it failed to start)
		game_finished=true;
		OSSignalEvent( &ev_paused );
	}
	return game_ret;
}

int appMain( int (*game)() ){
	WHBProcInit();
	OSInitEvent( &ev_paused,FALSE,OS_EVENT_MODE_AUTO );
	OSInitEvent( &ev_resume,FALSE,OS_EVENT_MODE_AUTO );

	uint8_t *stack=(uint8_t*)memalign( 16,GAME_STACK_SIZE );
	game_fn=game;
	bool threaded=stack && OSCreateThread( &game_thread,gameThreadMain,0,0,stack+GAME_STACK_SIZE,GAME_STACK_SIZE,
		OSGetThreadPriority( OSGetCurrentThread() ),OS_THREAD_ATTRIB_AFFINITY_CPU1 );
	if( !threaded ){
		WHBLogPrintf( "app: can't create the game thread" );
		free( stack );
		WHBProcShutdown();
		return 0;
	}
	OSSetThreadName( &game_thread,"scpcb game" );
	debugWatchThread( &game_thread );
	OSResumeThread( &game_thread );

	bool menu_launched=false;
	for( ;; ){
		ProcUIStatus status=ProcUIProcessMessages( TRUE );
		if( status==PROCUI_STATUS_EXITING ) break;
		if( status==PROCUI_STATUS_RELEASE_FOREGROUND ){
			WHBLogPrintf( "app: releasing the foreground" );
			if( !game_finished ){
				release_requested=true;
				while( !game_paused && !game_finished ) OSWaitEvent( &ev_paused );
			}
			ProcUIDrawDoneRelease();
			continue;
		}
		if( status==PROCUI_STATUS_IN_FOREGROUND && release_requested ){
			release_requested=false;
			OSSignalEvent( &ev_resume );
		}
		if( game_finished && !menu_launched ){
			//the program ended by itself : back to the HOME Menu (the system then closes the app)
			WHBLogPrintf( "app: program ended, launching the HOME Menu" );
			menu_launched=true;
			SYSLaunchMenu();
		}
		OSSleepTicks( OSMillisecondsToTicks( 5 ) );
	}

	WHBLogPrintf( "app: the system is closing the app" );
	exit_requested=true;
	OSSignalEvent( &ev_resume );
	int ret=0;
	OSJoinThread( &game_thread,&ret );
	free( stack );
	WHBLogPrintf( "app: game thread finished, shutting down ProcUI" );
	WHBProcShutdown();
	return ret;
}

}
