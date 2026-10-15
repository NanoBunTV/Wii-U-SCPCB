#include "std.h"
#include "imageload.h"
#include "gxcanvas.h"
#include "gxfilesystem.h"

#include <whb/log.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS		//elf2rpl can't relocate TLS
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace imageload{

void adjustTexSize( int *width,int *height ){
	int w,h;
	for( w=1;w<*width;w<<=1 ){}
	for( h=1;h<*height;h<<=1 ){}
	if( w>2048 ) w=2048;
	if( h>2048 ) h=2048;
	*width=w;*height=h;
}

bool load( const string &path,int flags,vector<unsigned> *out,int *width,int *height ){
	int w,h,comp;
	vector<unsigned char> file;
	if( !gxFileSystem::readWholeFile( path,&file ) ){
		WHBLogPrintf( "image: can't load %s (can't fopen)",path.c_str() );
		return false;
	}
	unsigned char *data=stbi_load_from_memory( file.data(),(int)file.size(),&w,&h,&comp,4 );
	if( !data ){
		WHBLogPrintf( "image: can't load %s (%s)",path.c_str(),stbi_failure_reason() );
		return false;
	}
	vector<unsigned char>().swap( file );
	bool trans=comp==4 || comp==2;

	vector<unsigned> argb( w*h );
	for( int k=0;k<w*h;++k ){
		const unsigned char *p=data+k*4;
		argb[k]=(p[3]<<24)|(p[0]<<16)|(p[1]<<8)|p[2];
	}
	stbi_image_free( data );

	if( flags & gxCanvas::CANVAS_TEX_ALPHA ){
		if( flags & gxCanvas::CANVAS_TEX_MASK ){
			//buildMask : black is transparent
			for( unsigned &c : argb ){
				unsigned rgb=c&0xffffff;
				c=(rgb ? 0xff000000 : 0)|rgb;
			}
		}else if( !trans ){
			//buildAlpha : alpha from brightness, colour whitened unless the colour flag is set
			bool whiten=!(flags & gxCanvas::CANVAS_TEX_RGB);
			for( unsigned &c : argb ){
				unsigned a=(((c>>16)&0xff)+((c>>8)&0xff)+(c&0xff))/3;
				c=(a<<24)|(c&0xffffff);
				if( whiten ) c|=0xffffff;
			}
		}
	}else{
		for( unsigned &c : argb ) c|=0xff000000;
	}

	int tw=w,th=h;
	if( flags & gxCanvas::CANVAS_TEXTURE ) adjustTexSize( &tw,&th );
	if( tw!=w || th!=h ){
		//stretch (nearest), like the DirectDraw blit the original used
		vector<unsigned> t( tw*th );
		for( int y=0;y<th;++y ){
			int sy=(int)((long long)y*h/th);
			for( int x=0;x<tw;++x ) t[y*tw+x]=argb[sy*w+(int)((long long)x*w/tw)];
		}
		argb.swap( t );
	}
	out->swap( argb );
	*width=tw;*height=th;
	return true;
}

}
