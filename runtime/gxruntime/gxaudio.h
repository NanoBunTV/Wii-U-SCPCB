// Wii U gxAudio : sounds and streams played by AX voices (the console's audio DSP mixes and resamples them).
//
// - LoadSound decodes the whole file (Ogg Vorbis, WAV) to 16-bit planes; each channel of a playing sound is one
//   AX voice (stereo : two, panned left / right).
// - Streams (PlayMusic and the FMOD stream functions SCP:CB uses for music) decode Ogg Vorbis into a ring buffer
//   that a looping AX voice plays; a thread on CPU0 keeps the ring filled.
// - Channels are pooled like Blitz's : a new sound reuses a channel object that has stopped, so channel handles
//   the game keeps stay valid.
#ifndef GXAUDIO_H
#define GXAUDIO_H

#include <string>
#include <set>
#include "gxsound.h"

class gxRuntime;

class gxAudio{
public:
	gxRuntime *runtime;

	gxAudio( gxRuntime *runtime );
	~gxAudio();

	// app in the background (HOME Menu) and back
	void pause();
	void resume();

	// streamed file playback (Ogg Vorbis) : open, then play (returns the channel, 0 on failure); a stream plays on
	// one channel at a time. Used by PlayMusic and the FMOD stream functions (runtime/userlibs).
	struct Stream;
	Stream *openStream( const std::string &filename,bool loop );
	gxChannel *playStream( Stream *stream );
	void stopStream( Stream *stream );
	void closeStream( Stream *stream );

	// stops the stream thread and every voice (before AXQuit, also when the system closes the app)
	static void shutdown();

	// memory report : bytes of decoded samples held by loaded sounds
	size_t soundBytes( int *count )const;

private:
	std::set<gxSound*> sound_set;

	/***** GX INTERFACE *****/
public:
	enum{
		CD_MODE_ONCE=1,CD_MODE_LOOP,CD_MODE_ALL
	};

	gxSound *loadSound( const std::string &filename,bool use_3d );
	gxSound *verifySound( gxSound *sound );
	void freeSound( gxSound *sound );

	void setPaused( bool paused );	//master pause
	void setVolume( float volume );	//master volume

	void set3dOptions( float roll,float dopp,float dist );
	void set3dListener( const float pos[3],const float vel[3],const float forward[3],const float up[3] );

	gxChannel *playCDTrack( int track,int mode );
	gxChannel *playFile( const std::string &filename,bool use_3d );

	// for gxSound::play
	gxChannel *playSound( gxSound *sound );
};

#endif
