// Wii U port : networking commands are stubs (generated from the original bbsockets.cpp signatures).
// SCP:CB only uses OpenTCPStream for its update check, which then simply fails.
#include "std.h"
#include "bbsys.h"

BBStr * bbDottedIP( int IP ){ return d_new BBStr(); }
int bbCountHostIPs( BBStr * host_name ){ delete host_name; return 0; }
int bbHostIP( int host_index ){ return 0; }
int bbCreateUDPStream( int port ){ return 0; }
void bbCloseUDPStream( int udp_stream ){  }
void bbSendUDPMsg( int udp_stream,int dest_ip,int dest_port ){  }
int bbRecvUDPMsg( int udp_stream ){ return 0; }
int bbUDPStreamIP( int udp_stream ){ return 0; }
int bbUDPStreamPort( int udp_stream ){ return 0; }
int bbUDPMsgIP( int udp_stream ){ return 0; }
int bbUDPMsgPort( int udp_stream ){ return 0; }
void bbUDPTimeouts( int recv_timeout ){  }
int bbOpenTCPStream( BBStr * server,int server_port,int local_port ){ delete server; return 0; }
void bbCloseTCPStream( int tcp_stream ){  }
int bbCreateTCPServer( int port ){ return 0; }
void bbCloseTCPServer( int tcp_server ){  }
int bbAcceptTCPStream( int tcp_server ){ return 0; }
int bbTCPStreamIP( int tcp_stream ){ return 0; }
int bbTCPStreamPort( int tcp_stream ){ return 0; }
void bbTCPTimeouts( int read_millis,int accept_millis ){  }

bool sockets_create(){ return true; }
bool sockets_destroy(){ return true; }

void sockets_link( void(*rtSym)(const char*,void*) ){
	rtSym( "$DottedIP%IP",(void*)bbDottedIP );
	rtSym( "%CountHostIPs$host_name",(void*)bbCountHostIPs );
	rtSym( "%HostIP%host_index",(void*)bbHostIP );
	rtSym( "%CreateUDPStream%port=0",(void*)bbCreateUDPStream );
	rtSym( "CloseUDPStream%udp_stream",(void*)bbCloseUDPStream );
	rtSym( "SendUDPMsg%udp_stream%dest_ip%dest_port=0",(void*)bbSendUDPMsg );
	rtSym( "%RecvUDPMsg%udp_stream",(void*)bbRecvUDPMsg );
	rtSym( "%UDPStreamIP%udp_stream",(void*)bbUDPStreamIP );
	rtSym( "%UDPStreamPort%udp_stream",(void*)bbUDPStreamPort );
	rtSym( "%UDPMsgIP%udp_stream",(void*)bbUDPMsgIP );
	rtSym( "%UDPMsgPort%udp_stream",(void*)bbUDPMsgPort );
	rtSym( "UDPTimeouts%recv_timeout",(void*)bbUDPTimeouts );
	rtSym( "%OpenTCPStream$server%server_port%local_port=0",(void*)bbOpenTCPStream );
	rtSym( "CloseTCPStream%tcp_stream",(void*)bbCloseTCPStream );
	rtSym( "%CreateTCPServer%port",(void*)bbCreateTCPServer );
	rtSym( "CloseTCPServer%tcp_server",(void*)bbCloseTCPServer );
	rtSym( "%AcceptTCPStream%tcp_server",(void*)bbAcceptTCPStream );
	rtSym( "%TCPStreamIP%tcp_stream",(void*)bbTCPStreamIP );
	rtSym( "%TCPStreamPort%tcp_stream",(void*)bbTCPStreamPort );
	rtSym( "TCPTimeouts%read_millis%accept_millis",(void*)bbTCPTimeouts );
}
