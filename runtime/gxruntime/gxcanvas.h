// Wii U gxCanvas : an image, texture or the back buffer.
//
// Storage :
//  - `pixels` : CPU copy in Blitz's ARGB format (native-endian uint32), used for software 2D drawing and
//    pixel access (LockBuffer / ReadPixel / WritePixel). Created on demand; LoadTexture'd textures drop it
//    after upload to save memory.
//  - `tex` : tiled GX2 texture (+ mip chain for mipmapped textures), created on first GPU use. CPU changes are
//    re-uploaded through a linear staging surface and GX2CopySurface (which handles tiling per mip level).
//  - Render targets (the back buffer, or a texture receiving CopyRect) also get a colour buffer aliasing `tex`.
// Whoever wrote last (CPU or GPU) is tracked so each side syncs before reading.
//
// 2D drawing onto the back buffer goes to the GPU (gxGraphics' 2D batcher); onto any other canvas it's done
// in software on `pixels`, like DirectDraw blits were.
#ifndef GXCANVAS_H
#define GXCANVAS_H

#include <string>
#include <gx2/surface.h>
#include <gx2/texture.h>

class gxFont;
class gxGraphics;

struct gxRect{
	int left,top,right,bottom;
	gxRect(){}
	gxRect( int x,int y,int w,int h ):left(x),top(y),right(x+w),bottom(y+h){}
};

class gxCanvas{
public:
	gxCanvas( gxGraphics *graphics,int width,int height,int flags );
	~gxCanvas();

	// --- backend interface ---
	bool isRenderTarget()const{ return render_target; }
	// GPU texture, uploaded and mipmapped. 0 if it can't be created.
	GX2Texture *getTexture();
	// Colour buffer aliasing the texture (level 0); makes the canvas a render target
	GX2ColorBuffer *getColorBuffer();
	GX2DepthBuffer *getDepthBuffer();
	// Called after the GPU wrote to the colour buffer
	void gpuModified();
	// CPU pixel array (ARGB), creating / syncing it
	unsigned *cpuPixels();
	// Called after the CPU wrote to the pixel array
	void cpuModified();
	// Replace the contents with a decoded image (ARGB, w*h), e.g. from the image loader
	void setPixels( const unsigned *argb,int w,int h );
	// Drop the CPU copy after uploading (loaded 3D textures)
	void releaseCpuCopy();
	// GPU-only compressed texture (DDS). Takes ownership of the surface memory.
	void setCompressed( const GX2Surface &surface );

	void backup()const{}		// device loss doesn't exist on the Wii U
	void restore()const{}
	int getModify()const{ return mod_cnt; }
	void setModify( int n ){ mod_cnt=n; }

	bool clip( gxRect *d )const;
	bool clip( gxRect *d,gxRect *s )const;

	// render-target canvas set up by gxGraphics (back buffer)
	void makeBackBuffer();

	unsigned maskARGB()const{ return mask_argb; }
	int handleX()const{ return handle_x; }
	int handleY()const{ return handle_y; }

private:
	friend class gxGraphics;

	gxGraphics *graphics;
	int flags,cube_mode,width,height;
	bool render_target;

	unsigned *pixels;			// CPU copy (ARGB), may be 0
	bool cpu_dirty;				// pixels changed since the last upload
	bool gpu_dirty;				// GPU wrote since the CPU copy was made
	bool gpu_only;				// compressed / dropped CPU copy
	bool gpu_drawn;				// drawn into by the GPU since its texture was last sampled (see gxGraphics::syncTexture)
	int mod_cnt;
	mutable int locked_cnt;

	GX2Texture tex;
	bool tex_valid;
	GX2ColorBuffer color_buf;
	bool color_valid;
	GX2DepthBuffer depth_buf;
	bool depth_valid;

	mutable unsigned *cm_mask;	// collision bit mask
	mutable int cm_pitch;

	gxFont *font;
	gxRect viewport,clip_rect;
	int origin_x,origin_y,handle_x,handle_y;
	unsigned mask_argb,color_argb,clsColor_argb;

	bool createTexture();
	void uploadTexture();
	void readBack();
	void updateBitMask()const;

	// software rendering helpers (non render-target canvases)
	void fillRect( const gxRect &r,unsigned argb );

	/***** GX INTERFACE *****/
public:
	enum{
		CANVAS_TEX_RGB=			0x0001,
		CANVAS_TEX_ALPHA=		0x0002,
		CANVAS_TEX_MASK=		0x0004,
		CANVAS_TEX_MIPMAP=		0x0008,
		CANVAS_TEX_CLAMPU=		0x0010,
		CANVAS_TEX_CLAMPV=		0x0020,
		CANVAS_TEX_SPHERE=		0x0040,
		CANVAS_TEX_CUBE=		0x0080,
		CANVAS_TEX_VIDMEM=		0x0100,
		CANVAS_TEX_HICOLOR=		0x0200,

		CANVAS_TEXTURE=			0x10000,
		CANVAS_NONDISPLAY=		0x20000,
		CANVAS_HIGHCOLOR=		0x40000
	};

	enum{
		CUBEMODE_REFLECTION=1,
		CUBEMODE_NORMAL=2,
		CUBEMODE_POSITION=3,

		CUBESPACE_WORLD=0,
		CUBESPACE_CAMERA=4
	};

	//MANIPULATORS
	void setFont( gxFont *font );
	void setMask( unsigned argb );
	void setColor( unsigned argb );
	void setClsColor( unsigned argb );
	void setOrigin( int x,int y );
	void setHandle( int x,int y );
	void setViewport( int x,int y,int w,int h );

	void cls();
	void plot( int x,int y );
	void line( int x,int y,int x2,int y2 );
	void rect( int x,int y,int w,int h,bool solid );
	void oval( int x,int y,int w,int h,bool solid );
	void text( int x,int y,const std::string &t );
	void blit( int x,int y,gxCanvas *src,int src_x,int src_y,int src_w,int src_h,bool solid );

	bool collide( int x,int y,const gxCanvas *src,int src_x,int src_y,bool solid )const;
	bool rect_collide( int x,int y,int rect_x,int rect_y,int rect_w,int rect_h,bool solid )const;

	bool lock()const;
	void setPixel( int x,int y,unsigned argb );
	void setPixelFast( int x,int y,unsigned argb ){
		pixels[y*width+x]=argb;
	}
	void copyPixel( int x,int y,gxCanvas *src,int src_x,int src_y );
	void copyPixelFast( int x,int y,gxCanvas *src,int src_x,int src_y );
	unsigned getPixel( int x,int y )const;
	unsigned getPixelFast( int x,int y )const{
		return pixels[y*width+x];
	}
	void unlock()const;

	void setCubeMode( int mode );
	void setCubeFace( int face );

	//ACCESSORS
	int getWidth()const{ return width; }
	int getHeight()const{ return height; }
	int getDepth()const{ return 32; }
	int getFlags()const{ return flags; }
	int cubeMode()const{ return cube_mode; }
	void getOrigin( int *x,int *y )const;
	void getHandle( int *x,int *y )const;
	void getViewport( int *x,int *y,int *w,int *h )const;
	unsigned getMask()const{ return mask_argb; }
	unsigned getColor()const{ return color_argb; }
	unsigned getClsColor()const{ return clsColor_argb; }
};

#endif
