#include "std.h"
#include "gxruntime.h"

#include <coreinit/time.h>
#include <coreinit/thread.h>
#include <sysapp/launch.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

static gxRuntime *runtime;

gxRuntime *gxRuntime::openRuntime( const string &cmd_line ){
	if( runtime ) return 0;
	WHBProcInit();
	WHBLogCafeInit();
	WHBLogUdpInit();
	WHBLogPrintf( "gxRuntime: ProcUI and logging up" );
	if( !WHBGfxInit() ){
		WHBLogPrintf( "gxRuntime: WHBGfxInit failed" );
		return 0;
	}
	//present a black frame right away : the system's boot screen stays up until the first frame
	WHBGfxBeginRender();
	WHBGfxBeginRenderTV();
	WHBGfxClearColor( 0,0,0,1 );
	WHBGfxFinishRenderTV();
	WHBGfxBeginRenderDRC();
	WHBGfxClearColor( 0,0,0,1 );
	WHBGfxFinishRenderDRC();
	WHBGfxFinishRender();
	WHBLogPrintf( "gxRuntime: GX2 up, first frame presented" );
	runtime=d_new gxRuntime( cmd_line );

	//memory available to the app (malloc heap, which wut also uses for the default MEM2 heap / GPU memory)
	unsigned lo=0,hi=2048u*1024*1024;
	while( hi-lo>1024*1024 ){
		unsigned mid=lo+(hi-lo)/2;
		void *p=malloc( mid );
		if( p ){ free( p );lo=mid; }else hi=mid;
	}
	WHBLogPrintf( "gxRuntime: largest free block %u MB",lo/(1024*1024) );
	return runtime;
}

void gxRuntime::closeRuntime( gxRuntime *r ){
	if( !r || r!=runtime ) return;
	bool user_quit=!r->is_running;
	delete r;
	runtime=0;
	WHBGfxShutdown();
	if( !user_quit ){
		//the program ended by itself : return to the HOME Menu and let ProcUI finish
		SYSLaunchMenu();
		while( WHBProcIsRunning() ){}
	}
	WHBLogUdpDeinit();
	WHBLogCafeDeinit();
	WHBProcShutdown();
}

gxRuntime::gxRuntime( const string &cl ):
audio(0),input(0),graphics(0),fileSystem(0),cmd_line(cl),is_running(true),pointer_visible(true){
}

gxRuntime::~gxRuntime(){
	if( graphics ) closeGraphics( graphics );
	if( audio ) closeAudio( audio );
	if( input ) closeInput( input );
	if( fileSystem ) closeFileSystem( fileSystem );
}

bool gxRuntime::idle(){
	if( !is_running ) return false;
	if( !WHBProcIsRunning() ){
		is_running=false;
		return false;
	}
	if( input ) input->poll();
	return true;
}

bool gxRuntime::delay( int ms ){
	if( ms>0 ) OSSleepTicks( OSMillisecondsToTicks( ms ) );
	return idle();
}

void gxRuntime::flip( bool vwait ){
	if( graphics ) graphics->flip( vwait );
}

void gxRuntime::moveMouse( int x,int y ){
}

bool gxRuntime::execute( const string &cmd ){
	return false;
}

void gxRuntime::setTitle( const string &title,const string &close ){
	app_title=title;
}

int gxRuntime::getMilliSecs(){
	return (int)OSTicksToMilliseconds( OSGetSystemTime() );
}

void gxRuntime::setPointerVisible( bool vis ){
	pointer_visible=vis;
}

string gxRuntime::commandLine(){
	return cmd_line;
}

string gxRuntime::systemProperty( const string &p ){
	string t=tolower( p );
	if( t=="appdir" ) return "";
	if( t=="tempdir" || t=="systemdir" || t=="windowsdir" ) return gxFileSystem::writableRoot();
	if( t=="cpu" ) return "IBM Espresso";
	return "";
}

void gxRuntime::debugInfo( const char *t ){
	WHBLogPrintf( "blitz: %s",t );
}

void gxRuntime::debugError( const char *t ){
	WHBLogPrintf( "blitz error: %s",t );
}

void gxRuntime::debugLog( const char *t ){
	WHBLogPrintf( "%s",t );
}

// ---- graphics modes : the back buffer can be any size, it's scaled to the TV / GamePad ----

static const int MODES[][2]={ {1280,720},{1920,1080},{854,480},{1024,768},{800,600},{640,480} };
static const int N_MODES=sizeof(MODES)/sizeof(MODES[0]);

int gxRuntime::numGraphicsDrivers(){
	return 1;
}

void gxRuntime::graphicsDriverInfo( int driver,string *name,int *caps ){
	*name="GX2";
	*caps=GFXMODECAPS_3D;
}

int gxRuntime::numGraphicsModes( int driver ){
	return N_MODES;
}

void gxRuntime::graphicsModeInfo( int driver,int mode,int *w,int *h,int *d,int *caps ){
	if( mode<0 || mode>=N_MODES ) mode=0;
	*w=MODES[mode][0];*h=MODES[mode][1];*d=32;*caps=GFXMODECAPS_3D;
}

void gxRuntime::windowedModeInfo( int *caps ){
	*caps=GFXMODECAPS_3D;
}

gxAudio *gxRuntime::openAudio( int flags ){
	if( audio ) return 0;
	audio=d_new gxAudio( this );
	return audio;
}

void gxRuntime::closeAudio( gxAudio *a ){
	if( !audio || a!=audio ) return;
	delete audio;
	audio=0;
}

gxInput *gxRuntime::openInput( int flags ){
	if( input ) return 0;
	input=d_new gxInput( this );
	return input;
}

void gxRuntime::closeInput( gxInput *i ){
	if( !input || i!=input ) return;
	delete input;
	input=0;
}

gxGraphics *gxRuntime::openGraphics( int w,int h,int d,int driver,int flags ){
	if( graphics ) return 0;
	if( w<=0 || h<=0 ){ w=1280;h=720; }
	graphics=d_new gxGraphics( this,w,h );
	if( input ) input->setMouseArea( w,h );
	return graphics;
}

void gxRuntime::closeGraphics( gxGraphics *g ){
	if( !graphics || g!=graphics ) return;
	delete graphics;
	graphics=0;
}

gxFileSystem *gxRuntime::openFileSystem( int flags ){
	if( fileSystem ) return 0;
	fileSystem=d_new gxFileSystem();
	return fileSystem;
}

void gxRuntime::closeFileSystem( gxFileSystem *f ){
	if( !fileSystem || f!=fileSystem ) return;
	delete fileSystem;
	fileSystem=0;
}

gxTimer *gxRuntime::createTimer( int hertz ){
	return d_new gxTimer( this,hertz );
}

void gxRuntime::freeTimer( gxTimer *t ){
	delete t;
}
