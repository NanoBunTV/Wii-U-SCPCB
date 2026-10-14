// texconv : precompresses the game's texture images for the Wii U port (BC1 / BC3 with mipmaps).
//
//   texconv <content_dir> [<skip_prefix> ...]
//
// For every .jpg/.jpeg/.png/.bmp/.tga under <content_dir> (except paths starting with a skip prefix, relative,
// '/' separated, case-insensitive) it writes, next to the image :
//   <image>.wbc   BC1, colour only           (used by LoadTexture without the alpha flag)
//   <image>.wbc3  BC3, colour + alpha        (only for images with an alpha channel; LoadTexture with alpha)
// The image is stretched to power-of-two sizes and mipmapped exactly like the runtime does for uncompressed
// textures (runtime/gxruntime/imageload.cpp, gxcanvas.cpp). Files are only rebuilt when older than the image.
//
// .wbc format (all integers big-endian, for the Wii U) :
//   "WBC1" | format u32 (1 = BC1, 3 = BC3) | width u32 | height u32 | mip count u32 | reserved u32
//   then per mip level : byte size u32, then the blocks (standard DXT layout, little-endian as on PC)
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../../runtime/thirdparty/stb/stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "../../runtime/thirdparty/stb/stb_dxt.h"

namespace fs=std::filesystem;
using namespace std;

static void adjustTexSize( int *width,int *height ){
	int w,h;
	for( w=1;w<*width;w<<=1 ){}
	for( h=1;h<*height;h<<=1 ){}
	if( w>2048 ) w=2048;
	if( h>2048 ) h=2048;
	*width=w;*height=h;
}

static void put32( vector<unsigned char> &out,unsigned n ){
	out.push_back( n>>24 );out.push_back( n>>16 );out.push_back( n>>8 );out.push_back( n );
}

// rgba : w*h*4 bytes
static void compressLevel( const vector<unsigned char> &rgba,int w,int h,bool bc3,vector<unsigned char> &out ){
	int bw=(w+3)/4,bh=(h+3)/4;
	unsigned char block[64],dest[16];
	for( int by=0;by<bh;++by ){
		for( int bx=0;bx<bw;++bx ){
			for( int y=0;y<4;++y ){
				int sy=min( by*4+y,h-1 );
				for( int x=0;x<4;++x ){
					int sx=min( bx*4+x,w-1 );
					memcpy( block+(y*4+x)*4,&rgba[(sy*w+sx)*4],4 );
					if( !bc3 ) block[(y*4+x)*4+3]=255;
				}
			}
			stb_compress_dxt_block( dest,block,bc3 ? 1 : 0,STB_DXT_HIGHQUAL );
			out.insert( out.end(),dest,dest+(bc3 ? 16 : 8) );
		}
	}
}

static bool writeFile( const fs::path &p,const vector<unsigned char> &data ){
	FILE *f=fopen( p.string().c_str(),"wb" );
	if( !f ) return false;
	fwrite( data.data(),1,data.size(),f );
	fclose( f );
	return true;
}

static bool convert( const fs::path &src ){
	int w,h,comp;
	unsigned char *data=stbi_load( src.string().c_str(),&w,&h,&comp,4 );
	if( !data ){
		fprintf( stderr,"texconv: can't load %s\n",src.string().c_str() );
		return false;
	}
	bool alpha=comp==4 || comp==2;

	//power-of-two stretch (nearest), as the runtime
	int tw=w,th=h;
	adjustTexSize( &tw,&th );
	vector<unsigned char> level( tw*th*4 );
	for( int y=0;y<th;++y ){
		int sy=(int)((long long)y*h/th);
		for( int x=0;x<tw;++x ){
			int sx=(int)((long long)x*w/tw);
			memcpy( &level[(y*tw+x)*4],data+(sy*w+sx)*4,4 );
		}
	}
	stbi_image_free( data );

	//mip chain (box filter, as gxCanvas::uploadTexture)
	vector<vector<unsigned char> > levels;
	vector<pair<int,int> > sizes;
	levels.push_back( level );
	sizes.push_back( make_pair( tw,th ) );
	int pw=tw,ph=th;
	while( pw>1 || ph>1 ){
		int nw=pw>1 ? pw/2 : 1,nh=ph>1 ? ph/2 : 1;
		const vector<unsigned char> &prev=levels.back();
		vector<unsigned char> next( nw*nh*4 );
		for( int y=0;y<nh;++y ){
			int y0=y*2<ph ? y*2 : ph-1,y1=y*2+1<ph ? y*2+1 : y0;
			for( int x=0;x<nw;++x ){
				int x0=x*2<pw ? x*2 : pw-1,x1=x*2+1<pw ? x*2+1 : x0;
				for( int c=0;c<4;++c ){
					unsigned t=prev[(y0*pw+x0)*4+c]+prev[(y0*pw+x1)*4+c]+prev[(y1*pw+x0)*4+c]+prev[(y1*pw+x1)*4+c];
					next[(y*nw+x)*4+c]=(t+2)/4;
				}
			}
		}
		levels.push_back( next );
		sizes.push_back( make_pair( nw,nh ) );
		pw=nw;ph=nh;
	}

	for( int pass=0;pass<(alpha ? 2 : 1);++pass ){
		bool bc3=pass==1;
		vector<unsigned char> out;
		out.insert( out.end(),{ 'W','B','C','1' } );
		put32( out,bc3 ? 3 : 1 );
		put32( out,tw );put32( out,th );
		put32( out,levels.size() );
		put32( out,0 );
		for( size_t k=0;k<levels.size();++k ){
			vector<unsigned char> blocks;
			compressLevel( levels[k],sizes[k].first,sizes[k].second,bc3,blocks );
			put32( out,blocks.size() );
			out.insert( out.end(),blocks.begin(),blocks.end() );
		}
		fs::path dst=src;
		dst+=bc3 ? ".wbc3" : ".wbc";
		if( !writeFile( dst,out ) ) return false;
	}
	return true;
}

static string lower( string s ){
	for( char &c : s ) c=tolower( (unsigned char)c );
	return s;
}

int main( int argc,char **argv ){
	if( argc<2 ){
		fprintf( stderr,"usage: texconv <content_dir> [skip_prefix ...]\n" );
		return 1;
	}
	fs::path root=argv[1];
	vector<string> skip;
	for( int k=2;k<argc;++k ) skip.push_back( lower( argv[k] ) );

	vector<fs::path> todo;
	for( auto &e : fs::recursive_directory_iterator( root ) ){
		if( !e.is_regular_file() ) continue;
		string ext=lower( e.path().extension().string() );
		if( ext!=".jpg" && ext!=".jpeg" && ext!=".png" && ext!=".bmp" && ext!=".tga" ) continue;
		string rel=lower( fs::relative( e.path(),root ).generic_string() );
		bool skipped=false;
		for( const string &s : skip ) if( rel.compare( 0,s.size(),s )==0 ) skipped=true;
		if( skipped ) continue;
		fs::path dst=e.path();dst+=".wbc";
		if( fs::exists( dst ) && fs::last_write_time( dst )>=fs::last_write_time( e.path() ) ) continue;
		todo.push_back( e.path() );
	}

	atomic<size_t> next( 0 );
	atomic<int> failed( 0 );
	mutex m;
	unsigned n=max( 1u,thread::hardware_concurrency() );
	vector<thread> threads;
	for( unsigned t=0;t<n;++t ){
		threads.emplace_back( [&](){
			for(;;){
				size_t k=next++;
				if( k>=todo.size() ) return;
				if( !convert( todo[k] ) ) ++failed;
			}
		} );
	}
	for( auto &t : threads ) t.join();
	printf( "texconv: %d images converted, %d failed\n",(int)todo.size(),(int)failed );
	return 0;
}
