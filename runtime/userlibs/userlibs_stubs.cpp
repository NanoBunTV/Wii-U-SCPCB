// Native implementations of the userlib (.decls) functions SCP:CB calls.
//
// The prototypes must match the ones bb2cpp writes to bbgame.h (int / float / const char* parameters).
// Status : all stubs for now. FMOD streaming will use the audio mixer; BlitzMovie (intro video) is skipped;
// Win32 window / memory calls have no Wii U equivalent; zlib zip functions are only used by the update
// checker / map creator paths.
#include <cstring>

// ---- BlitzMovie.dll ----
int _fblitzmovie_open( const char *file ){ return 0; }
int _fblitzmovie_stop(){ return 0; }
int _fblitzmovie_play(){ return 0; }
int _fblitzmovie_getwidth(){ return 0; }
void _fblitzmovie_close(){}
int _fblitzmovie_getheight(){ return 0; }
int _fblitzmovie_opendecodetoimage( const char *file,int image,int flags ){ return 0; }

// ---- fmod.dll (FMOD 3.75 streams, used for music) ----
int _ffsound_stream_close( int stream ){ return 0; }
int _ffsound_stream_open( const char *file,int mode,int memlength ){ return 0; }
int _ffsound_isplaying( int channel ){ return 0; }
void _ffsound_setpaused( int channel,int paused ){}
int _ffsound_setpan( int channel,int pan ){ return 0; }
int _ffsound_stream_stop( int stream ){ return 0; }
void _ffsound_setvolume( int channel,int volume ){}
int _ffsound_stream_play( int channel,int stream ){ return -1; }
void _ffsound_stopsound( int channel ){}
int _ffsound_init( int mixrate,int maxchannels,int flags ){ return 1; }

// ---- kernel32.dll / user32.dll ----
void _fglobalmemorystatus( int buf ){}
int _fapi_getmodulefilename( int module,const char *buf,int size ){ return 0; }
int _fapi_getclientrect( int hwnd,int rect ){ return 0; }
int _fapi_getdesktopwindow(){ return 0; }
int _fapi_getfocus(){ return 1; }	//the app always has focus (the HOME Menu suspends it)
int _fapi_setwindowpos( int hwnd,int after,int x,int y,int w,int h,int flags ){ return 0; }
int _fapi_setwindowlong( int hwnd,int index,int value ){ return 0; }

// ---- zlibwapi.dll ----
int _fzlibwapi_unzgetglobalinfo( int f,int info ){ return -1; }
int _fzlibwapi_crc32( int crc,int buf,int len ){ return 0; }
int _fzlibwapi_unzopen( const char *path ){ return 0; }
int _fzlibwapi_unzgetglobalcomment( int f,int buf,int size ){ return -1; }
int _fzlibwapi_uncompress( int dest,int destlen,int src,int srclen ){ return -1; }
int _fzlibwapi_compressbound( int len ){ return len+len/1000+64; }
int _fzlibwapi_adler32( int adler,int buf,int len ){ return 1; }
int _fzlibwapi_unzlocatefile( int f,const char *name,int cs ){ return -1; }
int _fzlibwapi_unzgotofirstfile( int f ){ return -1; }
int _fzlibwapi_compress2( int dest,int destlen,int src,int srclen,int level ){ return -1; }
int _fzlibwapi_unzclose( int f ){ return 0; }
int _fzlibwapi_unzgotonextfile( int f ){ return -1; }
int _fzlibwapi_zipclose( int f,const char *comment ){ return 0; }
int _fzlibwapi_unzreadcurrentfile( int f,int buf,int len ){ return -1; }
int _fzlibwapi_zipopennewfileinzip( int f,const char *name,int zfi,int ef_local,int ef_local_size,int ef_global,
	int ef_global_size,const char *comment,int method,int level ){ return -1; }
int _fzlibwapi_unzgetcurrentfileinfo( int f,int info,int name,int name_size,int extra,int extra_size,int comment,
	int comment_size ){ return -1; }
int _fzlibwapi_unzopencurrentfile( int f ){ return -1; }
int _fzlibwapi_zipopen( const char *path,int append ){ return 0; }
int _fzlibwapi_unzopencurrentfilepassword( int f,const char *password ){ return -1; }
int _fzlibwapi_zipopennewfileinzip3( int f,const char *name,int zfi,int ef_local,int ef_local_size,int ef_global,
	int ef_global_size,const char *comment,int method,int level,int raw,int window_bits,int mem_level,int strategy,
	const char *password,int crc ){ return -1; }
int _fzlibwapi_zipwritefileinzip( int f,int buf,int len ){ return -1; }
int _fzlibwapi_unzclosecurrentfile( int f ){ return 0; }
int _fzlibwapi_zipclosefileinzip( int f ){ return 0; }
