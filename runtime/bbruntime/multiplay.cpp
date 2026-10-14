// Wii U port : DirectPlay network game commands are stubs (generated from the original multiplay.cpp).
#include "std.h"
#include "bbsys.h"

int bbStartNetGame(  ){ return 0; }
int bbHostNetGame( BBStr * game_name ){ delete game_name; return 0; }
int bbJoinNetGame( BBStr * game_name,BBStr * ip_address ){ delete game_name; delete ip_address; return 0; }
void bbStopNetGame(  ){  }
int bbCreateNetPlayer( BBStr * name ){ delete name; return 0; }
void bbDeleteNetPlayer( int player ){  }
BBStr * bbNetPlayerName( int player ){ return d_new BBStr(); }
int bbNetPlayerLocal( int player ){ return 0; }
int bbSendNetMsg( int type,BBStr * msg,int from_player,int to_player,int reliable ){ delete msg; return 0; }
int bbRecvNetMsg(  ){ return 0; }
int bbNetMsgType(  ){ return 0; }
int bbNetMsgFrom(  ){ return 0; }
int bbNetMsgTo(  ){ return 0; }
BBStr * bbNetMsgData(  ){ return d_new BBStr(); }

bool multiplay_create(){ return true; }
bool multiplay_destroy(){ return true; }

void multiplay_link( void(*rtSym)(const char*,void*) ){
	rtSym( "%StartNetGame",(void*)bbStartNetGame );
	rtSym( "%HostNetGame$game_name",(void*)bbHostNetGame );
	rtSym( "%JoinNetGame$game_name$ip_address",(void*)bbJoinNetGame );
	rtSym( "StopNetGame",(void*)bbStopNetGame );
	rtSym( "%CreateNetPlayer$name",(void*)bbCreateNetPlayer );
	rtSym( "DeleteNetPlayer%player",(void*)bbDeleteNetPlayer );
	rtSym( "$NetPlayerName%player",(void*)bbNetPlayerName );
	rtSym( "%NetPlayerLocal%player",(void*)bbNetPlayerLocal );
	rtSym( "%SendNetMsg%type$msg%from_player%to_player=0%reliable=1",(void*)bbSendNetMsg );
	rtSym( "%RecvNetMsg",(void*)bbRecvNetMsg );
	rtSym( "%NetMsgType",(void*)bbNetMsgType );
	rtSym( "%NetMsgFrom",(void*)bbNetMsgFrom );
	rtSym( "%NetMsgTo",(void*)bbNetMsgTo );
	rtSym( "$NetMsgData",(void*)bbNetMsgData );
}
