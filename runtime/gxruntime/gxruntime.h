// Wii U gxRuntime : application lifecycle (ProcUI), time, and the factory for the graphics / audio / input /
// file system drivers. Same interface as the original Win32 gxRuntime.
#ifndef GXRUNTIME_H
#define GXRUNTIME_H

#include <string>
#include <vector>

#include "gxaudio.h"
#include "gxinput.h"
#include "gxgraphics.h"
#include "gxfilesystem.h"
#include "gxtimer.h"

class gxRuntime{
	/***** INTERNAL INTERFACE *****/
public:
	gxAudio *audio;
	gxInput *input;
	gxGraphics *graphics;
	gxFileSystem *fileSystem;

	void flip( bool vwait );
	void moveMouse( int x,int y );

	// True while the app has the foreground; false once the user asked to quit
	bool running()const{ return is_running; }

private:
	gxRuntime( const std::string &cmd_line );
	~gxRuntime();

	std::string cmd_line;
	std::string app_title;
	bool is_running;
	bool pointer_visible;

	/***** APP INTERFACE *****/
public:
	static gxRuntime *openRuntime( const std::string &cmd_line );
	static void closeRuntime( gxRuntime *runtime );

	void asyncStop(){}
	void asyncRun(){}
	void asyncEnd(){}

	/***** GX INTERFACE *****/
public:
	enum{
		GFXMODECAPS_3D=1
	};

	//return true if program should continue, or false for quit.
	bool idle();
	bool delay( int ms );

	bool execute( const std::string &cmd );
	void setTitle( const std::string &title,const std::string &close );
	int  getMilliSecs();
	void setPointerVisible( bool vis );

	std::string commandLine();

	std::string systemProperty( const std::string &t );

	void debugStop(){}
	void debugProfile( int per ){}
	void debugStmt( int pos,const char *file ){}
	void debugEnter( void *frame,void *env,const char *func ){}
	void debugLeave(){}
	void debugInfo( const char *t );
	void debugError( const char *t );
	void debugLog( const char *t );

	int numGraphicsDrivers();
	void graphicsDriverInfo( int driver,std::string *name,int *caps );

	int numGraphicsModes( int driver );
	void graphicsModeInfo( int driver,int mode,int *w,int *h,int *d,int *caps );

	void windowedModeInfo( int *caps );

	gxAudio *openAudio( int flags );
	void closeAudio( gxAudio *audio );

	gxInput *openInput( int flags );
	void closeInput( gxInput *input );

	gxGraphics *openGraphics( int w,int h,int d,int driver,int flags );
	void closeGraphics( gxGraphics *graphics );
	bool graphicsLost(){ return false; }

	gxFileSystem *openFileSystem( int flags );
	void closeFileSystem( gxFileSystem *filesys );

	gxTimer *createTimer( int hertz );
	void freeTimer( gxTimer *timer );

	void enableDirectInput( bool use ){}
	int  directInputEnabled(){ return 0; }

	int callDll( const std::string &dll,const std::string &func,const void *in,int in_sz,void *out,int out_sz ){ return 0; }
};

#endif
