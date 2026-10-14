// Wii U gxSound : a decoded sample (16-bit PCM) with Blitz's per-sound defaults
#ifndef GXSOUND_H
#define GXSOUND_H

#include <vector>
#include <cstdint>
#include "gxchannel.h"

class gxAudio;

class gxSound{
public:
	gxAudio *audio;

	gxSound( gxAudio *audio,std::vector<int16_t> &pcm,int channels,int rate,bool use_3d );
	~gxSound();

	std::vector<int16_t> pcm;		// interleaved
	int channels,rate;
	bool use_3d,loop;
	int def_rate;
	float def_vol,def_pan;

	/***** GX INTERFACE *****/
public:
	//actions
	gxChannel *play();
	gxChannel *play3d( const float pos[3],const float vel[3] );

	//modifiers
	void setLoop( bool loop );
	void setPitch( int hertz );
	void setVolume( float volume );
	void setPan( float pan );
};

#endif
