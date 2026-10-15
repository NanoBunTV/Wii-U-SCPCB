// Wii U software keyboard (see wiiu_keyboard.h).
//
// swkbd is created on first use (its work memory and an FS client stay allocated afterwards). While it is up, this
// function runs its own frame loop : read the GamePad, swkbd Calc, then gxGraphics::flip, whose present hook draws
// the keyboard into the TV and GamePad pictures after the back buffer. The game is blocked meanwhile (Blitz code
// waits for the text), and appPoll keeps the HOME Menu working.
#include "std.h"
#include "wiiu_keyboard.h"
#include "gxgraphics.h"
#include "wiiu_app.h"

#include <nn/swkbd.h>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <vpad/input.h>
#include <whb/log.h>

namespace wiiu {

static bool created,create_failed;
static FSClient *fs_client;
static void *work_memory;

static bool create(){
	if( created ) return true;
	if( create_failed ) return false;
	fs_client=(FSClient*)MEMAllocFromDefaultHeap( sizeof(FSClient) );
	uint32_t size=nn::swkbd::GetWorkMemorySize( 0 );
	work_memory=MEMAllocFromDefaultHeap( size );
	if( !fs_client || !work_memory ){
		WHBLogPrintf( "keyboard: out of memory (%u bytes of work memory)",(unsigned)size );
		create_failed=true;
		return false;
	}
	FSAddClient( fs_client,FS_ERROR_FLAG_ALL );
	nn::swkbd::CreateArg arg;
	arg.workMemory=work_memory;
	arg.regionType=nn::swkbd::RegionType::Europe;
	arg.fsClient=fs_client;
	if( !nn::swkbd::Create( arg ) ){
		WHBLogPrintf( "keyboard: nn::swkbd::Create failed" );
		create_failed=true;
		return false;
	}
	created=true;
	WHBLogPrintf( "keyboard: created (%u KB of work memory)",(unsigned)(size/1024) );
	return true;
}

static void drawKeyboard( int screen ){
	if( screen==0 ) nn::swkbd::DrawTV();
	else nn::swkbd::DrawDRC();
}

static void calcFrame(){
	VPADStatus vpad;
	VPADReadError err;
	memset( &vpad,0,sizeof(vpad) );
	if( VPADRead( VPAD_CHAN_0,&vpad,1,&err )<1 || err!=VPAD_READ_SUCCESS ) memset( &vpad,0,sizeof(vpad) );
	else VPADGetTPCalibratedPoint( VPAD_CHAN_0,&vpad.tpNormal,&vpad.tpNormal );
	nn::swkbd::ControllerInfo info;
	info.vpad=&vpad;
	nn::swkbd::Calc( info );
	if( nn::swkbd::IsNeedCalcSubThreadFont() ) nn::swkbd::CalcSubThreadFont();
	if( nn::swkbd::IsNeedCalcSubThreadPredict() ) nn::swkbd::CalcSubThreadPredict();
}

bool keyboardInput( gxGraphics *g,const string &initial,const string &hint,int max_length,string *out ){
	if( !g || !create() ) return false;

	//Blitz strings are Windows-1252 bytes : Latin-1 is close enough for a text box
	std::u16string init16,hint16;
	for( size_t k=0;k<initial.size();++k ) init16+=(char16_t)(unsigned char)initial[k];
	for( size_t k=0;k<hint.size();++k ) hint16+=(char16_t)(unsigned char)hint[k];

	nn::swkbd::AppearArg arg;
	arg.keyboardArg.configArg.languageType=nn::swkbd::LanguageType::English;
	arg.keyboardArg.configArg.disableNewLine=true;
	arg.inputFormArg.initialText=init16.c_str();
	arg.inputFormArg.hintText=hint16.c_str();
	arg.inputFormArg.maxTextLength=max_length>0 ? max_length : -1;
	if( !nn::swkbd::AppearInputForm( arg ) ){
		WHBLogPrintf( "keyboard: AppearInputForm failed" );
		return false;
	}
	gxGraphics::setPresentHook( drawKeyboard );

	bool ok=false;
	for( ;; ){
		if( !appPoll() ) break;		//the system is closing the app
		calcFrame();
		if( nn::swkbd::IsDecideOkButton( 0 ) ){ ok=true;break; }
		if( nn::swkbd::IsDecideCancelButton( 0 ) ) break;
		g->flip( true );
	}
	if( ok ){
		out->clear();
		if( const char16_t *s=nn::swkbd::GetInputFormString() ){
			for( ;*s;++s ) *out+=(char)(*s<256 ? *s : '?');
		}
	}
	nn::swkbd::DisappearInputForm();
	//let it fade out (a few frames)
	for( int k=0;k<60 && nn::swkbd::GetStateInputForm()!=nn::swkbd::State::Hidden;++k ){
		if( !appPoll() ) break;
		calcFrame();
		g->flip( true );
	}
	gxGraphics::setPresentHook( 0 );
	return ok;
}

}
