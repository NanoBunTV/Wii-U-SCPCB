// Wii U GPU helpers shared by the gxruntime classes : GPU memory, frame fences, per-frame transient memory,
// shader loading.
#ifndef WIIU_GPU_H
#define WIIU_GPU_H

#include <cstddef>
#include <cstdint>
#include <gx2/shaders.h>
#include <whb/gfx.h>

namespace wiiu {

// MEM2 allocations usable by the GPU (vertex / index buffers, textures, render targets)
void *gpuAlloc( size_t size,size_t align );
void gpuFree( void *p );
// Frees once the GPU has finished every command queued so far
void gpuFreeDeferred( void *p );

// Frame counter : resources record the frame they were last used in by the GPU
uint32_t gpuFrame();
// True if work queued during frame `frame` may still be read by the GPU
bool gpuFrameBusy( uint32_t frame );
// Called right after the frame's commands are submitted (after the swap)
void gpuEndFrame();
// Waits for the GPU to finish everything queued so far
void gpuSync();

// Transient GPU-visible memory valid until the GPU has finished the current frame
void *gpuTransient( size_t size,size_t align );

// GPU timing (sampled frames) : gpuMarkFrame() starts a sampled frame, gpuMark( name ) records a GPU timestamp
// when the GPU gets there (GX2SampleBottomGPUCycle), gpuMarkCollect() (after GX2DrawDone) turns them into GPU time
// per part (time since the previous mark, summed per name), gpuMarkReport() gives the averages and resets.
void gpuMarkFrame();
void gpuMark( const char *name );
void gpuMarkCollect();
bool gpuMarkReport( char *buf,size_t size );

// CPU <-> GPU coherency
void gpuFlushCpuWrites( void *p,size_t size,int invalidate_mode );   // after CPU writes, before GPU reads
void gpuInvalidateCpu( void *p,size_t size );                        // before CPU reads GPU-written memory

// per-second performance counters (logged by gxGraphics::flip)
struct GpuStats{
	unsigned uploads,upload_bytes,readbacks,syncs,flushes2d,draws,frames;
	unsigned tris;
	//time per category in microseconds : UpdateWorld, RenderWorld (CPU side), the whole frame from flip to flip,
	//and in Flip waiting for the previous frame to be shown (vsync / GPU) and for the GPU to finish this one
	unsigned update_us,render_us,frame_us,flip_wait_us,gpu_wait_us;
	//every 30th frame, Flip waits for the GPU right after submitting : the GPU time left after the CPU's part
	unsigned gpu_samples;
};
extern GpuStats stats;

struct ShaderProgram{
	WHBGfxShaderGroup group;
	GX2VertexShader *vs;
	GX2PixelShader *ps;

	// Uniform register offsets (-1 if the shader doesn't use it)
	int vsUniform( const char *name )const;
	int psUniform( const char *name )const;
	int psSampler( const char *name )const;
};

// Loads a vertex / pixel shader pair from a CafeGLSL .gsh file in the content folder
bool loadShaderProgram( ShaderProgram *prog,const char *gsh_path );
// Attribute setup, then finishFetchShader()
void addAttribute( ShaderProgram *prog,const char *name,uint32_t buffer,uint32_t offset,int format );
void finishFetchShader( ShaderProgram *prog );
void useShaderProgram( const ShaderProgram *prog );

}

#endif
