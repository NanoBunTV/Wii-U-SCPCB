// Wii U gxSound : a decoded sample (16-bit PCM, one plane per channel, in memory the audio DSP reads) with
// Blitz's per-sound defaults. Played by AX voices (see gxaudio.cpp).
#ifndef GXSOUND_H
#define GXSOUND_H

#include <cstdint>
#include "gxchannel.h"

class gxAudio;

class gxSound{
public:
	gxAudio *audio;

	// takes ownership of the planes (memalign'd, big-endian samples, flushed from the CPU cache)
	gxSound( gxAudio *audio,int16_t *planes[2],int frames,int channels,int rate,bool use_3d );
	~gxSound();

	int16_t *planes[2];		// planes[1] is 0 for mono
	int frames,channels,rate;
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
