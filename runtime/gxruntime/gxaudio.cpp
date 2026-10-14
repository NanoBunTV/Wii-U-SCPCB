// Wii U gxAudio : placeholder until the software mixer is written (sounds load as 0, which Blitz tolerates)
#include "std.h"
#include "gxaudio.h"

gxAudio::gxAudio( gxRuntime *r ):runtime(r){
}

gxAudio::~gxAudio(){
	while( sound_set.size() ) freeSound( *sound_set.begin() );
}

void gxAudio::pause(){}
void gxAudio::resume(){}

gxChannel *gxAudio::playStream( const string &filename,bool loop ){ return 0; }

gxSound *gxAudio::loadSound( const string &filename,bool use_3d ){ return 0; }

gxSound *gxAudio::verifySound( gxSound *s ){ return sound_set.count( s ) ? s : 0; }

void gxAudio::freeSound( gxSound *s ){ if( sound_set.erase( s ) ) delete s; }

void gxAudio::setPaused( bool paused ){}
void gxAudio::setVolume( float volume ){}
void gxAudio::set3dOptions( float roll,float dopp,float dist ){}
void gxAudio::set3dListener( const float pos[3],const float vel[3],const float forward[3],const float up[3] ){}
gxChannel *gxAudio::playCDTrack( int track,int mode ){ return 0; }
gxChannel *gxAudio::playFile( const string &filename,bool use_3d ){ return 0; }

gxSound::gxSound( gxAudio *a,vector<int16_t> &p,int c,int r,bool u ):audio(a),channels(c),rate(r),use_3d(u),loop(false),
def_rate(r),def_vol(1),def_pan(0){
	pcm.swap( p );
}

gxSound::~gxSound(){}
gxChannel *gxSound::play(){ return 0; }
gxChannel *gxSound::play3d( const float pos[3],const float vel[3] ){ return 0; }
void gxSound::setLoop( bool l ){ loop=l; }
void gxSound::setPitch( int hertz ){ def_rate=hertz; }
void gxSound::setVolume( float v ){ def_vol=v; }
void gxSound::setPan( float p ){ def_pan=p; }

gxChannel::~gxChannel(){}
