// Wii U port : commands added in Blitz3D 1.106 - 1.108 that aren't in the open-source runtime release.
// SCP:CB calls them; they're stubs (bump-environment mapping isn't emulated, LOD bias isn't applied).
#include "std.h"
#include "bbsys.h"

class Texture;

void bbTextureBumpEnvMat( Texture *t,int x,int y,float envmat ){}
void bbTextureBumpEnvScale( Texture *t,float scale ){}
void bbTextureBumpEnvOffset( Texture *t,float offset ){}
void bbTextureLodBias( float bias ){}
BBStr *bbErrorLog(){ return d_new BBStr(); }

void extras_link( void (*rtSym)( const char *sym,void *pc ) ){
	rtSym( "TextureBumpEnvMat%texture%x%y#envmat",bbTextureBumpEnvMat );
	rtSym( "TextureBumpEnvScale%texture#envscale",bbTextureBumpEnvScale );
	rtSym( "TextureBumpEnvOffset%texture#envoffset",bbTextureBumpEnvOffset );
	rtSym( "TextureLodBias#bias",bbTextureLodBias );
	rtSym( "$ErrorLog",bbErrorLog );
}
