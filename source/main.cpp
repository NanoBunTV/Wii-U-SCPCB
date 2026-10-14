// M2 room viewer : loads SCP:CB rooms (.rmesh) through the ported Blitz3D runtime and renders them with the
// GX2 back end. Free-fly camera on the GamePad; D-pad left / right cycles through the rooms.
// The room loading mirrors LoadRMesh in MapSystem.bb (geometry + textures only).
//
// Controls : left stick move, right stick look, ZL up, L down, B fast, D-pad left / right previous / next room.
#include "bbruntime.h"
#include "bbapi.h"
#include "gxruntime.h"
#include "wiiu_debug.h"

#include <malloc.h>
#include <coreinit/time.h>
#include <whb/log.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// Blitz handles are plain pointers passed around as ints; these helpers keep the casts readable
template<class T> static T *as( void *p ){ return (T*)p; }
static BBStr *S( const std::string &s ){ return new BBStr( s ); }	//the runtime frees string arguments

static const float ROOM_SCALE=8.0f/2048.0f;

enum{ KEY_W=17,KEY_A=30,KEY_S=31,KEY_D=32,KEY_SPACE=57,KEY_LCTRL=29,KEY_LSHIFT=42,KEY_LEFT=203,KEY_RIGHT=205 };

static Texture *ambient_tex,*blank_tex;
static std::map<std::string,Texture*> tex_cache;

static Texture *cachedTexture( const std::string &dir,const std::string &name,int kind ){
	std::string key=name;
	std::transform( key.begin(),key.end(),key.begin(),::tolower );
	std::map<std::string,Texture*>::iterator it=tex_cache.find( key );
	if( it!=tex_cache.end() ) return it->second;
	Texture *t=bbLoadTexture( S( dir+name ),kind<3 ? 1 : 3 );
	if( t ){
		if( kind==1 ) bbTextureBlend( t,5 );
		if( key.find( "_lm" )!=std::string::npos ) bbTextureBlend( t,3 );
	}
	tex_cache[key]=t;
	return t;
}

static void freeTextures(){
	for( std::map<std::string,Texture*>::iterator it=tex_cache.begin();it!=tex_cache.end();++it ){
		if( it->second ) bbFreeTexture( it->second );
	}
	tex_cache.clear();
}

static std::string readString( bbStream *f ){
	BBStr *s=bbReadString( f );
	std::string t=*s;
	delete s;
	return t;
}

// LoadRMesh (MapSystem.bb), drawn geometry only. Returns a pivot holding the opaque and alpha meshes.
static Entity *loadRMesh( const std::string &file ){
	bbFile *f=bbReadFile( S( file ) );
	if( !f ){
		WHBLogPrintf( "viewer: can't open %s",file.c_str() );
		return 0;
	}
	bbStream *st=as<bbStream>( f );
	std::string magic=readString( st );
	if( magic!="RoomMesh" && magic!="RoomMesh.HasTriggerBox" ){
		WHBLogPrintf( "viewer: %s is not RMESH (%s)",file.c_str(),magic.c_str() );
		bbCloseFile( f );
		return 0;
	}
	std::string dir=file.substr( 0,file.find_last_of( '\\' )+1 );

	Entity *root=bbCreatePivot( 0 );
	Entity *opaque=bbCreateMesh( root );
	Entity *alpha=bbCreateMesh( root );

	int count=bbReadInt( st );
	for( int i=0;i<count;++i ){
		Entity *child=bbCreateMesh( 0 );
		Brush *brush=bbCreateBrush( 255,255,255 );
		Texture *tex[2]={ 0,0 };
		int is_alpha=0;
		for( int j=0;j<2;++j ){
			int kind=bbReadByte( st );
			if( kind ){
				std::string name=readString( st );
				tex[j]=cachedTexture( dir,name,kind );
				if( tex[j] ){
					is_alpha=kind==3 ? 1 : 2;
					bbTextureCoords( tex[j],1-j );
				}
			}
		}
		if( is_alpha==1 ){
			if( tex[1] ){
				bbTextureBlend( tex[1],2 );
				bbBrushTexture( brush,tex[1],0,0 );
			}else{
				bbBrushTexture( brush,blank_tex,0,0 );
			}
		}else if( tex[0] && tex[1] ){
			for( int j=0;j<2;++j ) bbBrushTexture( brush,tex[j],0,j+1 );
			bbBrushTexture( brush,ambient_tex,0,0 );
		}else{
			for( int j=0;j<2;++j ) bbBrushTexture( brush,tex[j] ? tex[j] : blank_tex,0,j );
		}
		Surface *surf=bbCreateSurface( as<MeshModel>( child ),0 );
		if( is_alpha>0 ) bbPaintSurface( surf,brush );
		bbFreeBrush( brush );

		int nv=bbReadInt( st );
		for( int j=0;j<nv;++j ){
			float x=bbReadFloat( st ),y=bbReadFloat( st ),z=bbReadFloat( st );
			int v=bbAddVertex( surf,x,y,z,0,0,1 );
			for( int k=0;k<2;++k ){
				float u=bbReadFloat( st ),vv=bbReadFloat( st );
				bbVertexTexCoords( surf,v,u,vv,0,k );
			}
			int r=bbReadByte( st ),g=bbReadByte( st ),b=bbReadByte( st );
			bbVertexColor( surf,v,r,g,b,1 );
		}
		int nt=bbReadInt( st );
		for( int j=0;j<nt;++j ){
			int a=bbReadInt( st ),b=bbReadInt( st ),c=bbReadInt( st );
			bbAddTriangle( surf,a,b,c );
		}
		bbAddMesh( as<MeshModel>( child ),as<MeshModel>( is_alpha==1 ? alpha : opaque ) );
		bbFreeEntity( child );
	}
	bbCloseFile( f );
	return root;
}

static std::vector<std::string> listRooms(){
	std::vector<std::string> rooms;
	gxDir *d=bbReadDir( S( "GFX\\map" ) );
	if( !d ) return rooms;
	for( ;; ){
		BBStr *n=bbNextFile( d );
		std::string t=*n;
		delete n;
		if( t.empty() ) break;
		std::string l=t;
		std::transform( l.begin(),l.end(),l.begin(),::tolower );
		if( l.size()>6 && l.compare( l.size()-6,6,".rmesh" )==0 ) rooms.push_back( "GFX\\map\\"+t );
	}
	bbCloseDir( d );
	std::sort( rooms.begin(),rooms.end() );
	return rooms;
}

// wut routes the default heap (and so GPU allocations) through malloc : report malloc's usage
static unsigned usedMem(){
	struct mallinfo mi=mallinfo();
	return mi.uordblks;
}

static void viewer(){
	WHBLogPrintf( "viewer: start" );
	bbGraphics3D( 1280,720,0,1 );
	WHBLogPrintf( "viewer: graphics open" );
	bbSetBuffer( bbBackBuffer() );

	//textures LoadRMesh relies on (Main.bb / MapSystem.bb)
	ambient_tex=bbCreateTexture( 2,2,257,1 );
	bbTextureBlend( ambient_tex,5 );
	bbSetBuffer( bbTextureBuffer( ambient_tex,0 ) );
	bbClsColor( 0,0,0 );
	bbCls();
	blank_tex=bbCreateTexture( 4,4,1,1 );
	bbSetBuffer( bbTextureBuffer( blank_tex,0 ) );
	bbClsColor( 255,255,255 );
	bbCls();
	bbSetBuffer( bbBackBuffer() );
	bbClsColor( 0,0,0 );

	WHBLogPrintf( "viewer: textures ok" );
	gxFont *hud_font=bbLoadFont( S( "GFX/font/cour/Courier New.ttf" ),20,0,0,0 );
	if( hud_font ) bbSetFont( hud_font );
	Entity *cam=bbCreateCamera( 0 );
	bbCameraRange( as<Camera>( cam ),0.01f,40.0f );
	bbCameraClsColor( as<Camera>( cam ),20,20,30 );
	bbAmbientLight( 255,255,255 );

	std::vector<std::string> rooms=listRooms();
	WHBLogPrintf( "viewer: %d rooms, used %u KB",(int)rooms.size(),usedMem()/1024 );
	if( rooms.empty() ) return;
	int room_index=0;
	for( size_t k=0;k<rooms.size();++k ) if( rooms[k].find( "173_opt" )!=std::string::npos ) room_index=k;

	Entity *room=0;
	unsigned mem_before=0;
	int load_ms=0;
	float pitch=0,yaw=0;
	bool auto_orbit=true;		//slow turn and a tour of all rooms until the controls are used (for automated runs)
	int room_time=0;
	int tour_count=0;
	bool left_was=false,right_was=false;

	int frames=0,fps=0;
	int fps_time=bbMilliSecs();
	OSTime cpu_acc=0;
	float cpu_ms=0;

	//reset the mouse speeds (the first MouseXSpeed call returns the distance from 0,0)
	bbMoveMouse( 640,360 );
	bbMouseXSpeed();bbMouseYSpeed();

	for( ;; ){
		//room switching
		bool left=bbKeyDown( KEY_LEFT ),right=bbKeyDown( KEY_RIGHT );
		int step=(right && !right_was) ? 1 : ((left && !left_was) ? -1 : 0);
		left_was=left;right_was=right;
		if( step ) auto_orbit=false;
		if( auto_orbit && room && bbMilliSecs()-room_time>1500 && tour_count<(int)rooms.size() ){
			step=1;
			++tour_count;
			if( tour_count==(int)rooms.size() ) WHBLogPrintf( "viewer: tour done, all %d rooms loaded",(int)rooms.size() );
		}
		if( !room || step ){
			if( room ){ bbFreeEntity( room );room=0; }
			freeTextures();
			room_index=(room_index+step+rooms.size())%rooms.size();
			mem_before=usedMem();
			int t0=bbMilliSecs();
			room=loadRMesh( rooms[room_index] );
			if( !room ) room=bbCreatePivot( 0 );	//unreadable room (e.g. the empty room2.rmesh) : show nothing
			load_ms=bbMilliSecs()-t0;
			if( room ) bbScaleEntity( room,ROOM_SCALE,ROOM_SCALE,ROOM_SCALE,0 );
			bbPositionEntity( cam,0,0.7f,0,0 );
			pitch=0;
			WHBLogPrintf( "viewer: %s loaded in %d ms, room %d KB, total used %u KB",rooms[room_index].c_str(),load_ms,
				(int)(usedMem()-mem_before)/1024,usedMem()/1024 );
			room_time=bbMilliSecs();
		}

		OSTime t_start=OSGetSystemTime();

		//camera
		int mxs=bbMouseXSpeed(),mys=bbMouseYSpeed();
		if( mxs || mys || bbKeyDown( KEY_W ) || bbKeyDown( KEY_S ) || bbKeyDown( KEY_A ) || bbKeyDown( KEY_D ) ) auto_orbit=false;
		if( auto_orbit ) yaw+=0.3f;
		yaw-=mxs*0.25f;
		pitch+=mys*0.25f;
		if( pitch>89 ) pitch=89;
		if( pitch<-89 ) pitch=-89;
		bbMoveMouse( 640,360 );
		bbMouseXSpeed();bbMouseYSpeed();
		bbRotateEntity( cam,pitch,yaw,0,0 );
		float speed=bbKeyDown( KEY_LSHIFT ) ? 0.15f : 0.05f;
		float fwd=(bbKeyDown( KEY_W ) ? 1 : 0)-(bbKeyDown( KEY_S ) ? 1 : 0);
		float side=(bbKeyDown( KEY_D ) ? 1 : 0)-(bbKeyDown( KEY_A ) ? 1 : 0);
		float up=(bbKeyDown( KEY_SPACE ) ? 1 : 0)-(bbKeyDown( KEY_LCTRL ) ? 1 : 0);
		bbMoveEntity( cam,side*speed,up*speed,fwd*speed );

		bbRenderWorld( 1 );

		cpu_acc+=OSGetSystemTime()-t_start;

		//HUD
		bbColor( 255,255,255 );
		char line[256];
		snprintf( line,sizeof(line),"%s (%d/%d)  loaded in %d ms, %d KB",rooms[room_index].c_str(),room_index+1,
			(int)rooms.size(),load_ms,(int)(usedMem()-mem_before)/1024 );
		bbText( 8,8,S( line ),0,0 );
		snprintf( line,sizeof(line),"%d fps  cpu %.1f ms  %d tris  used %u MB  pitch %.0f yaw %.0f",fps,cpu_ms,
			bbTrisRendered(),usedMem()/(1024*1024),pitch,yaw );
		bbText( 8,30,S( line ),0,0 );
		bbText( 8,690,S( "L-stick move  R-stick look  ZL/L up/down  B fast  D-pad room" ),0,0 );

		bbFlip( 1 );

		++frames;
		int now=bbMilliSecs();
		if( now-fps_time>=1000 ){
			fps=frames;
			cpu_ms=OSTicksToMicroseconds( cpu_acc )/1000.0f/frames;
			frames=0;cpu_acc=0;
			fps_time=now;
		}
	}
}

static int viewerMain(){
	gxRuntime *rt=gxRuntime::openRuntime( "" );
	if( !rt ) return 0;
	const char *err=bbruntime_run( rt,viewer,false );
	if( err ) WHBLogPrintf( "viewer: runtime error: %s",err );
	gxRuntime::closeRuntime( rt );
	return 0;
}

int main( int argc,char **argv ){
	wiiu::debugInit();
	int ret=wiiu::runOnBigStack( viewerMain );
	wiiu::debugShutdown();
	return ret;
}
