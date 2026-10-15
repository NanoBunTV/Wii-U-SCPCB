// Wii U GamePad -> Blitz keyboard / mouse / joystick.
//
// Mapping (SCP:CB's default binds) :
//   left stick      W / A / S / D (DirectInput 17 / 30 / 31 / 32)
//   right stick     mouse movement (mouse look)
//   touch screen    with a pointer (menus, inventory, keypads : the game's ShowPointer) : mouse position, tap = left
//                   click; in gameplay (HidePointer) : drag = look around, quick tap = left click (interact)
//   A, ZR           left mouse button        Y   right mouse button
//   ZL              Space (blink)            L   Left Ctrl (crouch)
//   B, L-stick press Left Shift (sprint)     X   Tab (inventory)
//   +               Escape (menu)            -   F5 (save)
//   R               F3 (console, when enabled in the options)
//   D-pad           arrow keys
#include "std.h"
#include "gxinput.h"
#include "gxruntime.h"
#include "gxfilesystem.h"

#include <vpad/input.h>
#include <coreinit/time.h>
#include <whb/log.h>
#include <cstdio>

enum{
	DIK_ESCAPE=1,DIK_TAB=15,DIK_W=17,DIK_RETURN=28,DIK_LCONTROL=29,DIK_A=30,DIK_S=31,DIK_D=32,
	DIK_LSHIFT=42,DIK_SPACE=57,DIK_F3=61,DIK_F5=63,DIK_UP=200,DIK_LEFT=203,DIK_RIGHT=205,DIK_DOWN=208
};

class Keyboard : public gxDevice{};

class Mouse : public gxDevice{
public:
	float width,height;
	Mouse():width(1280),height(720){}
};

class Joystick : public gxDevice{};

static Keyboard *keyboard;
static Mouse *mouse;
static Joystick *joystick;
static bool touch_down;
//touch in gameplay (pointer hidden) : drag to look, tap to click
static bool look_touch;					//the current touch started in gameplay
static float look_last_x,look_last_y,look_moved;
static int look_start_ms,tap_click_frames;

// ---- scripted input for automated tests ----
// If <writable folder>/autoinput.txt exists (only on the test SD card of the portable Cemu), its timed events
// are merged with the GamePad. One event per line, time in ms since start :
//   <ms> key <scancode>          press and release a key (held 100 ms)
//   <ms> keydown|keyup <scancode>
//   <ms> click <x> <y>           move the mouse and left-click (held 100 ms)
//   <ms> mouse <x> <y>           move the mouse
struct ScriptEvent{ int ms;string op;int a,b; };
static vector<ScriptEvent> script;
static size_t script_pos;
static bool script_keys[256];
static int script_key_release[256];
static bool script_click;
static int script_click_release;
static int script_start;

static void loadScript(){
	string path=gxFileSystem::writableRoot()+"autoinput.txt";
	FILE *f=fopen( path.c_str(),"r" );
	if( !f ) return;
	char line[256];
	while( fgets( line,sizeof(line),f ) ){
		char op[32]={0};
		ScriptEvent e={0,"",0,0};
		if( sscanf( line,"%d %31s %d %d",&e.ms,op,&e.a,&e.b )>=2 ){
			e.op=op;
			script.push_back( e );
		}
	}
	fclose( f );
	script_start=(int)OSTicksToMilliseconds( OSGetSystemTime() );
	WHBLogPrintf( "gxInput: %d scripted input events",(int)script.size() );
}

static void runScript( bool down[256],bool &click,float &mx,float &my ){
	if( script.empty() ) return;
	int now=(int)OSTicksToMilliseconds( OSGetSystemTime() )-script_start;
	while( script_pos<script.size() && script[script_pos].ms<=now ){
		const ScriptEvent &e=script[script_pos++];
		int k=e.a&255;
		if( e.op=="key" ){ script_keys[k]=true;script_key_release[k]=now+100; }
		else if( e.op=="keydown" ){ script_keys[k]=true;script_key_release[k]=0; }
		else if( e.op=="keyup" ){ script_keys[k]=false; }
		else if( e.op=="click" ){ mx=e.a;my=e.b;script_click=true;script_click_release=now+100; }
		else if( e.op=="mouse" ){ mx=e.a;my=e.b; }
		WHBLogPrintf( "gxInput: script %d %s %d %d",e.ms,e.op.c_str(),e.a,e.b );
	}
	for( int k=0;k<256;++k ){
		if( script_keys[k] && script_key_release[k] && now>=script_key_release[k] ) script_keys[k]=false;
		if( script_keys[k] ) down[k]=true;
	}
	if( script_click && now>=script_click_release ) script_click=false;
	if( script_click ) click=true;
}

struct ButtonKey{
	uint32_t button;
	int key;
};

static const ButtonKey KEYS[]={
	{ VPAD_BUTTON_ZL,DIK_SPACE },
	{ VPAD_BUTTON_L,DIK_LCONTROL },
	{ VPAD_BUTTON_B,DIK_LSHIFT },
	{ VPAD_BUTTON_STICK_L,DIK_LSHIFT },
	{ VPAD_BUTTON_X,DIK_TAB },
	{ VPAD_BUTTON_PLUS,DIK_ESCAPE },
	{ VPAD_BUTTON_MINUS,DIK_F5 },
	{ VPAD_BUTTON_R,DIK_F3 },
	{ VPAD_BUTTON_UP,DIK_UP },
	{ VPAD_BUTTON_DOWN,DIK_DOWN },
	{ VPAD_BUTTON_LEFT,DIK_LEFT },
	{ VPAD_BUTTON_RIGHT,DIK_RIGHT },
};

gxInput::gxInput( gxRuntime *rt ):runtime(rt){
	keyboard=d_new Keyboard;
	mouse=d_new Mouse;
	joystick=d_new Joystick;
	touch_down=false;
	loadScript();
}

gxInput::~gxInput(){
	delete keyboard;keyboard=0;
	delete mouse;mouse=0;
	delete joystick;joystick=0;
}

void gxInput::reset(){
	if( keyboard ) keyboard->reset();
	if( mouse ) mouse->reset();
	if( joystick ) joystick->reset();
}

void gxInput::poll(){
	if( !keyboard ) return;
	VPADStatus st;
	VPADReadError err;
	if( VPADRead( VPAD_CHAN_0,&st,1,&err )<1 || err!=VPAD_READ_SUCCESS ) return;

	uint32_t hold=st.hold;

	//right stick button : debug overlay on / off
	extern bool gx_overlay_enabled;
	if( st.trigger & VPAD_BUTTON_STICK_R ) gx_overlay_enabled=!gx_overlay_enabled;

	//keys : a key is down if any button mapped to it is held
	bool down[256]={};
	for( size_t k=0;k<sizeof(KEYS)/sizeof(KEYS[0]);++k ){
		if( hold & KEYS[k].button ) down[KEYS[k].key]=true;
	}
	const float T=.4f;
	if( st.leftStick.y>T ) down[DIK_W]=true;
	if( st.leftStick.y<-T ) down[DIK_S]=true;
	if( st.leftStick.x<-T ) down[DIK_A]=true;
	if( st.leftStick.x>T ) down[DIK_D]=true;
	//mouse : right stick moves, touch screen positions
	float mx=mouse->axis_states[0],my=mouse->axis_states[1];
	bool script_click_down=false;
	runScript( down,script_click_down,mx,my );

	//every key the GamePad or the script can press
	for( int k=1;k<256;++k ) keyboard->setDownState( k,down[k] );
	float rx=st.rightStick.x,ry=st.rightStick.y;
	const float DZ=.15f,SPEED=14.0f;
	if( fabsf( rx )>DZ ) mx+=(rx-(rx>0 ? DZ : -DZ))/(1-DZ)*SPEED;
	if( fabsf( ry )>DZ ) my-=(ry-(ry>0 ? DZ : -DZ))/(1-DZ)*SPEED;

	bool touching=false;
	bool pointer=runtime->pointerVisible();
	int now_ms=(int)OSTicksToMilliseconds( OSGetSystemTime() );
	bool touched=st.tpNormal.touched && st.tpNormal.validity==VPAD_VALID;
	if( touched ){
		VPADTouchData t;
		VPADGetTPCalibratedPoint( VPAD_CHAN_0,&t,&st.tpNormal );
		if( !touch_down && !look_touch ){
			//a new touch : its mode is decided by what the game shows when it starts
			look_touch=!pointer;
			look_last_x=t.x;look_last_y=t.y;
			look_moved=0;
			look_start_ms=now_ms;
		}
		if( look_touch ){
			//gameplay : drag = mouse movement (calibrated points are 1280x720 screen space)
			const float DRAG=.7f;
			float dx=t.x-look_last_x,dy=t.y-look_last_y;
			look_last_x=t.x;look_last_y=t.y;
			look_moved+=fabsf( dx )+fabsf( dy );
			mx+=dx*DRAG;
			my+=dy*DRAG;
		}else{
			//calibrated points are in 1280x720 screen space; the back buffer is shown letterboxed on the GamePad
			float sw=1280,sh=720;
			float ba=mouse->width/mouse->height,sa=sw/sh,w=sw,h=sh;
			if( ba>sa ) h=sw/ba;
			else w=sh*ba;
			float ox=(sw-w)*.5f,oy=(sh-h)*.5f;
			mx=(t.x-ox)/w*mouse->width;
			my=(t.y-oy)/h*mouse->height;
			touching=true;
		}
	}else if( look_touch ){
		//released : a short touch that barely moved is a tap = left click (held 2 frames)
		if( now_ms-look_start_ms<300 && look_moved<24 ) tap_click_frames=2;
		look_touch=false;
	}
	bool tap_click=tap_click_frames>0;
	if( tap_click_frames>0 ) --tap_click_frames;
	if( mx<0 ) mx=0;
	if( my<0 ) my=0;
	if( mx>mouse->width-1 ) mx=mouse->width-1;
	if( my>mouse->height-1 ) my=mouse->height-1;
	mouse->axis_states[0]=(int)mx;
	mouse->axis_states[1]=(int)my;

	//mouse buttons : 1 left, 2 right (touch presses after the position moved, like a real click)
	mouse->setDownState( 1,(hold & (VPAD_BUTTON_A|VPAD_BUTTON_ZR)) || (touching && touch_down) || tap_click || script_click_down );
	mouse->setDownState( 2,(hold & VPAD_BUTTON_Y)!=0 );
	touch_down=touching || look_touch;

	//joystick 0 : the GamePad as a plain joystick
	joystick->axis_states[0]=st.leftStick.x;
	joystick->axis_states[1]=-st.leftStick.y;
	joystick->axis_states[5]=st.rightStick.x;
	joystick->axis_states[6]=-st.rightStick.y;
	static const uint32_t JB[]={ VPAD_BUTTON_A,VPAD_BUTTON_B,VPAD_BUTTON_X,VPAD_BUTTON_Y,VPAD_BUTTON_L,VPAD_BUTTON_R,
		VPAD_BUTTON_ZL,VPAD_BUTTON_ZR,VPAD_BUTTON_MINUS,VPAD_BUTTON_PLUS,VPAD_BUTTON_STICK_L,VPAD_BUTTON_STICK_R };
	for( int k=0;k<12;++k ) joystick->setDownState( k+1,(hold & JB[k])!=0 );
}

void gxInput::setMouseArea( int w,int h ){
	if( !mouse ) return;
	mouse->width=w;mouse->height=h;
	mouse->axis_states[0]=w/2;
	mouse->axis_states[1]=h/2;
}

void gxInput::moveMouse( int x,int y ){
	if( !mouse ) return;
	mouse->axis_states[0]=x;
	mouse->axis_states[1]=y;
}

gxDevice *gxInput::getMouse()const{ return mouse; }
gxDevice *gxInput::getKeyboard()const{ return keyboard; }
gxDevice *gxInput::getJoystick( int port )const{ return port==0 ? joystick : 0; }
int gxInput::getJoystickType( int port )const{ return port==0 ? 2 : 0; }
int gxInput::numJoysticks()const{ return 1; }

int gxInput::toAscii( int key )const{
	switch( key ){
	case DIK_RETURN: return 13;
	case DIK_ESCAPE: return 27;
	case DIK_TAB: return 9;
	case DIK_SPACE: return 32;
	case DIK_UP: return ASC_UP;
	case DIK_DOWN: return ASC_DOWN;
	case DIK_LEFT: return ASC_LEFT;
	case DIK_RIGHT: return ASC_RIGHT;
	}
	return 0;
}
