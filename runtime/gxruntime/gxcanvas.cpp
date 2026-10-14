#include "std.h"
#include "gxcanvas.h"
#include "gxgraphics.h"
#include "gxfont.h"
#include "wiiu_gpu.h"

#include <gx2/mem.h>
#include <gx2/surface.h>
#include <gx2/draw.h>
#include <gx2/utils.h>
#include <whb/log.h>

static inline unsigned argbToRgba( unsigned argb ){
	return (argb<<8)|(argb>>24);
}
static inline unsigned rgbaToArgb( unsigned rgba ){
	return (rgba>>8)|(rgba<<24);
}

static bool clipRect( const gxRect &viewport,gxRect *d ){
	if( d->right<=d->left ||
		d->bottom<=d->top ||
		d->left>=viewport.right ||
		d->right<=viewport.left ||
		d->top>=viewport.bottom ||
		d->bottom<=viewport.top ) return false;
	if( d->left<viewport.left ) d->left=viewport.left;
	if( d->right>viewport.right ) d->right=viewport.right;
	if( d->top<viewport.top ) d->top=viewport.top;
	if( d->bottom>viewport.bottom ) d->bottom=viewport.bottom;
	return true;
}

static bool clipRect( const gxRect &viewport,gxRect *d,gxRect *s ){
	if( d->right<=d->left ||
		d->bottom<=d->top ||
		d->left>=viewport.right ||
		d->right<=viewport.left ||
		d->top>=viewport.bottom ||
		d->bottom<=viewport.top ) return false;
	int dx,dy;
	if( (dx=viewport.left-d->left)>0 ){ d->left+=dx;s->left+=dx; }
	if( (dx=viewport.right-d->right)<0 ){ d->right+=dx;s->right+=dx; }
	if( (dy=viewport.top-d->top)>0 ){ d->top+=dy;s->top+=dy; }
	if( (dy=viewport.bottom-d->bottom)<0 ){ d->bottom+=dy;s->bottom+=dy; }
	return true;
}

gxCanvas::gxCanvas( gxGraphics *g,int w,int h,int f ):
graphics(g),flags(f),cube_mode(CUBEMODE_REFLECTION|CUBESPACE_WORLD),width(w),height(h),
render_target(false),pixels(0),cpu_dirty(false),gpu_dirty(false),gpu_only(false),mod_cnt(0),locked_cnt(0),
tex_valid(false),color_valid(false),depth_valid(false),cm_mask(0),cm_pitch(0),font(0){
	memset( &tex,0,sizeof(tex) );
	memset( &color_buf,0,sizeof(color_buf) );
	memset( &depth_buf,0,sizeof(depth_buf) );
	clip_rect=gxRect( 0,0,w,h );
	setMask( 0 );
	setColor( ~0 );
	setClsColor( 0 );
	setOrigin( 0,0 );
	setHandle( 0,0 );
	setFont( graphics->getDefaultFont() );
	setViewport( 0,0,w,h );
}

gxCanvas::~gxCanvas(){
	delete[] cm_mask;
	delete[] pixels;
	if( tex_valid ){
		wiiu::gpuFreeDeferred( tex.surface.image );
		wiiu::gpuFreeDeferred( tex.surface.mipmaps );
	}
	if( depth_valid ) wiiu::gpuFreeDeferred( depth_buf.surface.image );
}

void gxCanvas::makeBackBuffer(){
	render_target=true;
	delete[] pixels;pixels=0;
	createTexture();
	getColorBuffer();
	getDepthBuffer();
}

// ---- GPU side ----

static int mipLevelCount( int w,int h ){
	int n=1;
	while( w>1 || h>1 ){ w=w>1 ? w/2 : 1;h=h>1 ? h/2 : 1;++n; }
	return n;
}

bool gxCanvas::createTexture(){
	if( tex_valid ) return true;
	GX2Surface &s=tex.surface;
	s.dim=GX2_SURFACE_DIM_TEXTURE_2D;
	s.width=width;
	s.height=height;
	s.depth=1;
	s.mipLevels=(flags & CANVAS_TEX_MIPMAP) && !render_target ? mipLevelCount( width,height ) : 1;
	s.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	s.aa=GX2_AA_MODE1X;
	s.use=(GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE|GX2_SURFACE_USE_COLOR_BUFFER);
	s.tileMode=GX2_TILE_MODE_DEFAULT;
	s.swizzle=0;
	GX2CalcSurfaceSizeAndAlignment( &s );
	tex.viewFirstMip=0;
	tex.viewNumMips=s.mipLevels;
	tex.viewFirstSlice=0;
	tex.viewNumSlices=1;
	tex.compMap=GX2_COMP_MAP( GX2_SQ_SEL_R,GX2_SQ_SEL_G,GX2_SQ_SEL_B,GX2_SQ_SEL_A );
	GX2InitTextureRegs( &tex );
	s.image=wiiu::gpuAlloc( s.imageSize,s.alignment );
	if( !s.image ) return false;
	if( s.mipLevels>1 ){
		s.mipmaps=wiiu::gpuAlloc( s.mipmapSize,s.alignment );
		if( !s.mipmaps ){ wiiu::gpuFree( s.image );return false; }
	}else s.mipmaps=0;
	tex_valid=true;
	cpu_dirty=pixels!=0;
	return true;
}

// Copies one level of ARGB pixels into the texture through a linear staging surface
static void uploadLevel( GX2Texture *tex,int level,const unsigned *src,int w,int h ){
	GX2Surface st;
	memset( &st,0,sizeof(st) );
	st.dim=GX2_SURFACE_DIM_TEXTURE_2D;
	st.width=w;st.height=h;st.depth=1;st.mipLevels=1;
	st.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	st.aa=GX2_AA_MODE1X;
	st.use=GX2_SURFACE_USE_TEXTURE;
	st.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &st );
	//small uploads use the per-frame ring; big ones (texture loading) get their own buffer, freed once
	//the copy is done, so loading many textures without presenting frames doesn't pile staging memory up
	bool big=st.imageSize>1024*1024;
	st.image=big ? wiiu::gpuAlloc( st.imageSize,st.alignment ) : wiiu::gpuTransient( st.imageSize,st.alignment );
	if( !st.image ) return;
	unsigned *dst=(unsigned*)st.image;
	for( int y=0;y<h;++y ){
		unsigned *d=dst+y*st.pitch;
		const unsigned *s=src+y*w;
		for( int x=0;x<w;++x ) d[x]=argbToRgba( s[x] );
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_TEXTURE,st.image,st.imageSize );
	GX2CopySurface( &st,0,0,&tex->surface,level,0 );
	if( big ){
		wiiu::gpuSync();
		wiiu::gpuFree( st.image );
	}
}

void gxCanvas::uploadTexture(){
	if( !pixels ) return;
	++wiiu::stats.uploads;
	wiiu::stats.upload_bytes+=width*height*4;
	graphics->flush2D();
	uploadLevel( &tex,0,pixels,width,height );
	if( tex.surface.mipLevels>1 ){
		//box-filtered mip chain, built on the CPU
		vector<unsigned> prev( pixels,pixels+width*height ),next;
		int pw=width,ph=height;
		for( int level=1;level<(int)tex.surface.mipLevels;++level ){
			int w=pw>1 ? pw/2 : 1,h=ph>1 ? ph/2 : 1;
			next.resize( w*h );
			for( int y=0;y<h;++y ){
				int y0=y*2<ph ? y*2 : ph-1,y1=y*2+1<ph ? y*2+1 : y0;
				for( int x=0;x<w;++x ){
					int x0=x*2<pw ? x*2 : pw-1,x1=x*2+1<pw ? x*2+1 : x0;
					unsigned a=prev[y0*pw+x0],b=prev[y0*pw+x1],c=prev[y1*pw+x0],d=prev[y1*pw+x1];
					unsigned r=0;
					for( int sh=0;sh<32;sh+=8 ){
						unsigned t=((a>>sh)&255)+((b>>sh)&255)+((c>>sh)&255)+((d>>sh)&255);
						r|=((t+2)/4)<<sh;
					}
					next[y*w+x]=r;
				}
			}
			uploadLevel( &tex,level,next.data(),w,h );
			prev.swap( next );
			pw=w;ph=h;
		}
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE,tex.surface.image,tex.surface.imageSize );
	if( tex.surface.mipmaps ) GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE,tex.surface.mipmaps,tex.surface.mipmapSize );
	graphics->restoreState();
	cpu_dirty=false;
}

GX2Texture *gxCanvas::getTexture(){
	if( !createTexture() ) return 0;
	if( cpu_dirty ) uploadTexture();
	return &tex;
}

GX2ColorBuffer *gxCanvas::getColorBuffer(){
	if( !createTexture() ) return 0;
	if( cpu_dirty ) uploadTexture();
	if( !color_valid ){
		color_buf.surface=tex.surface;
		color_buf.surface.mipLevels=1;
		color_buf.surface.mipmaps=0;
		color_buf.viewMip=0;
		color_buf.viewFirstSlice=0;
		color_buf.viewNumSlices=1;
		GX2InitColorBufferRegs( &color_buf );
		color_valid=true;
	}
	return &color_buf;
}

GX2DepthBuffer *gxCanvas::getDepthBuffer(){
	if( !depth_valid ){
		GX2Surface &s=depth_buf.surface;
		s.dim=GX2_SURFACE_DIM_TEXTURE_2D;
		s.width=width;s.height=height;s.depth=1;s.mipLevels=1;
		s.format=GX2_SURFACE_FORMAT_FLOAT_D24_S8;
		s.aa=GX2_AA_MODE1X;
		s.use=GX2_SURFACE_USE_DEPTH_BUFFER;
		s.tileMode=GX2_TILE_MODE_DEFAULT;
		GX2CalcSurfaceSizeAndAlignment( &s );
		depth_buf.viewMip=0;
		depth_buf.viewFirstSlice=0;
		depth_buf.viewNumSlices=1;
		depth_buf.depthClear=1.0f;
		depth_buf.stencilClear=0;
		GX2InitDepthBufferRegs( &depth_buf );
		s.image=wiiu::gpuAlloc( s.imageSize,s.alignment );
		if( !s.image ) return 0;
		depth_valid=true;
	}
	return &depth_buf;
}

void gxCanvas::gpuModified(){
	gpu_dirty=!render_target;	//the back buffer has no CPU copy
	++mod_cnt;
	delete[] cm_mask;cm_mask=0;
}

void gxCanvas::readBack(){
	//GPU -> CPU copy of level 0 (works on hardware; Cemu may not reflect GPU writes in CPU memory)
	++wiiu::stats.readbacks;
	graphics->flush2D();
	GX2Surface st;
	memset( &st,0,sizeof(st) );
	st.dim=GX2_SURFACE_DIM_TEXTURE_2D;
	st.width=width;st.height=height;st.depth=1;st.mipLevels=1;
	st.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	st.aa=GX2_AA_MODE1X;
	st.use=GX2_SURFACE_USE_TEXTURE;
	st.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
	GX2CalcSurfaceSizeAndAlignment( &st );
	st.image=wiiu::gpuAlloc( st.imageSize,st.alignment );
	if( !st.image ) return;
	GX2Invalidate( GX2_INVALIDATE_MODE_COLOR_BUFFER,tex.surface.image,tex.surface.imageSize );
	GX2CopySurface( &tex.surface,0,0,&st,0,0 );
	graphics->restoreState();
	wiiu::gpuSync();
	wiiu::gpuInvalidateCpu( st.image,st.imageSize );
	if( !pixels ) pixels=d_new unsigned[width*height];
	const unsigned *src=(const unsigned*)st.image;
	for( int y=0;y<height;++y ){
		for( int x=0;x<width;++x ) pixels[y*width+x]=rgbaToArgb( src[y*st.pitch+x] );
	}
	wiiu::gpuFree( st.image );
	gpu_dirty=false;
}

unsigned *gxCanvas::cpuPixels(){
	if( !pixels ){
		if( tex_valid && (gpu_dirty || gpu_only) ){
			readBack();
		}else{
			pixels=d_new unsigned[width*height];
			memset( pixels,0,width*height*4 );
		}
	}else if( gpu_dirty ){
		readBack();
	}
	return pixels;
}

void gxCanvas::cpuModified(){
	cpu_dirty=tex_valid;
	++mod_cnt;
	delete[] cm_mask;cm_mask=0;
}

void gxCanvas::setPixels( const unsigned *argb,int w,int h ){
	if( w!=width || h!=height ) return;
	if( !pixels ) pixels=d_new unsigned[width*height];
	memcpy( pixels,argb,width*height*4 );
	gpu_dirty=false;
	cpuModified();
}

void gxCanvas::releaseCpuCopy(){
	if( !pixels || render_target ) return;
	if( !getTexture() ) return;
	delete[] pixels;pixels=0;
	gpu_only=true;
}

void gxCanvas::setCompressed( const GX2Surface &surface ){
	if( tex_valid ){
		wiiu::gpuFreeDeferred( tex.surface.image );
		wiiu::gpuFreeDeferred( tex.surface.mipmaps );
	}
	tex.surface=surface;
	tex.viewFirstMip=0;
	tex.viewNumMips=surface.mipLevels;
	tex.viewFirstSlice=0;
	tex.viewNumSlices=1;
	tex.compMap=GX2_COMP_MAP( GX2_SQ_SEL_R,GX2_SQ_SEL_G,GX2_SQ_SEL_B,GX2_SQ_SEL_A );
	GX2InitTextureRegs( &tex );
	tex_valid=true;
	delete[] pixels;pixels=0;
	gpu_only=true;
	cpu_dirty=false;
}

// ---- state ----

bool gxCanvas::clip( gxRect *d )const{
	return clipRect( viewport,d );
}

bool gxCanvas::clip( gxRect *d,gxRect *s )const{
	return clipRect( viewport,d,s );
}

void gxCanvas::setFont( gxFont *f ){
	font=f;
}

void gxCanvas::setMask( unsigned argb ){
	mask_argb=argb&0xffffff;
	delete[] cm_mask;cm_mask=0;
}

void gxCanvas::setColor( unsigned argb ){
	color_argb=argb;
}

void gxCanvas::setClsColor( unsigned argb ){
	clsColor_argb=argb;
}

void gxCanvas::setOrigin( int x,int y ){
	origin_x=x;origin_y=y;
}

void gxCanvas::setHandle( int x,int y ){
	handle_x=x;handle_y=y;
}

void gxCanvas::setViewport( int x,int y,int w,int h ){
	gxRect r( x,y,w,h );
	if( !clipRect( clip_rect,&r ) ) r=gxRect( 0,0,0,0 );
	viewport=r;
}

void gxCanvas::getOrigin( int *x,int *y )const{
	*x=origin_x;*y=origin_y;
}

void gxCanvas::getHandle( int *x,int *y )const{
	*x=handle_x;*y=handle_y;
}

void gxCanvas::getViewport( int *x,int *y,int *w,int *h )const{
	*x=viewport.left;*y=viewport.top;
	*w=viewport.right-viewport.left;*h=viewport.bottom-viewport.top;
}

void gxCanvas::setCubeMode( int mode ){
	cube_mode=mode;
}

void gxCanvas::setCubeFace( int face ){
	//cube maps aren't supported yet : all faces share the one surface
}

// ---- rendering primitives ----

void gxCanvas::fillRect( const gxRect &r,unsigned argb ){
	if( render_target ){
		graphics->draw2DRect( this,r,argb );
		gpuModified();
		return;
	}
	unsigned *p=cpuPixels();
	for( int y=r.top;y<r.bottom;++y ){
		unsigned *row=p+y*width;
		for( int x=r.left;x<r.right;++x ) row[x]=argb;
	}
	cpuModified();
}

void gxCanvas::cls(){
	if( viewport.right>viewport.left && viewport.bottom>viewport.top ) fillRect( viewport,clsColor_argb );
}

void gxCanvas::plot( int x,int y ){
	x+=origin_x;if( x<viewport.left || x>=viewport.right ) return;
	y+=origin_y;if( y<viewport.top || y>=viewport.bottom ) return;
	fillRect( gxRect( x,y,1,1 ),color_argb );
}

void gxCanvas::line( int x0,int y0,int x1,int y1 ){
	int ddf,padj,sadj;
	int dx,dy,sx,sy,ax,ay;

	x0+=origin_x;y0+=origin_y;
	x1+=origin_x;y1+=origin_y;

	int cx0,cx1,cy0,cy1,clip0,clip1;

	cx0=viewport.left;
	cx1=viewport.right-1;
	cy0=viewport.top;
	cy1=viewport.bottom-1;

	while( true ){
		clip0=0;clip1=0;

		if(y0>cy1)clip0|=1;else if(y0<cy0)clip0|=2;
		if(x0>cx1)clip0|=4;else if(x0<cx0)clip0|=8;
		if(y1>cy1)clip1|=1;else if(y1<cy0)clip1|=2;
		if(x1>cx1)clip1|=4;else if(x1<cx0)clip1|=8;

		if((clip0|clip1)==0) break;		//draw line
		if((clip0&clip1)!=0) return;	//outside

		if((clip0&1)==1) {x0=x0+((x1-x0)*(cy1-y0))/(y1-y0);y0=cy1;continue;}
		if((clip0&2)==2) {x0=x0+((x1-x0)*(cy0-y0))/(y1-y0);y0=cy0;continue;}
		if((clip0&4)==4) {y0=y0+((y1-y0)*(cx1-x0))/(x1-x0);x0=cx1;continue;}
		if((clip0&8)==8) {y0=y0+((y1-y0)*(cx0-x0))/(x1-x0);x0=cx0;continue;}

		if((clip1&1)==1) {x1=x0+((x1-x0)*(cy1-y0))/(y1-y0);y1=cy1;continue;}
		if((clip1&2)==2) {x1=x0+((x1-x0)*(cy0-y0))/(y1-y0);y1=cy0;continue;}
		if((clip1&4)==4) {y1=y0+((y1-y0)*(cx1-x0))/(x1-x0);x1=cx1;continue;}
		if((clip1&8)==8) {y1=y0+((y1-y0)*(cx0-x0))/(x1-x0);x1=cx0;continue;}
	}

	dx=x1-x0;dy=y1-y0;

	if( render_target ){
		//horizontal / vertical runs become rects, anything else is drawn pixel by pixel
		if( dx==0 || dy==0 ){
			int l=x0<x1 ? x0 : x1,t=y0<y1 ? y0 : y1;
			graphics->draw2DRect( this,gxRect( l,t,abs(dx)+1,abs(dy)+1 ),color_argb );
			gpuModified();
			return;
		}
	}

	if (dx>=0) {sx=1;ax=dx;} else {sx=-1;ax=-dx;}
	if (dy>=0) {sy=1;ay=dy;} else {sy=-1;ay=-dy;}

	if( render_target ){
		if( ax>ay ){
			ddf=-ax;sadj=ax+ax;padj=ay+ay;
			while( ax-->=0 ){
				graphics->draw2DRect( this,gxRect( x0,y0,1,1 ),color_argb );
				x0+=sx;ddf+=padj;if( ddf>=0 ){ y0+=sy;ddf-=sadj; }
			}
		}else{
			ddf=-ay;sadj=ay+ay;padj=ax+ax;
			while( ay-->=0 ){
				graphics->draw2DRect( this,gxRect( x0,y0,1,1 ),color_argb );
				y0+=sy;ddf+=padj;if( ddf>=0 ){ x0+=sx;ddf-=sadj; }
			}
		}
		gpuModified();
		return;
	}

	unsigned *p=cpuPixels();
	if( ax>ay ){
		ddf=-ax;sadj=ax+ax;padj=ay+ay;
		while( ax-->=0 ){
			p[y0*width+x0]=color_argb;
			x0+=sx;ddf+=padj;if( ddf>=0 ){ y0+=sy;ddf-=sadj; }
		}
	}else{
		ddf=-ay;sadj=ay+ay;padj=ax+ax;
		while( ay-->=0 ){
			p[y0*width+x0]=color_argb;
			y0+=sy;ddf+=padj;if( ddf>=0 ){ x0+=sx;ddf-=sadj; }
		}
	}
	cpuModified();
}

void gxCanvas::rect( int x,int y,int w,int h,bool solid ){
	x+=origin_x;y+=origin_y;
	gxRect dest( x,y,w,h );
	if( !clip( &dest ) ) return;

	if( solid ){
		fillRect( dest,color_argb );
		return;
	}
	gxRect r1( x,y,w,1 );if( clip( &r1 ) ) fillRect( r1,color_argb );
	gxRect r2( x,y,1,h );if( clip( &r2 ) ) fillRect( r2,color_argb );
	gxRect r3( x+w-1,y,1,h );if( clip( &r3 ) ) fillRect( r3,color_argb );
	gxRect r4( x,y+h-1,w,1 );if( clip( &r4 ) ) fillRect( r4,color_argb );
}

void gxCanvas::oval( int x1,int y1,int w,int h,bool solid ){
	x1+=origin_x;y1+=origin_y;
	gxRect dest( x1,y1,w,h );
	if( !clip( &dest ) ) return;

	float xr=w*.5f,yr=h*.5f,ar=(float)w/(float)h;
	float cx=x1+xr+.5f,cy=y1+yr-.5f,rsq=yr*yr,y;

	if( solid ){
		y=dest.top-cy;
		for( int t=dest.top;t<dest.bottom;++y,++t ){
			float x=sqrt( rsq-y*y )*ar;
			int xa=floor( cx-x ),xb=floor( cx+x );
			if( xb<=xa || xa>=viewport.right || xb<=viewport.left ) continue;
			gxRect dr;dr.top=t;dr.bottom=t+1;
			dr.left=xa<viewport.left ? viewport.left : xa;
			dr.right=xb>viewport.right ? viewport.right : xb;
			fillRect( dr,color_argb );
		}
		return;
	}

	int p_xa,p_xb,t,hh=floor(cy);

	p_xa=p_xb=cx;
	t=dest.top;y=t-cy;
	if( dest.top>y1 ){ --t;--y; }
	for( ;t<=hh;++y,++t ){
		float x=sqrt( rsq-y*y )*ar;
		int xa=floor( cx-x ),xb=floor( cx+x );
		gxRect r1( xa,t,p_xa-xa,1 );if( r1.right<=r1.left ) r1.right=r1.left+1;
		if( clip( &r1 ) ) fillRect( r1,color_argb );
		gxRect r2( p_xb,t,xb-p_xb,1 );if( r2.left>=r2.right ) r2.left=r2.right-1;
		if( clip( &r2 ) ) fillRect( r2,color_argb );
		p_xa=xa;p_xb=xb;
	}

	p_xa=p_xb=cx;
	t=dest.bottom-1;y=t-cy;
	if( dest.bottom<y1+h ){ ++t;++y; }
	for( ;t>hh;--y,--t ){
		float x=sqrt( rsq-y*y )*ar;
		int xa=floor( cx-x ),xb=floor( cx+x );
		gxRect r1( xa,t,p_xa-xa,1 );if( r1.right<=r1.left ) r1.right=r1.left+1;
		if( clip( &r1 ) ) fillRect( r1,color_argb );
		gxRect r2( p_xb,t,xb-p_xb,1 );if( r2.left>=r2.right ) r2.left=r2.right-1;
		if( clip( &r2 ) ) fillRect( r2,color_argb );
		p_xa=xa;p_xb=xb;
	}
}

void gxCanvas::blit( int x,int y,gxCanvas *src,int src_x,int src_y,int src_w,int src_h,bool solid ){
	x+=origin_x-src->handle_x;
	y+=origin_y-src->handle_y;
	gxRect dest_r( x,y,src_w,src_h ),src_r( src_x,src_y,src_w,src_h );
	if( !clip( &dest_r,&src_r ) ) return;
	if( !clipRect( src->clip_rect,&src_r,&dest_r ) ) return;

	if( render_target || src->render_target ){
		//GPU copy (the back buffer is involved)
		graphics->draw2DImage( this,dest_r,src,src_r,!solid,0xffffffff );
		gpuModified();
		return;
	}

	const unsigned *s=src->cpuPixels();
	unsigned *d=cpuPixels();
	int w=dest_r.right-dest_r.left,h=dest_r.bottom-dest_r.top;
	unsigned mask=src->mask_argb;
	for( int j=0;j<h;++j ){
		const unsigned *sr=s+(src_r.top+j)*src->width+src_r.left;
		unsigned *dr=d+(dest_r.top+j)*width+dest_r.left;
		if( solid ){
			memmove( dr,sr,w*4 );
		}else{
			for( int i=0;i<w;++i ){
				unsigned c=sr[i];
				if( (c&0xffffff)!=mask ) dr[i]=c;
			}
		}
	}
	cpuModified();
}

void gxCanvas::text( int x,int y,const string &t ){
	if( !font ) return;

	int ty=y+origin_y;
	if( ty>=viewport.bottom ) return;
	if( ty+font->getHeight()<=viewport.top ) return;

	int tx=x+origin_x;
	if( tx>=viewport.right ) return;

	int b=0,w;
	while( b<(int)t.size() && tx+(w=font->charWidth( t[b]&0xff ))<=viewport.left ){
		tx+=w;x+=w;++b;
	}
	int e=b;
	while( e<(int)t.size() && tx<viewport.right ){
		tx+=font->charWidth( t[e]&0xff );++e;
	}

	if( e>b ) font->render( this,color_argb,x,y,t.substr( b,e-b ) );
}

// ---- pixel access ----

bool gxCanvas::lock()const{
	gxCanvas *c=const_cast<gxCanvas*>(this);
	if( !locked_cnt++ ){
		if( render_target ){
			//the back buffer has no CPU copy : give it one for the duration of the lock
			c->gpu_dirty=true;
			c->readBack();
		}else{
			c->cpuPixels();
		}
	}
	return true;
}

void gxCanvas::unlock()const{
	if( locked_cnt && !--locked_cnt ){
		gxCanvas *c=const_cast<gxCanvas*>(this);
		if( render_target ){
			//write the (possibly modified) copy back to the GPU, then drop it
			c->cpu_dirty=true;
			c->uploadTexture();
			delete[] c->pixels;c->pixels=0;
		}else{
			c->cpuModified();
		}
	}
}

void gxCanvas::setPixel( int x,int y,unsigned argb ){
	x+=origin_x;if( x<viewport.left || x>=viewport.right ) return;
	y+=origin_y;if( y<viewport.top || y>=viewport.bottom ) return;
	if( render_target && !locked_cnt ){
		graphics->draw2DRect( this,gxRect( x,y,1,1 ),argb );
		gpuModified();
		return;
	}
	lock();
	setPixelFast( x,y,argb );
	unlock();
}

void gxCanvas::copyPixel( int x,int y,gxCanvas *src,int src_x,int src_y ){
	if( src_x<0 || src_x>=src->width || src_y<0 || src_y>=src->height ) return;
	setPixel( x,y,src->getPixel( src_x,src_y ) );
}

void gxCanvas::copyPixelFast( int x,int y,gxCanvas *src,int src_x,int src_y ){
	setPixelFast( x,y,src->getPixelFast( src_x,src_y ) );
}

unsigned gxCanvas::getPixel( int x,int y )const{
	x+=origin_x;if( x<viewport.left || x>=viewport.right ) return 0;
	y+=origin_y;if( y<viewport.top || y>=viewport.bottom ) return 0;
	lock();
	unsigned n=getPixelFast( x,y );
	unlock();
	if( !(flags & CANVAS_TEX_ALPHA) ) n|=0xff000000;
	return n;
}

// ---- collisions ----

void gxCanvas::updateBitMask()const{
	gxCanvas *c=const_cast<gxCanvas*>(this);
	const unsigned *p=c->cpuPixels();
	cm_pitch=(width+31)/32+1;
	cm_mask=d_new unsigned[cm_pitch*height];
	memset( cm_mask,0,cm_pitch*height*4 );
	for( int y=0;y<height;++y ){
		unsigned *row=cm_mask+y*cm_pitch;
		for( int x=0;x<width;++x ){
			if( (p[y*width+x]&0xffffff)!=mask_argb ) row[x>>5]|=0x80000000u>>(x&31);
		}
	}
}

bool gxCanvas::collide( int x1,int y1,const gxCanvas *i2,int x2,int y2,bool solid )const{

	x1-=handle_x;x2-=i2->handle_x;
	if( x1+width<=x2 || x1>=x2+i2->width ) return false;
	y1-=handle_y;y2-=i2->handle_y;
	if( y1+height<=y2 || y1>=y2+i2->height ) return false;

	if( solid ) return true;

	if( !cm_mask ) updateBitMask();
	if( !i2->cm_mask ) i2->updateBitMask();

	//overlap rectangle, tested pixel by pixel through the bit masks
	int l=x1>x2 ? x1 : x2,r=x1+width<x2+i2->width ? x1+width : x2+i2->width;
	int t=y1>y2 ? y1 : y2,b=y1+height<y2+i2->height ? y1+height : y2+i2->height;
	for( int y=t;y<b;++y ){
		const unsigned *r1=cm_mask+(y-y1)*cm_pitch;
		const unsigned *r2=i2->cm_mask+(y-y2)*i2->cm_pitch;
		for( int x=l;x<r;++x ){
			int a=x-x1,c=x-x2;
			if( (r1[a>>5]&(0x80000000u>>(a&31))) && (r2[c>>5]&(0x80000000u>>(c&31))) ) return true;
		}
	}
	return false;
}

bool gxCanvas::rect_collide( int x1,int y1,int x2,int y2,int w2,int h2,bool solid )const{

	x1-=handle_x;
	if( x1+width<=x2 || x1>=x2+w2 ) return false;
	y1-=handle_y;
	if( y1+height<=y2 || y1>=y2+h2 ) return false;

	if( solid ) return true;

	if( !cm_mask ) updateBitMask();

	int l=x1>x2 ? x1 : x2,r=x1+width<x2+w2 ? x1+width : x2+w2;
	int t=y1>y2 ? y1 : y2,b=y1+height<y2+h2 ? y1+height : y2+h2;
	for( int y=t;y<b;++y ){
		const unsigned *r1=cm_mask+(y-y1)*cm_pitch;
		for( int x=l;x<r;++x ){
			int a=x-x1;
			if( r1[a>>5]&(0x80000000u>>(a&31)) ) return true;
		}
	}
	return false;
}
