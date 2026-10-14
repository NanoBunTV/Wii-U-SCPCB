// Wii U gxInput : GamePad / Pro Controller / touch screen mapped onto Blitz's keyboard, mouse and joystick
// devices (see gxinput.cpp for the mapping).
#ifndef GXINPUT_H
#define GXINPUT_H

#include "gxdevice.h"

class gxRuntime;

class gxInput{
public:
	gxRuntime *runtime;

	gxInput( gxRuntime *runtime );
	~gxInput();

	void reset();
	bool acquire(){ return true; }
	void unacquire(){}

	// reads the controllers and updates the devices (called from gxRuntime::idle)
	void poll();
	// mouse coordinates cover the back buffer (set when graphics open)
	void setMouseArea( int w,int h );

private:

	/***** GX INTERFACE *****/
public:
	enum{
		ASC_HOME=1,ASC_END=2,ASC_INSERT=3,ASC_DELETE=4,
		ASC_PAGEUP=5,ASC_PAGEDOWN=6,
		ASC_UP=28,ASC_DOWN=29,ASC_RIGHT=30,ASC_LEFT=31
	};

	void moveMouse( int x,int y );

	gxDevice *getMouse()const;
	gxDevice *getKeyboard()const;
	gxDevice *getJoystick( int port )const;
	int getJoystickType( int port )const;
	int numJoysticks()const;
	int toAscii( int key )const;
};

#endif
