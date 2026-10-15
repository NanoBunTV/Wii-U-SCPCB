// Native implementations of the userlib (.decls) functions SCP:CB calls.
//
// The prototypes must match the ones bb2cpp writes to bbgame.h (int / float / const char* parameters).
// FMOD streams play through gxAudio (see below); BlitzMovie (intro video) is skipped;
// Win32 window / memory calls have no Wii U equivalent; zlib zip functions are only used by the update
// checker / map creator paths.
#include <cstring>
#include <string>
#include <map>
#include "../gxruntime/gxaudio.h"
#include "../gxruntime/gxruntime.h"
#include "../gxruntime/wiiu_keyboard.h"

extern gxAudio *gx_audio;		//bbruntime/bbaudio.cpp
extern gxRuntime *gx_runtime;	//bbruntime/bbsys.cpp

// ---- wiiu.decls (the port's own helpers) ----
const char *_fwiiu_peekstring( int data,int offset,int length ){
	static std::string s;
	s.assign( (const char*)data+offset,length>0 ? length : 0 );
	return s.c_str();
}

//text boxes (Menu.bb InputBox) : the system software keyboard over the current picture
const char *_fwiiu_textinput( const char *text,const char *hint,int max_length ){
	static std::string s;
	s=text ? text : "";
	std::string out;
	if( gx_runtime && wiiu::keyboardInput( gx_runtime->graphics,s,hint ? hint : "",max_length,&out ) ) s=out;
	return s.c_str();
}

// ---- BlitzMovie.dll ----
int _fblitzmovie_open( const char *file ){ return 0; }
int _fblitzmovie_stop(){ return 0; }
int _fblitzmovie_play(){ return 0; }
int _fblitzmovie_getwidth(){ return 0; }
void _fblitzmovie_close(){}
int _fblitzmovie_getheight(){ return 0; }
int _fblitzmovie_opendecodetoimage( const char *file,int image,int flags ){ return 0; }

// ---- fmod.dll (FMOD 3.75 streams, used for music and long sounds : StrictLoads.bb) ----
// Streams are gxAudio streams (Ogg Vorbis ring buffers on AX voices). A stream handle is the gxAudio::Stream
// pointer; FMOD channel numbers are ids (from 1 : the game treats 0 and -1 as failures) mapped to the stream's
// channel. Mode bit 2 (FSOUND_LOOP_NORMAL) loops. Volume and pan are FMOD's 0..255 (pan -1 = stereo centre).
static std::map<int,gxChannel*> fmod_channels;
static std::map<int,gxAudio::Stream*> fmod_channel_stream;
static int fmod_next_id=1;

static gxChannel *fmodChannel( int id ){
	std::map<int,gxChannel*>::iterator it=fmod_channels.find( id );
	return it!=fmod_channels.end() ? it->second : 0;
}

static void fmodForget( gxAudio::Stream *st ){
	for( std::map<int,gxAudio::Stream*>::iterator it=fmod_channel_stream.begin();it!=fmod_channel_stream.end(); ){
		if( it->second==st ){
			fmod_channels.erase( it->first );
			fmod_channel_stream.erase( it++ );
		}else ++it;
	}
}

int _ffsound_stream_close( int stream ){
	if( !gx_audio || !stream ) return 0;
	fmodForget( (gxAudio::Stream*)stream );
	gx_audio->closeStream( (gxAudio::Stream*)stream );
	return 1;
}
int _ffsound_stream_open( const char *file,int mode,int memlength ){
	if( !gx_audio ) return 0;
	return (int)gx_audio->openStream( file,(mode & 2)!=0 );
}
int _ffsound_isplaying( int channel ){
	gxChannel *c=fmodChannel( channel );
	return c && c->isPlaying() ? 1 : 0;
}
void _ffsound_setpaused( int channel,int paused ){
	if( gxChannel *c=fmodChannel( channel ) ) c->setPaused( paused!=0 );
}
int _ffsound_setpan( int channel,int pan ){
	gxChannel *c=fmodChannel( channel );
	if( !c ) return 0;
	c->setPan( pan<0 ? 0.0f : pan/127.5f-1.0f );
	return 1;
}
int _ffsound_stream_stop( int stream ){
	if( !gx_audio || !stream ) return 0;
	gx_audio->stopStream( (gxAudio::Stream*)stream );
	return 1;
}
void _ffsound_setvolume( int channel,int volume ){
	if( gxChannel *c=fmodChannel( channel ) ) c->setVolume( volume/255.0f );
}
int _ffsound_stream_play( int channel,int stream ){
	if( !gx_audio || !stream ) return -1;
	gxAudio::Stream *st=(gxAudio::Stream*)stream;
	gxChannel *c=gx_audio->playStream( st );
	if( !c ) return -1;
	fmodForget( st );		//a stream plays on one channel at a time
	int id=fmod_next_id++;
	fmod_channels[id]=c;
	fmod_channel_stream[id]=st;
	return id;
}
void _ffsound_stopsound( int channel ){
	if( gxChannel *c=fmodChannel( channel ) ) c->stop();
}
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
