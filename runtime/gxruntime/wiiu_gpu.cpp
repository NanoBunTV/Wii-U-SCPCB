#include "wiiu_gpu.h"

#include <coreinit/cache.h>
#include <coreinit/memdefaultheap.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <whb/gfx.h>
#include <whb/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <vector>

namespace wiiu {

GpuStats stats;

// ---- frames and fences ----

enum{ FRAME_HISTORY=8, TRANSIENT_FRAMES=3, TRANSIENT_SIZE=4*1024*1024 };

static uint32_t cur_frame=1;
static OSTime frame_stamp[FRAME_HISTORY];		//submission time stamp of each recent frame

struct Deferred{
	void *p;
	uint32_t frame;
};
static std::vector<Deferred> deferred;

struct TransientChunk{
	uint8_t *mem;
	size_t used;
};
static TransientChunk transient[TRANSIENT_FRAMES];

uint32_t gpuFrame(){
	return cur_frame;
}

bool gpuFrameBusy( uint32_t frame ){
	if( frame>=cur_frame ) return true;					//not submitted yet
	if( cur_frame-frame>=FRAME_HISTORY ) return false;	//long gone
	return GX2GetRetiredTimeStamp()<frame_stamp[frame%FRAME_HISTORY];
}

static void waitFrame( uint32_t frame ){
	if( !frame || frame>=cur_frame || cur_frame-frame>=FRAME_HISTORY ) return;
	GX2WaitTimeStamp( frame_stamp[frame%FRAME_HISTORY] );
}

void gpuEndFrame(){
	frame_stamp[cur_frame%FRAME_HISTORY]=GX2GetLastSubmittedTimeStamp();
	++cur_frame;

	//free what the GPU is done with
	size_t k=0;
	for( size_t n=0;n<deferred.size();++n ){
		if( gpuFrameBusy( deferred[n].frame ) ) deferred[k++]=deferred[n];
		else free( deferred[n].p );
	}
	deferred.resize( k );

	//recycle this frame's transient chunk : it was last used TRANSIENT_FRAMES frames ago
	TransientChunk &c=transient[cur_frame%TRANSIENT_FRAMES];
	if( cur_frame>TRANSIENT_FRAMES ) waitFrame( cur_frame-TRANSIENT_FRAMES );
	c.used=0;
}

void gpuSync(){
	++stats.syncs;
	GX2DrawDone();
}

// GPU memory comes from the malloc heap : wut gives most of MEM2 to it, leaving the default MEM2 heap small,
// and any MEM2 memory is GPU-accessible
void *gpuAlloc( size_t size,size_t align ){
	if( align<64 ) align=64;
	size=(size+63)&~63;
	void *p=memalign( align,size );
	if( !p ){
		WHBLogPrintf( "gpuAlloc: out of memory (%u bytes)",(unsigned)size );
		return 0;
	}
	//the heap memory may still have dirty lines in the CPU cache (from its previous use). If the GPU writes
	//this memory (texture copies, render targets) and the CPU evicts those lines later, they overwrite the GPU's
	//data : garbage blocks in textures, black render-target images (hardware only, Cemu has no cache)
	DCFlushRange( p,size );
	return p;
}

void gpuFree( void *p ){
	free( p );
}

void gpuFreeDeferred( void *p ){
	if( !p ) return;
	Deferred d={ p,cur_frame };
	deferred.push_back( d );
}

void *gpuTransient( size_t size,size_t align ){
	if( align<64 ) align=64;
	TransientChunk &c=transient[cur_frame%TRANSIENT_FRAMES];
	if( !c.mem ){
		c.mem=(uint8_t*)memalign( 256,TRANSIENT_SIZE );
		c.used=0;
	}
	size_t off=(c.used+align-1)&~(align-1);
	if( c.mem && off+size<=TRANSIENT_SIZE ){
		c.used=off+size;
		return c.mem+off;
	}
	if( c.mem && size<=TRANSIENT_SIZE ){
		//chunk full (e.g. many uploads while loading, without frames) : wait for the GPU, then reuse it
		gpuSync();
		c.used=size;
		return c.mem;
	}
	//bigger than a chunk : one-off allocation freed with the frame
	void *p=gpuAlloc( size,align );
	gpuFreeDeferred( p );
	return p;
}

void gpuFlushCpuWrites( void *p,size_t size,int mode ){
	GX2Invalidate( (GX2InvalidateMode)(mode|GX2_INVALIDATE_MODE_CPU),p,size );
}

void gpuInvalidateCpu( void *p,size_t size ){
	DCInvalidateRange( p,size );
}

// ---- shaders ----

static std::vector<uint8_t> readFile( const char *path ){
	std::vector<uint8_t> data;
	FILE *f=fopen( path,"rb" );
	if( !f ) return data;
	fseek( f,0,SEEK_END );
	long n=ftell( f );
	fseek( f,0,SEEK_SET );
	if( n>0 ){
		data.resize( n );
		if( fread( data.data(),1,n,f )!=(size_t)n ) data.clear();
	}
	fclose( f );
	return data;
}

// The .gsh data must stay alive : WHB's shaders point into copies it makes, so it can be freed after loading
bool loadShaderProgram( ShaderProgram *prog,const char *path ){
	memset( prog,0,sizeof(*prog) );
	std::vector<uint8_t> gsh=readFile( path );
	if( gsh.empty() ){
		WHBLogPrintf( "shader: can't read %s",path );
		return false;
	}
	if( !WHBGfxLoadGFDShaderGroup( &prog->group,0,gsh.data() ) ){
		WHBLogPrintf( "shader: bad GFD file %s",path );
		return false;
	}
	prog->vs=prog->group.vertexShader;
	prog->ps=prog->group.pixelShader;
	return true;
}

void addAttribute( ShaderProgram *prog,const char *name,uint32_t buffer,uint32_t offset,int format ){
	if( !WHBGfxInitShaderAttribute( &prog->group,name,buffer,offset,(GX2AttribFormat)format ) ){
		WHBLogPrintf( "shader: attribute %s not used",name );
	}
}

void finishFetchShader( ShaderProgram *prog ){
	WHBGfxInitFetchShader( &prog->group );
}

void useShaderProgram( const ShaderProgram *prog ){
	GX2SetFetchShader( &prog->group.fetchShader );
	GX2SetVertexShader( prog->vs );
	GX2SetPixelShader( prog->ps );
}

int ShaderProgram::vsUniform( const char *name )const{
	if( !vs ) return -1;
	GX2UniformVar *v=GX2GetVertexUniformVar( vs,name );
	return v ? (int)v->offset : -1;
}

int ShaderProgram::psUniform( const char *name )const{
	if( !ps ) return -1;
	GX2UniformVar *v=GX2GetPixelUniformVar( ps,name );
	return v ? (int)v->offset : -1;
}

int ShaderProgram::psSampler( const char *name )const{
	if( !ps ) return -1;
	for( uint32_t k=0;k<ps->samplerVarCount;++k ){
		if( !strcmp( ps->samplerVars[k].name,name ) ) return (int)ps->samplerVars[k].location;
	}
	return -1;
}

}
