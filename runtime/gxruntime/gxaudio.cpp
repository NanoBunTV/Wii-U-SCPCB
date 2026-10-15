// Wii U gxAudio (see gxaudio.h) : AX voices play decoded sounds and stream ring buffers.
//
// Sample format : AX's LPCM16 voices read 16-bit big-endian samples, the Wii U CPU's own byte order. stb_vorbis
// decodes to native shorts (so big-endian already); WAV data is little-endian and is swapped when loaded.
//
// Mixing : each voice goes to the TV and the GamePad, channel 0 = left, 1 = right, gains from the channel's volume
// and pan (Blitz / FMOD : pan -1..1, the far side fades out, the centre plays both sides at full volume). AX
// resamples each voice from its sample rate (or the channel's pitch) to the 48 kHz renderer.
//
// Threads : the game thread calls the gx interface; a thread on CPU0 refills the stream rings every 5 ms. One mutex
// guards channels, voices and streams. All voices use the same priority and the total is capped below
// AXGetMaxVoices(), so AX never takes a voice away from us.
#include "std.h"
#include "gxaudio.h"
#include "gxfilesystem.h"
#include "wiiu_prof.h"
#include "wiiu_app.h"

#include <coreinit/cache.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <sndcore2/core.h>
#include <sndcore2/device.h>
#include <sndcore2/voice.h>
#include <whb/log.h>
#include <malloc.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

namespace{

enum{
	VOICE_PRIORITY=15,
	MAX_POOL=4096,				//channel objects (Blitz : grows from 4096)
	RING_FRAMES=16384,			//stream ring (≈ 0.37 s at 44.1 kHz)
	RING_SAFETY=1024,			//frames kept between the play position and the write position
	DECODE_CHUNK=2048
};
const float RENDER_RATE=48000.0f;

OSMutex mutex;
struct Lock{
	Lock(){ OSLockMutex( &mutex ); }
	~Lock(){ OSUnlockMutex( &mutex ); }
};

int voices_used,voices_max;
float master_vol=1;
bool master_paused,app_paused;

}

struct AXChannel : public gxChannel{
	AXVoice *voice[2];
	int n_voices,channels;
	gxSound *sound;				//sound playing on it (0 for streams)
	gxAudio::Stream *stream;	//stream it belongs to
	float vol,pan;
	int rate;
	bool paused;

	AXChannel():n_voices(0),channels(0),sound(0),stream(0),vol(1),pan(0),rate(44100),paused(false){
		voice[0]=voice[1]=0;
	}

	/***** GX INTERFACE *****/
	void stop();
	void setPaused( bool paused );
	void setPitch( int pitch );
	void setVolume( float volume );
	void setPan( float pan );
	void set3d( const float pos[3],const float vel[3] ){}
	bool isPlaying();
};

struct gxAudio::Stream{
	FileData file;
	stb_vorbis *vb;
	int channels,rate;
	bool loop;
	int16_t *ring[2];
	uint64_t written,played,end_frame;
	uint32_t last_pos;
	bool eof,finished;
	AXChannel chan;				//its own channel (not pooled)
};

namespace{

vector<AXChannel*> channel_pool;
int next_chan;
set<gxAudio::Stream*> streams;

bool voicesRunning(){
	return !master_paused && !app_paused;
}

void applyMix( AXChannel *c ){
	float g=c->vol*master_vol;
	if( g<0 ) g=0;
	float p=c->pan<-1 ? -1 : (c->pan>1 ? 1 : c->pan);
	float l=g*(p>0 ? 1-p : 1),r=g*(p<0 ? 1+p : 1);
	for( int k=0;k<c->n_voices;++k ){
		float vl=l,vr=r;
		if( c->channels>1 ){ if( k==0 ) vr=0;else vl=0; }
		if( vl>1 ) vl=1;
		if( vr>1 ) vr=1;
		AXVoiceDeviceMixData mix[6];
		memset( mix,0,sizeof(mix) );
		mix[0].bus[0].volume=(uint16_t)(vl*0x8000);
		mix[1].bus[0].volume=(uint16_t)(vr*0x8000);
		AXVoiceBegin( c->voice[k] );
		AXSetVoiceDeviceMix( c->voice[k],AX_DEVICE_TYPE_TV,0,mix );
		AXSetVoiceDeviceMix( c->voice[k],AX_DEVICE_TYPE_DRC,0,mix );
		AXVoiceEnd( c->voice[k] );
	}
}

void applyRate( AXChannel *c ){
	float ratio=c->rate/RENDER_RATE;
	if( ratio<.01f ) ratio=.01f;
	if( ratio>3.9f ) ratio=3.9f;
	for( int k=0;k<c->n_voices;++k ){
		AXVoiceBegin( c->voice[k] );
		AXSetVoiceSrcRatio( c->voice[k],ratio );
		AXVoiceEnd( c->voice[k] );
	}
}

void applyState( AXChannel *c ){
	AXVoiceState st=!c->paused && voicesRunning() ? AX_VOICE_STATE_PLAYING : AX_VOICE_STATE_STOPPED;
	for( int k=0;k<c->n_voices;++k ){
		AXVoiceBegin( c->voice[k] );
		AXSetVoiceState( c->voice[k],st );
		AXVoiceEnd( c->voice[k] );
	}
}

void freeVoices( AXChannel *c ){
	for( int k=0;k<c->n_voices;++k ){
		AXVoiceBegin( c->voice[k] );
		AXSetVoiceState( c->voice[k],AX_VOICE_STATE_STOPPED );
		AXVoiceEnd( c->voice[k] );
		AXFreeVoice( c->voice[k] );
		c->voice[k]=0;
	}
	voices_used-=c->n_voices;
	c->n_voices=0;
	c->sound=0;
}

//one voice per channel of the data (planes), started together
bool startVoices( AXChannel *c,int16_t *const planes[2],int channels,int frames,bool loop ){
	int need=channels>1 ? 2 : 1;
	if( frames<=0 || voices_used+need>voices_max ) return false;
	AXVoice *v[2]={ 0,0 };
	for( int k=0;k<need;++k ){
		v[k]=AXAcquireVoice( VOICE_PRIORITY,0,0 );
		if( !v[k] ){
			if( k ) AXFreeVoice( v[0] );
			return false;
		}
	}
	voices_used+=need;
	c->voice[0]=v[0];c->voice[1]=v[1];
	c->n_voices=need;
	c->channels=need;
	for( int k=0;k<need;++k ){
		AXVoiceBegin( v[k] );
		AXSetVoiceType( v[k],AX_VOICE_TYPE_UNKNOWN );
		AXVoiceVeData ve={ 0x8000,0 };
		AXSetVoiceVe( v[k],&ve );
		AXVoiceOffsets o;
		memset( &o,0,sizeof(o) );
		o.dataType=AX_VOICE_FORMAT_LPCM16;
		o.loopingEnabled=loop ? AX_VOICE_LOOP_ENABLED : AX_VOICE_LOOP_DISABLED;
		o.loopOffset=0;
		o.endOffset=frames-1;
		o.currentOffset=0;
		o.data=planes[k];
		AXSetVoiceOffsets( v[k],&o );
		AXSetVoiceSrcType( v[k],AX_VOICE_SRC_TYPE_LINEAR );
		AXVoiceEnd( v[k] );
	}
	applyMix( c );
	applyRate( c );
	//start both voices in the same audio frame
	AXVoiceState st=!c->paused && voicesRunning() ? AX_VOICE_STATE_PLAYING : AX_VOICE_STATE_STOPPED;
	for( int k=0;k<need;++k ) AXVoiceBegin( v[k] );
	for( int k=0;k<need;++k ) AXSetVoiceState( v[k],st );
	for( int k=need-1;k>=0;--k ) AXVoiceEnd( v[k] );
	return true;
}

bool isPlayingLocked( AXChannel *c ){
	if( !c->n_voices ) return false;
	if( c->stream ) return !c->stream->finished;
	if( c->paused || !voicesRunning() ) return true;		//stopped by us, not finished
	if( AXIsVoiceRunning( c->voice[0] ) ) return true;
	freeVoices( c );
	return false;
}

AXChannel *allocChannel(){
	for( size_t k=0;k<channel_pool.size();++k ){
		AXChannel *c=channel_pool[next_chan];
		if( ++next_chan>=(int)channel_pool.size() ) next_chan=0;
		if( !isPlayingLocked( c ) ) return c;
	}
	if( channel_pool.size()>=MAX_POOL ) return 0;
	AXChannel *c=new AXChannel();
	channel_pool.push_back( c );
	return c;
}

int16_t *allocPlane( int frames ){
	size_t bytes=((size_t)frames*2+63)&~(size_t)63;
	int16_t *p=(int16_t*)memalign( 64,bytes );
	if( p ) memset( p,0,bytes );
	return p;
}

void flushPlane( const int16_t *p,int frames ){
	DCFlushRange( (void*)p,frames*2 );
}

// ---- streams ----

//decodes n frames into the ring (silence after the end of a non-looping stream)
void fillFrames( gxAudio::Stream *st,int n ){
	while( n>0 ){
		int pos=(int)(st->written%RING_FRAMES);
		int run=RING_FRAMES-pos;
		if( run>n ) run=n;
		if( run>DECODE_CHUNK ) run=DECODE_CHUNK;
		int got=0;
		short *bufs[2]={ st->ring[0]+pos,(st->channels>1 ? st->ring[1] : st->ring[0])+pos };
		int nc=st->channels>1 ? 2 : 1;
		if( !st->eof ){
			got=stb_vorbis_get_samples_short( st->vb,nc,bufs,run );
			if( got<=0 && st->loop ){
				stb_vorbis_seek_start( st->vb );
				got=stb_vorbis_get_samples_short( st->vb,nc,bufs,run );
			}
			if( got<=0 ){
				st->eof=true;
				st->end_frame=st->written;
			}
		}
		if( got<=0 ){
			got=run;
			for( int k=0;k<nc;++k ) memset( bufs[k],0,run*2 );
		}
		for( int k=0;k<nc;++k ) flushPlane( bufs[k],got );
		st->written+=got;
		n-=got;
	}
}

void updateStream( gxAudio::Stream *st ){
	AXChannel *c=&st->chan;
	if( st->finished || !c->n_voices ) return;
	uint32_t pos=AXGetVoiceCurrentOffsetEx( c->voice[0],st->ring[0] );
	if( pos>=RING_FRAMES ) pos=0;
	st->played+=(pos+RING_FRAMES-st->last_pos)%RING_FRAMES;
	st->last_pos=pos;
	if( st->eof && st->played>=st->end_frame ){
		st->finished=true;
		freeVoices( c );
		return;
	}
	if( st->played>st->written ) st->written=st->played;		//fell behind : skip ahead
	int64_t space=(int64_t)RING_FRAMES-RING_SAFETY-(int64_t)(st->written-st->played);
	if( space>0 ) fillFrames( st,(int)space );
}

OSThread stream_thread __attribute__(( aligned( 16 ) ));
uint8_t *stream_stack;
volatile bool stream_run;
bool initialised;

int streamThread( int,const char ** ){
	while( stream_run ){
		{
			Lock lock;
			for( set<gxAudio::Stream*>::iterator it=streams.begin();it!=streams.end();++it ) updateStream( *it );
		}
		OSSleepTicks( OSMillisecondsToTicks( 5 ) );
	}
	return 0;
}

void init(){
	if( initialised ) return;
	initialised=true;
	OSInitMutex( &mutex );
	int max=(int)AXGetMaxVoices();
	voices_max=max>24 ? max-8 : max;
	WHBLogPrintf( "audio: AX %s, %d voices (using up to %d)",AXIsInit() ? "up" : "NOT initialised",max,voices_max );
	const int STACK=256*1024;
	stream_stack=(uint8_t*)memalign( 16,STACK );
	stream_run=true;
	if( stream_stack && OSCreateThread( &stream_thread,streamThread,0,0,stream_stack+STACK,STACK,12,OS_THREAD_ATTRIB_AFFINITY_CPU0 ) ){
		OSSetThreadName( &stream_thread,"scpcb audio streams" );
		OSResumeThread( &stream_thread );
	}else{
		WHBLogPrintf( "audio: can't create the stream thread" );
		stream_run=false;
	}
}

// ---- decoding ----

//RIFF WAVE, PCM 8 / 16 bit (little-endian) -> interleaved native shorts
bool decodeWav( const unsigned char *d,size_t n,vector<int16_t> *out,int *channels,int *rate ){
	if( n<12 || memcmp( d,"RIFF",4 ) || memcmp( d+8,"WAVE",4 ) ) return false;
	int fmt=0,ch=0,bits=0;
	unsigned sr=0;
	size_t p=12;
	while( p+8<=n ){
		unsigned size=d[p+4]|(d[p+5]<<8)|(d[p+6]<<16)|(d[p+7]<<24);
		const unsigned char *c=d+p+8;
		if( !memcmp( d+p,"fmt ",4 ) && size>=16 && p+8+16<=n ){
			fmt=c[0]|(c[1]<<8);
			ch=c[2]|(c[3]<<8);
			sr=c[4]|(c[5]<<8)|(c[6]<<16)|(c[7]<<24);
			bits=c[14]|(c[15]<<8);
		}else if( !memcmp( d+p,"data",4 ) ){
			if( fmt!=1 || ch<1 || (bits!=8 && bits!=16) ) return false;
			size_t bytes=size;
			if( p+8+bytes>n ) bytes=n-(p+8);
			if( bits==16 ){
				size_t count=bytes/2;
				out->resize( count );
				for( size_t k=0;k<count;++k ) (*out)[k]=(int16_t)(c[k*2]|(c[k*2+1]<<8));
			}else{
				out->resize( bytes );
				for( size_t k=0;k<bytes;++k ) (*out)[k]=(int16_t)((c[k]-128)<<8);
			}
			*channels=ch;
			*rate=(int)sr;
			return true;
		}
		p+=8+size+(size&1);
	}
	return false;
}

}

// ---- channel ----

void AXChannel::stop(){
	Lock lock;
	if( stream ) stream->finished=true;
	freeVoices( this );
}

void AXChannel::setPaused( bool p ){
	Lock lock;
	paused=p;
	applyState( this );
}

void AXChannel::setPitch( int hz ){
	Lock lock;
	rate=hz;
	applyRate( this );
}

void AXChannel::setVolume( float v ){
	Lock lock;
	vol=v;
	applyMix( this );
}

void AXChannel::setPan( float p ){
	Lock lock;
	pan=p;
	applyMix( this );
}

bool AXChannel::isPlaying(){
	Lock lock;
	return isPlayingLocked( this );
}

gxChannel::~gxChannel(){}

// ---- gxSound ----

gxSound::gxSound( gxAudio *a,int16_t *p[2],int f,int c,int r,bool u ):audio(a),frames(f),channels(c),rate(r),use_3d(u),
loop(false),def_rate(r),def_vol(1),def_pan(0){
	planes[0]=p[0];planes[1]=p[1];
}

gxSound::~gxSound(){
	free( planes[0] );
	free( planes[1] );
}

gxChannel *gxSound::play(){ return audio->playSound( this ); }
gxChannel *gxSound::play3d( const float pos[3],const float vel[3] ){ return audio->playSound( this ); }
void gxSound::setLoop( bool l ){ loop=l; }
void gxSound::setPitch( int hertz ){ def_rate=hertz; }
void gxSound::setVolume( float v ){ def_vol=v; }
void gxSound::setPan( float p ){ def_pan=p; }

// ---- gxAudio ----

static gxAudio *the_audio;
static void appPaused(){ if( the_audio ) the_audio->pause(); }
static void appResumed(){ if( the_audio ) the_audio->resume(); }

gxAudio::gxAudio( gxRuntime *r ):runtime(r){
	init();
	the_audio=this;
	wiiu::appSetPauseCallbacks( appPaused,appResumed );
}

gxAudio::~gxAudio(){
	if( the_audio==this ) the_audio=0;
	while( sound_set.size() ) freeSound( *sound_set.begin() );
	shutdown();
}

void gxAudio::shutdown(){
	if( !initialised ) return;
	if( stream_run ){
		stream_run=false;
		int ret;
		OSJoinThread( &stream_thread,&ret );
	}
	Lock lock;
	for( size_t k=0;k<channel_pool.size();++k ) freeVoices( channel_pool[k] );
	for( set<Stream*>::iterator it=streams.begin();it!=streams.end();++it ) freeVoices( &(*it)->chan );
}

static void applyAllStates(){
	for( size_t k=0;k<channel_pool.size();++k ) applyState( channel_pool[k] );
	for( set<gxAudio::Stream*>::iterator it=streams.begin();it!=streams.end();++it ) applyState( &(*it)->chan );
}

void gxAudio::pause(){
	Lock lock;
	app_paused=true;
	applyAllStates();
}

void gxAudio::resume(){
	Lock lock;
	app_paused=false;
	applyAllStates();
}

void gxAudio::setPaused( bool paused ){
	Lock lock;
	master_paused=paused;
	applyAllStates();
}

void gxAudio::setVolume( float volume ){
	Lock lock;
	master_vol=volume;
	for( size_t k=0;k<channel_pool.size();++k ) applyMix( channel_pool[k] );
	for( set<Stream*>::iterator it=streams.begin();it!=streams.end();++it ) applyMix( &(*it)->chan );
}

gxSound *gxAudio::loadSound( const string &f,bool use_3d ){
	BB_PROF_SCOPE( "[LoadSound: decode]" );
	string path=gxFileSystem::resolvePath( f );
	FileData data;
	if( path.empty() || !gxFileSystem::readWholeFile( path,&data ) ){
		WHBLogPrintf( "audio: can't read %s",f.c_str() );
		return 0;
	}
	vector<int16_t> pcm;
	int channels=0,rate=0;
	if( data.size()>=4 && !memcmp( data.data(),"OggS",4 ) ){
		short *out=0;
		int frames=stb_vorbis_decode_memory( data.data(),(int)data.size(),&channels,&rate,&out );
		if( frames>0 && out ){
			pcm.assign( out,out+(size_t)frames*channels );
		}
		free( out );
	}else if( !decodeWav( data.data(),data.size(),&pcm,&channels,&rate ) ){
		WHBLogPrintf( "audio: unsupported sound format %s",f.c_str() );
		return 0;
	}
	if( channels<1 || rate<=0 || pcm.size()<(size_t)channels ){
		WHBLogPrintf( "audio: can't decode %s",f.c_str() );
		return 0;
	}
	int frames=(int)(pcm.size()/channels);
	int nc=channels>1 ? 2 : 1;
	int16_t *planes[2]={ allocPlane( frames ),nc>1 ? allocPlane( frames ) : 0 };
	if( !planes[0] || (nc>1 && !planes[1]) ){
		free( planes[0] );free( planes[1] );
		WHBLogPrintf( "audio: out of memory for %s",f.c_str() );
		return 0;
	}
	for( int k=0;k<frames;++k ){
		planes[0][k]=pcm[(size_t)k*channels];
		if( nc>1 ) planes[1][k]=pcm[(size_t)k*channels+1];
	}
	for( int k=0;k<nc;++k ) flushPlane( planes[k],frames );
	gxSound *s=d_new gxSound( this,planes,frames,nc,rate,use_3d );
	sound_set.insert( s );
	return s;
}

gxSound *gxAudio::verifySound( gxSound *s ){ return sound_set.count( s ) ? s : 0; }

size_t gxAudio::soundBytes( int *count )const{
	size_t n=0;
	for( std::set<gxSound*>::const_iterator it=sound_set.begin();it!=sound_set.end();++it ){
		n+=(size_t)(*it)->frames*2*(*it)->channels;
	}
	*count=sound_set.size();
	return n;
}

void gxAudio::freeSound( gxSound *s ){
	if( !sound_set.erase( s ) ) return;
	{
		Lock lock;
		for( size_t k=0;k<channel_pool.size();++k ){
			if( channel_pool[k]->sound==s ) freeVoices( channel_pool[k] );
		}
	}
	delete s;
}

gxChannel *gxAudio::playSound( gxSound *s ){
	Lock lock;
	AXChannel *c=allocChannel();
	if( !c ) return 0;
	c->stream=0;
	c->vol=s->def_vol;
	c->pan=s->def_pan;
	c->rate=s->def_rate;
	c->paused=false;
	if( !startVoices( c,s->planes,s->channels,s->frames,s->loop ) ) return 0;
	c->sound=s;
	return c;
}

void gxAudio::set3dOptions( float roll,float dopp,float dist ){}
void gxAudio::set3dListener( const float pos[3],const float vel[3],const float forward[3],const float up[3] ){}
gxChannel *gxAudio::playCDTrack( int track,int mode ){ return 0; }

gxChannel *gxAudio::playFile( const string &filename,bool use_3d ){
	Stream *s=openStream( filename,false );
	return s ? playStream( s ) : 0;
}

gxAudio::Stream *gxAudio::openStream( const string &f,bool loop ){
	string path=gxFileSystem::resolvePath( f );
	Stream *st=new Stream();
	if( path.empty() || !gxFileSystem::readWholeFile( path,&st->file ) ){
		WHBLogPrintf( "audio: can't read stream %s",f.c_str() );
		delete st;
		return 0;
	}
	int err=0;
	st->vb=stb_vorbis_open_memory( st->file.data(),(int)st->file.size(),&err,0 );
	if( !st->vb ){
		WHBLogPrintf( "audio: can't open stream %s (not Ogg Vorbis ? error %d)",f.c_str(),err );
		delete st;
		return 0;
	}
	stb_vorbis_info info=stb_vorbis_get_info( st->vb );
	st->channels=info.channels>1 ? 2 : 1;
	st->rate=(int)info.sample_rate;
	st->loop=loop;
	st->ring[0]=allocPlane( RING_FRAMES );
	st->ring[1]=st->channels>1 ? allocPlane( RING_FRAMES ) : 0;
	st->written=st->played=st->end_frame=0;
	st->last_pos=0;
	st->eof=false;
	st->finished=true;		//until played
	st->chan.stream=st;
	if( !st->ring[0] || (st->channels>1 && !st->ring[1]) ){
		closeStream( st );
		return 0;
	}
	Lock lock;
	streams.insert( st );
	return st;
}

gxChannel *gxAudio::playStream( Stream *st ){
	if( !st ) return 0;
	Lock lock;
	AXChannel *c=&st->chan;
	freeVoices( c );
	//from the start, ring filled
	stb_vorbis_seek_start( st->vb );
	st->written=st->played=st->end_frame=0;
	st->last_pos=0;
	st->eof=false;
	fillFrames( st,RING_FRAMES-RING_SAFETY );
	c->vol=1;c->pan=0;c->rate=st->rate;c->paused=false;
	if( !startVoices( c,st->ring,st->channels,RING_FRAMES,true ) ) return 0;
	st->finished=false;
	return c;
}

void gxAudio::stopStream( Stream *st ){
	if( st ) st->chan.stop();
}

void gxAudio::closeStream( Stream *st ){
	if( !st ) return;
	{
		Lock lock;
		freeVoices( &st->chan );
		streams.erase( st );
	}
	if( st->vb ) stb_vorbis_close( st->vb );
	free( st->ring[0] );
	free( st->ring[1] );
	delete st;
}
