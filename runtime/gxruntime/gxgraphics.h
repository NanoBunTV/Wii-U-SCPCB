// Wii U gxGraphics : GX2 device, canvases / fonts / meshes / scenes, the 2D batcher for drawing on the back
// buffer, and presenting the back buffer to the TV and the GamePad.
#ifndef GXGRAPHICS_H
#define GXGRAPHICS_H

#include <set>
#include <string>

#include "gxfont.h"
#include "gxcanvas.h"
#include "gxscene.h"
#include "gxmesh.h"
#include "gxmovie.h"
#include "wiiu_gpu.h"

class gxRuntime;

class gxGraphics{
public:
	gxGraphics( gxRuntime *runtime,int width,int height );
	~gxGraphics();

	gxRuntime *runtime;

	// --- backend interface ---
	// 2D drawing on a render-target canvas (batched, flushed on state changes and before 3D / flip)
	void draw2DRect( gxCanvas *target,const gxRect &r,unsigned argb );
	void draw2DImage( gxCanvas *target,const gxRect &dest,gxCanvas *src,const gxRect &src_r,bool masked,unsigned tint_argb );
	void draw2DGlyphs( gxCanvas *target,const gxRect &dest,gxCanvas *atlas,const gxRect &src_r,unsigned argb );
	void flush2D();
	// Clears a rectangle of a render target's colour and / or depth (camera viewports)
	void clearRect( gxCanvas *target,const gxRect &r,unsigned argb,bool clear_color,float z,bool clear_depth );
	// Makes `target` the current GX2 render target (sets colour / depth buffer, viewport and scissor)
	void bindTarget( gxCanvas *target,bool with_depth );
	gxCanvas *currentTarget()const{ return cur_target; }
	// Fallback 1x1 white texture for unused sampler slots
	GX2Texture *whiteTexture();
	GX2Sampler *pointSampler(){ return &point_sampler; }
	// Re-applies the GX2 context state after operations that clobber it (GX2CopySurface, presenting)
	void restoreState();
	// Before a canvas the GPU drew into is sampled as a texture : draws its pending 2D batch and flushes the
	// colour cache to memory (on hardware the texture unit otherwise reads stale memory; Cemu has no such caches)
	void syncTexture( gxCanvas *c );
	// Changes whenever another user of the GPU state (2D batcher, copies, present) ran : the scene
	// re-applies its shader and render state when it sees a new value
	static int stateEpoch();
	// Called after the back buffer is copied to the TV (screen 0) and GamePad (screen 1) pictures, before they are
	// shown : the software keyboard draws itself there (wiiu_keyboard.cpp). 0 = none.
	static void setPresentHook( void (*hook)( int screen ) );
	// Memory report ("memory :" log lines every 15 s while the total changes) : canvases, meshes and sounds from
	// here; the hook adds the 3D engine's part (bbblitz3d.cpp), writing at most size chars, returns the length
	static void setMemoryHook( int (*hook)( char *buf,int size ) );
	void logMemoryReport();

private:
	// back in the foreground (wiiu::appSetResumeCallback)
	static void onResume();

	gxCanvas *front_canvas,*back_canvas;
	gxFont *def_font;
	gxMesh *dummy_mesh;
	gxCanvas *cur_target;
	bool cur_target_depth;
	gxCanvas *white_canvas;
	enum{ CANVAS_TEXTURE_FLAGS=gxCanvas::CANVAS_TEXTURE };

	std::set<gxFont*> font_set;
	std::set<gxCanvas*> canvas_set;
	std::set<gxMesh*> mesh_set;
	std::set<gxScene*> scene_set;
	std::set<gxMovie*> movie_set;

	// 2D batcher
	struct Vertex2D{
		float x,y,u,v;
		unsigned rgba;
	};
	enum{ MODE_SOLID=0,MODE_IMAGE=1,MODE_MASKED=2,MODE_GLYPH=3 };
	wiiu::ShaderProgram prog2d;
	int u2d_screen,u2d_mode,u2d_mask;
	int s2d_tex;
	GX2Sampler point_sampler,linear_sampler;
	Vertex2D *batch;
	int batch_cnt,batch_max;
	int batch_mode;
	gxCanvas *batch_tex;
	gxCanvas *batch_target;
	unsigned batch_mask;
	void begin2D( gxCanvas *target,int mode,gxCanvas *tex,unsigned mask );
	void quad2D( float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,unsigned argb );

	// presenting
	void present( gxCanvas *src,GX2ColorBuffer *cb );

	// debug overlay (fps, memory), see gxgraphics.cpp
	void drawOverlay();

	// precompressed texture (tools/texconv sidecar file), 0 if missing
	gxCanvas *loadCompressed( const std::string &path,int flags );

	float gamma_r[256],gamma_g[256],gamma_b[256];

	/***** GX INTERFACE *****/
public:
	enum{
		GRAPHICS_WINDOWED=1,	//windowed mode
		GRAPHICS_SCALED=2,		//scaled window
		GRAPHICS_3D=4,			//3d mode! Hurrah!
		GRAPHICS_AUTOSUSPEND=8	//suspend graphics when app suspended
	};

	//MANIPULATORS
	void vwait();
	void flip( bool vwait );

	//SPECIAL!
	void copy( gxCanvas *dest,int dx,int dy,int dw,int dh,gxCanvas *src,int sx,int sy,int sw,int sh );

	//NEW! Gamma control!
	void setGamma( int r,int g,int b,float dr,float dg,float db );
	void getGamma( int r,int g,int b,float *dr,float *dg,float *db );
	void updateGamma( bool calibrate );

	//ACCESSORS
	int getWidth()const;
	int getHeight()const;
	int getDepth()const;
	int getScanLine()const;
	int getAvailVidmem()const;
	int getTotalVidmem()const;

	gxCanvas *getFrontCanvas()const;
	gxCanvas *getBackCanvas()const;
	gxFont *getDefaultFont()const;

	//OBJECTS
	gxCanvas *createCanvas( int width,int height,int flags );
	gxCanvas *loadCanvas( const std::string &file,int flags );
	gxCanvas *verifyCanvas( gxCanvas *canvas );
	void freeCanvas( gxCanvas *canvas );

	gxMovie *openMovie( const std::string &file,int flags );
	gxMovie *verifyMovie( gxMovie *movie );
	void closeMovie( gxMovie *movie );

	gxFont *loadFont( const std::string &font,int height,int flags );
	gxFont *verifyFont( gxFont *font );
	void freeFont( gxFont *font );

	gxScene *createScene( int flags );
	gxScene *verifyScene( gxScene *scene );
	void freeScene( gxScene *scene );

	gxMesh *createMesh( int max_verts,int max_tris,int flags );
	gxMesh *verifyMesh( gxMesh *mesh );
	void freeMesh( gxMesh *mesh );
};

#endif
