#include "std.h"
#include "gxgraphics.h"
#include "gxruntime.h"
#include "gxfilesystem.h"
#include "imageload.h"
#include "wiiu_debug.h"
#include "wiiu_app.h"
#include "wiiu_prof.h"

#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/state.h>
#include <gx2/swap.h>
#include <coreinit/time.h>
#include <whb/gfx.h>
#include <whb/log.h>

#include "stb_truetype.h"

#include <malloc.h>
#include <algorithm>

static int state_epoch=1;
static void flushStaging();

// Our own context state for offscreen rendering. GX2 context states shadow register writes (render targets
// included), so rendering with WHBGfx's TV state active would change the target WHBGfx presents to.
static GX2ContextState *offscreen_state;

int gxGraphics::stateEpoch(){
	return state_epoch;
}

//back in the foreground : WHBGfx rebuilt its MEM1 heap, so the back buffer is placed again (its contents are lost)
static gxGraphics *resume_graphics;
//performance overlay font (drawOverlay), freed with the graphics' fonts
static gxFont *overlay_font;

void gxGraphics::onResume(){
	gxGraphics *g=resume_graphics;
	if( !g ) return;
	gxCanvas::mem1HeapRebuilt();
	g->back_canvas->placeInMem1();
	g->clearRect( g->back_canvas,gxRect( 0,0,g->back_canvas->getWidth(),g->back_canvas->getHeight() ),0xff000000,true,1.0f,true );
}

gxGraphics::gxGraphics( gxRuntime *rt,int w,int h ):
runtime(rt),front_canvas(0),back_canvas(0),def_font(0),dummy_mesh(0),cur_target(0),cur_target_depth(false),
white_canvas(0),batch(0),batch_cnt(0),batch_max(4096),batch_mode(0),batch_tex(0),batch_target(0),batch_mask(0),
hud_active(false),hud_group(0){

	memset( &hud_scene,0,sizeof(hud_scene) );

	for( int k=0;k<256;++k ) gamma_r[k]=gamma_g[k]=gamma_b[k]=k;

	if( !offscreen_state ){
		offscreen_state=(GX2ContextState*)wiiu::gpuAlloc( sizeof(GX2ContextState),GX2_CONTEXT_STATE_ALIGNMENT );
		GX2SetupContextStateEx( offscreen_state,TRUE );
	}
	restoreState();

	if( !wiiu::loadShaderProgram( &prog2d,"fs:/vol/content/shaders/canvas2d.gsh" ) ){
		WHBLogPrintf( "gxGraphics: canvas2d shader missing" );
	}
	wiiu::addAttribute( &prog2d,"a_pos",0,0,GX2_ATTRIB_FORMAT_FLOAT_32_32 );
	wiiu::addAttribute( &prog2d,"a_uv",0,8,GX2_ATTRIB_FORMAT_FLOAT_32_32 );
	wiiu::addAttribute( &prog2d,"a_rgba",0,16,GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 );
	wiiu::finishFetchShader( &prog2d );
	u2d_screen=prog2d.vsUniform( "u_screen" );
	u2d_mode=prog2d.psUniform( "u_mode" );
	u2d_mask=prog2d.psUniform( "u_mask" );
	s2d_tex=prog2d.psSampler( "u_tex" );
	WHBLogPrintf( "gxGraphics: 2D shader vs=%p ps=%p u_screen=%d u_mode=%d u_mask=%d u_tex=%d attribs=%u",
		prog2d.vs,prog2d.ps,u2d_screen,u2d_mode,u2d_mask,s2d_tex,prog2d.group.numAttributes );

	GX2InitSampler( &point_sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_POINT );
	GX2InitSampler( &linear_sampler,GX2_TEX_CLAMP_MODE_CLAMP,GX2_TEX_XY_FILTER_MODE_LINEAR );

	batch=new Vertex2D[batch_max];

	back_canvas=d_new gxCanvas( this,w,h,0 );
	back_canvas->makeBackBuffer();
	//the 3D scene and the 2D drawing go to the back buffer : in MEM1, the GPU's fast memory (in MEM2, the
	//console's main memory, every pixel written, blended and depth-tested costs much more)
	WHBLogPrintf( "gxGraphics: back buffer in %s",back_canvas->placeInMem1() ? "MEM1" : "MEM2" );
	resume_graphics=this;
	wiiu::appSetResumeCallback( onResume );
	front_canvas=back_canvas;
	//DirectDraw surfaces start out black; GPU memory doesn't
	clearRect( back_canvas,gxRect( 0,0,w,h ),0xff000000,true,1.0f,true );

	def_font=loadFont( "courier",12,0 );
	back_canvas->setFont( def_font );

	unsigned white=0xffffffff;
	white_canvas=d_new gxCanvas( this,1,1,CANVAS_TEXTURE_FLAGS );
	white_canvas->setPixels( &white,1,1 );

	WHBLogPrintf( "gxGraphics: %dx%d",w,h );
}

gxGraphics::~gxGraphics(){
	flush2D();
	wiiu::gpuSync();
	if( resume_graphics==this ) resume_graphics=0;
	overlay_font=0;
	while( font_set.size() ) freeFont( *font_set.begin() );
	while( canvas_set.size() ) freeCanvas( *canvas_set.begin() );
	while( mesh_set.size() ) freeMesh( *mesh_set.begin() );
	while( scene_set.size() ) freeScene( *scene_set.begin() );
	while( movie_set.size() ) closeMovie( *movie_set.begin() );
	//def_font was in font_set (loadFont) and is already freed
	delete white_canvas;
	delete back_canvas;
	delete[] batch;
	if( hud_scene.surface.image ) wiiu::gpuFreeDeferred( hud_scene.surface.image );
}

void gxGraphics::restoreState(){
	GX2SetContextState( offscreen_state );
	cur_target=0;
	++state_epoch;
}

void gxGraphics::syncTexture( gxCanvas *c ){
	if( batch_cnt && batch_target==c ) flush2D();
	if( !c->gpu_drawn ) return;
	c->gpu_drawn=false;
	GX2Invalidate( (GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE),
		c->tex.surface.image,c->tex.surface.imageSize );
}

GX2Texture *gxGraphics::whiteTexture(){
	return white_canvas ? white_canvas->getTexture() : 0;
}

void gxGraphics::bindTarget( gxCanvas *target,bool with_depth ){
	//3D drawn on the back buffer after hudBegin : the GamePad gets the plain back buffer this frame
	if( hud_active && with_depth && target==back_canvas ) hudStop();
	if( target==cur_target && with_depth==cur_target_depth ) return;
	GX2ColorBuffer *cb=target->getColorBuffer();
	if( !cb ) return;
	GX2SetColorBuffer( cb,GX2_RENDER_TARGET_0 );
	if( with_depth ){
		GX2DepthBuffer *db=target->getDepthBuffer();
		if( db ) GX2SetDepthBuffer( db );
	}
	int w=target->getWidth(),h=target->getHeight();
	GX2SetViewport( 0,0,w,h,0.0f,1.0f );
	GX2SetScissor( 0,0,w,h );
	cur_target=target;
	cur_target_depth=with_depth;
}

// ---- 2D batcher ----

void gxGraphics::begin2D( gxCanvas *target,int mode,gxCanvas *tex,unsigned mask ){
	if( tex ) tex->getTexture();		//uploads now, before any quads that use it are queued
	if( batch_cnt && (target!=batch_target || mode!=batch_mode || tex!=batch_tex || mask!=batch_mask) ) flush2D();
	batch_target=target;
	batch_mode=mode;
	batch_tex=tex;
	batch_mask=mask;
}

void gxGraphics::quad2D( float x0,float y0,float x1,float y1,float u0,float v0,float u1,float v1,unsigned argb ){
	if( batch_cnt+4>batch_max ) flush2D();
	unsigned rgba=(argb<<8)|0xff;
	Vertex2D *v=batch+batch_cnt;
	v[0].x=x0;v[0].y=y0;v[0].u=u0;v[0].v=v0;v[0].rgba=rgba;
	v[1].x=x1;v[1].y=y0;v[1].u=u1;v[1].v=v0;v[1].rgba=rgba;
	v[2].x=x1;v[2].y=y1;v[2].u=u1;v[2].v=v1;v[2].rgba=rgba;
	v[3].x=x0;v[3].y=y1;v[3].u=u0;v[3].v=v1;v[3].rgba=rgba;
	batch_cnt+=4;
	if( hud_active && batch_target==back_canvas ) hudRecord( v );
}

void gxGraphics::flush2D(){
	if( !batch_cnt ) return;
	++wiiu::stats.flushes2d;
	int n=batch_cnt;
	batch_cnt=0;

	size_t size=n*sizeof(Vertex2D);
	void *verts=wiiu::gpuTransient( size,GX2_VERTEX_BUFFER_ALIGNMENT );
	memcpy( verts,batch,size );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,verts,size );

	bindTarget( batch_target,false );
	batch_target->gpu_drawn=true;
	setup2D( batch_mode,batch_tex,batch_mask,batch_target->getWidth(),batch_target->getHeight(),&point_sampler );
	GX2SetAttribBuffer( 0,size,sizeof(Vertex2D),verts );
	GX2DrawEx( GX2_PRIMITIVE_MODE_QUADS,n,0,1 );
	++state_epoch;		//the scene must re-apply its shader and state
}

void gxGraphics::setup2D( int mode,gxCanvas *tex,unsigned mask,int tw,int th,GX2Sampler *sampler ){
	wiiu::useShaderProgram( &prog2d );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );

	float screen[4]={ 2.0f/tw,-2.0f/th,0,0 };
	if( u2d_screen>=0 ) GX2SetVertexUniformReg( u2d_screen,4,screen );
	float m[4]={ (float)mode,0,0,0 };
	if( u2d_mode>=0 ) GX2SetPixelUniformReg( u2d_mode,4,m );
	float mk[4]={ ((mask>>16)&255)/255.0f,((mask>>8)&255)/255.0f,(mask&255)/255.0f,0 };
	if( u2d_mask>=0 ) GX2SetPixelUniformReg( u2d_mask,4,mk );

	GX2Texture *t=tex ? &tex->tex : whiteTexture();
	if( t && s2d_tex>=0 ){
		GX2SetPixelTexture( t,s2d_tex );
		GX2SetPixelSampler( sampler,s2d_tex );
	}

	GX2SetDepthOnlyControl( FALSE,FALSE,GX2_COMPARE_FUNC_ALWAYS );
	if( mode==MODE_GLYPH ){
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_SRC_ALPHA,GX2_BLEND_MODE_INV_SRC_ALPHA,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
	}else{
		GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	}
	GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,FALSE );
	GX2SetAlphaTest( FALSE,GX2_COMPARE_FUNC_ALWAYS,0 );
}

void gxGraphics::draw2DRect( gxCanvas *target,const gxRect &r,unsigned argb ){
	begin2D( target,MODE_SOLID,0,0 );
	quad2D( r.left,r.top,r.right,r.bottom,0,0,0,0,argb );
}

void gxGraphics::draw2DImage( gxCanvas *target,const gxRect &dest,gxCanvas *src,const gxRect &s,bool masked,unsigned tint ){
	if( src==target ){
		//overlapping self copy (e.g. CopyRect within the back buffer) : not supported by the GPU path
		return;
	}
	begin2D( target,masked ? MODE_MASKED : MODE_IMAGE,src,masked ? src->maskARGB() : 0 );
	float iw=1.0f/src->getWidth(),ih=1.0f/src->getHeight();
	quad2D( dest.left,dest.top,dest.right,dest.bottom,s.left*iw,s.top*ih,s.right*iw,s.bottom*ih,tint );
}

void gxGraphics::draw2DGlyphs( gxCanvas *target,const gxRect &dest,gxCanvas *atlas,const gxRect &s,unsigned argb ){
	begin2D( target,MODE_GLYPH,atlas,0 );
	float iw=1.0f/atlas->getWidth(),ih=1.0f/atlas->getHeight();
	quad2D( dest.left,dest.top,dest.right,dest.bottom,s.left*iw,s.top*ih,s.right*iw,s.bottom*ih,argb );
}

void gxGraphics::clearRect( gxCanvas *target,const gxRect &r,unsigned argb,bool clear_color,float z,bool clear_depth ){
	flush2D();
	if( !clear_color && !clear_depth ) return;
	if( hud_active && target==back_canvas ) hudStop();		//(Cls, a camera viewport) : as for 3D, see bindTarget
	if( clear_color ) target->gpu_drawn=true;
	int w=target->getWidth(),h=target->getHeight();
	bool full=r.left<=0 && r.top<=0 && r.right>=w && r.bottom>=h;
	if( full ){
		//fast clears (they reset the context state)
		if( clear_color ){
			GX2ColorBuffer *cb=target->getColorBuffer();
			GX2ClearColor( cb,((argb>>16)&255)/255.0f,((argb>>8)&255)/255.0f,(argb&255)/255.0f,(argb>>24)/255.0f );
		}
		if( clear_depth ){
			GX2DepthBuffer *db=target->getDepthBuffer();
			db->depthClear=z;
			GX2ClearDepthStencilEx( db,z,0,GX2_CLEAR_FLAGS_DEPTH );
		}
		restoreState();
		return;
	}
	//partial : a quad with depth ALWAYS
	gxRect c=r;
	if( c.left<0 ) c.left=0;
	if( c.top<0 ) c.top=0;
	if( c.right>w ) c.right=w;
	if( c.bottom>h ) c.bottom=h;
	if( c.right<=c.left || c.bottom<=c.top ) return;

	Vertex2D *v=(Vertex2D*)wiiu::gpuTransient( 4*sizeof(Vertex2D),GX2_VERTEX_BUFFER_ALIGNMENT );
	unsigned rgba=(argb<<8)|(argb>>24);
	v[0].x=c.left;v[0].y=c.top;v[1].x=c.right;v[1].y=c.top;
	v[2].x=c.right;v[2].y=c.bottom;v[3].x=c.left;v[3].y=c.bottom;
	for( int k=0;k<4;++k ){ v[k].u=v[k].v=0;v[k].rgba=rgba; }
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,v,4*sizeof(Vertex2D) );

	bindTarget( target,clear_depth );
	wiiu::useShaderProgram( &prog2d );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );
	float screen[4]={ 2.0f/w,-2.0f/h,z,0 };
	if( u2d_screen>=0 ) GX2SetVertexUniformReg( u2d_screen,4,screen );
	float mode[4]={ (float)MODE_SOLID,0,0,0 };
	if( u2d_mode>=0 ) GX2SetPixelUniformReg( u2d_mode,4,mode );
	GX2SetDepthOnlyControl( clear_depth,clear_depth,GX2_COMPARE_FUNC_ALWAYS );
	GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	GX2SetTargetChannelMasks( clear_color ? GX2_CHANNEL_MASK_RGBA : (GX2ChannelMask)0,
		(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0 );
	GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,FALSE );
	GX2SetAttribBuffer( 0,4*sizeof(Vertex2D),sizeof(Vertex2D),v );
	GX2DrawEx( GX2_PRIMITIVE_MODE_QUADS,4,0,1 );
	GX2SetTargetChannelMasks( GX2_CHANNEL_MASK_RGBA,
		(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0,(GX2ChannelMask)0 );
	++state_epoch;
}

// ---- presenting ----

//true if the back buffer covers the whole target (aspect ratios within 0.5 %) : no letterbox, no clear needed
static bool presentFills( gxCanvas *src,GX2ColorBuffer *cb ){
	float sa=(float)src->getWidth()/src->getHeight(),ta=(float)cb->surface.width/cb->surface.height;
	return fabsf( sa-ta )<=ta*.005f;
}

static void (*present_hook)( int screen );

void gxGraphics::setPresentHook( void (*hook)( int screen ) ){
	present_hook=hook;
}

static int (*memory_hook)( char *buf,int size );

void gxGraphics::setMemoryHook( int (*hook)( char *buf,int size ) ){
	memory_hook=hook;
}

void gxGraphics::logMemoryReport(){
	BB_PROF_SCOPE( "[memory report]" );
	//canvases : GPU textures (uncompressed RGBA / compressed), render targets, CPU pixel copies (those of canvases
	//that also have a texture counted apart : the duplicates)
	size_t tex_rgba=0,tex_comp=0,rt=0,pix=0,pix_dup=0,masks=0;
	int n_rgba=0,n_comp=0,n_pix=0,n_dup=0;
	for( std::set<gxCanvas*>::const_iterator it=canvas_set.begin();it!=canvas_set.end();++it ){
		const gxCanvas *c=*it;
		if( c->tex_valid && !c->in_mem1 ){
			size_t b=c->tex.surface.imageSize+(c->tex.surface.mipmaps ? c->tex.surface.mipmapSize : 0);
			if( c->render_target ) rt+=b;
			else if( c->tex.surface.format!=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 ){ tex_comp+=b;++n_comp; }
			else{ tex_rgba+=b;++n_rgba; }
		}
		if( c->depth_valid && !c->in_mem1 ) rt+=c->depth_buf.surface.imageSize;
		if( c->pixels ){
			size_t b=(size_t)c->width*c->height*4;
			if( c->tex_valid ){ pix_dup+=b;++n_dup; }else{ pix+=b;++n_pix; }
		}
		if( c->cm_mask ) masks+=(size_t)c->cm_pitch*c->height*4;
	}
	size_t mesh=0;int n_spares=0;
	for( std::set<gxMesh*>::const_iterator it=mesh_set.begin();it!=mesh_set.end();++it ){
		mesh+=(*it)->gpuBytes();
		n_spares+=(*it)->spareCount();
	}
	int n_sounds=0;
	size_t sounds=runtime->audio ? runtime->audio->soundBytes( &n_sounds ) : 0;
	struct mallinfo mi=mallinfo();
	char extra[256];extra[0]=0;
	if( memory_hook ) memory_hook( extra,sizeof(extra) );
	//(two lines : OSReport cuts long lines)
	WHBLogPrintf( "memory : %u MB in use (heap %u MB, %u MB of it free) | textures %u KB RGBA (%d) + %u KB compressed (%d), render targets %u KB | "
		"CPU pixels %u KB (%d, + %u KB in %d also on the GPU), collision masks %u KB",
		(unsigned)mi.uordblks>>20,(unsigned)mi.arena>>20,(unsigned)mi.fordblks>>20,(unsigned)(tex_rgba>>10),n_rgba,(unsigned)(tex_comp>>10),n_comp,(unsigned)(rt>>10),
		(unsigned)(pix>>10),n_pix,(unsigned)(pix_dup>>10),n_dup,(unsigned)(masks>>10) );
	WHBLogPrintf( "memory : meshes (GPU) %u KB in %d (%d spares) | sounds %u KB (%d) | %s",
		(unsigned)(mesh>>10),(int)mesh_set.size(),n_spares,(unsigned)(sounds>>10),n_sounds,extra );
	//the biggest CPU pixel copies (images)
	vector<std::pair<size_t,const gxCanvas*> > big;
	for( std::set<gxCanvas*>::const_iterator it=canvas_set.begin();it!=canvas_set.end();++it ){
		if( (*it)->pixels ) big.push_back( std::make_pair( (size_t)(*it)->width*(*it)->height*4,*it ) );
	}
	std::sort( big.begin(),big.end() );
	for( int k=0;k<12 && k<(int)big.size();++k ){
		const gxCanvas *c=big[big.size()-1-k].second;
		WHBLogPrintf( "memory :   %u KB %dx%d%s %s",(unsigned)(big[big.size()-1-k].first>>10),c->width,c->height,
			c->tex_valid ? " (+ texture)" : "",c->name.empty() ? "(created)" : c->name.c_str() );
	}
}

//where the back buffer (sw x sh) is shown in a tw x th target : letterboxed, keeping its aspect ratio (nearly the
//same aspect ratio : the whole target)
static void presentRect( int sw,int sh,int tw,int th,float *x0,float *y0,float *w,float *h ){
	float sa=(float)sw/sh,ta=(float)tw/th;
	*w=tw;*h=th;
	if( fabsf( sa-ta )>ta*.005f ){
		if( sa>ta ) *h=tw/sa;
		else *w=th*sa;
	}
	*x0=(tw-*w)*.5f;*y0=(th-*h)*.5f;
}

void gxGraphics::present( GX2Texture *src,int sw,int sh,GX2ColorBuffer *cb ){
	int tw=cb->surface.width,th=cb->surface.height;
	GX2SetColorBuffer( cb,GX2_RENDER_TARGET_0 );
	static int logged=0;
	if( logged<2 ){
		++logged;
		WHBLogPrintf( "present: target %dx%d src %dx%d tex image %p",tw,th,sw,sh,src->surface.image );
	}
	GX2SetViewport( 0,0,tw,th,0.0f,1.0f );
	GX2SetScissor( 0,0,tw,th );

	float x0,y0,w,h;
	presentRect( sw,sh,tw,th,&x0,&y0,&w,&h );

	Vertex2D *v=(Vertex2D*)wiiu::gpuTransient( 4*sizeof(Vertex2D),GX2_VERTEX_BUFFER_ALIGNMENT );
	float x1=x0+w,y1=y0+h;
	//triangle strip : top-left, top-right, bottom-left, bottom-right
	v[0].x=x0;v[0].y=y0;v[0].u=0;v[0].v=0;
	v[1].x=x1;v[1].y=y0;v[1].u=1;v[1].v=0;
	v[2].x=x0;v[2].y=y1;v[2].u=0;v[2].v=1;
	v[3].x=x1;v[3].y=y1;v[3].u=1;v[3].v=1;
	for( int k=0;k<4;++k ) v[k].rgba=0xffffffff;
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,v,4*sizeof(Vertex2D) );

	wiiu::useShaderProgram( &prog2d );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );
	float screen[4]={ 2.0f/tw,-2.0f/th,0,0 };
	if( u2d_screen>=0 ) GX2SetVertexUniformReg( u2d_screen,4,screen );
	float mode[4]={ (float)MODE_IMAGE,0,0,0 };
	if( u2d_mode>=0 ) GX2SetPixelUniformReg( u2d_mode,4,mode );
	if( s2d_tex>=0 ){
		GX2SetPixelTexture( src,s2d_tex );
		GX2SetPixelSampler( &linear_sampler,s2d_tex );
	}
	GX2SetDepthOnlyControl( FALSE,FALSE,GX2_COMPARE_FUNC_ALWAYS );
	GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,FALSE );
	GX2SetAttribBuffer( 0,4*sizeof(Vertex2D),sizeof(Vertex2D),v );
	GX2DrawEx( GX2_PRIMITIVE_MODE_TRIANGLE_STRIP,4,0,1 );
}

// ---- brightness ----
// The game's "screen gamma" multiplies colours by g (0..2). The PC version did it with two extra full-screen
// renders of the back buffer each frame; here one blended rectangle over each presented picture : g < 1 :
// picture * g; g > 1 : picture + picture * (g-1).
static float screen_gamma=1;

void gxGraphics::setScreenGamma( float g ){
	screen_gamma=g<0 ? 0 : g>2 ? 2 : g;
}

void gxGraphics::applyGamma( int tw,int th ){
	float g=screen_gamma;
	if( fabsf( g-1 )<.01f ) return;
	float f=g<1 ? g : g-1;
	unsigned c=(unsigned)(f*255+.5f);
	unsigned rgba=(c<<24)|(c<<16)|(c<<8)|0xff;
	Vertex2D *v=(Vertex2D*)wiiu::gpuTransient( 4*sizeof(Vertex2D),GX2_VERTEX_BUFFER_ALIGNMENT );
	v[0].x=0;v[0].y=0;v[1].x=tw;v[1].y=0;v[2].x=tw;v[2].y=th;v[3].x=0;v[3].y=th;
	for( int k=0;k<4;++k ){ v[k].u=v[k].v=0;v[k].rgba=rgba; }
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,v,4*sizeof(Vertex2D) );
	setup2D( MODE_SOLID,0,0,tw,th,&point_sampler );
	if( g<1 ){
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_ZERO,GX2_BLEND_MODE_SRC_COLOR,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
	}else{
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_DST_COLOR,GX2_BLEND_MODE_ONE,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
	}
	GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
	GX2SetAttribBuffer( 0,4*sizeof(Vertex2D),sizeof(Vertex2D),v );
	GX2DrawEx( GX2_PRIMITIVE_MODE_QUADS,4,0,1 );
	GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
}

// ---- GamePad HUD ----
// The game draws its HUD (meters, inventory, documents, messages) for the TV; on the GamePad's smaller screen the
// same picture is hard to read. So from hudBegin (Main.bb, after the 3D view is drawn) to the flip, every 2D quad
// drawn on the back buffer is also recorded, and the GamePad gets the back buffer as it was at hudBegin plus the
// recorded quads again, scaled up by hud_scale about their anchor (the game sets anchors with hudAnchor : the
// meters at the bottom left, the radio at the bottom right...). A group of parts with one anchor is scaled less
// if it would leave the screen (only sides that were on screen count). 3D drawing or a clear on the back buffer
// after hudBegin stops the recording : the GamePad then shows the back buffer like the TV.
static float hud_scale=1.5f;
//the last GamePad frame's centre scaling (for touches), see hudTouch
static bool hud_touch_valid;
static float hud_touch_s=1,hud_touch_ax,hud_touch_ay;

void gxGraphics::setHudScale( float s ){
	hud_scale=s<1 ? 1 : s>3 ? 3 : s;
}

void gxGraphics::hudTouch( float *x,float *y ){
	if( !hud_touch_valid ) return;
	*x=hud_touch_ax+(*x-hud_touch_ax)/hud_touch_s;
	*y=hud_touch_ay+(*y-hud_touch_ay)/hud_touch_s;
}

void gxGraphics::hudBegin(){
	if( hud_active || hud_scale<=1.001f ) return;
	flush2D();
	//the 3D view so far : drawn into a copy for the GamePad (same surface layout as the back buffer). Drawn, not
	//GX2CopySurface'd : in Cemu that copy left speckles where the scene had blended pixels
	GX2Surface &bs=back_canvas->tex.surface;
	if( !hud_scene.surface.image || hud_scene.surface.width!=bs.width || hud_scene.surface.height!=bs.height ){
		if( hud_scene.surface.image ) wiiu::gpuFreeDeferred( hud_scene.surface.image );
		hud_scene=back_canvas->tex;
		hud_scene.surface.image=wiiu::gpuAlloc( bs.imageSize,bs.alignment );
		hud_scene.surface.mipmaps=0;
		if( !hud_scene.surface.image ){
			memset( &hud_scene,0,sizeof(hud_scene) );
			return;
		}
		GX2InitTextureRegs( &hud_scene );
		memset( &hud_scene_cb,0,sizeof(hud_scene_cb) );
		hud_scene_cb.surface=hud_scene.surface;
		hud_scene_cb.viewNumSlices=1;
		GX2InitColorBufferRegs( &hud_scene_cb );
	}
	GX2Invalidate( (GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE),bs.image,bs.imageSize );
	present( &back_canvas->tex,bs.width,bs.height,&hud_scene_cb );
	restoreState();
	hud_active=true;
	hud_groups.clear();
	hud_group=hudFindGroup( HUD_ANCHOR,.5f,.5f );
}

int gxGraphics::hudFindGroup( int kind,float fx,float fy ){
	if( kind!=HUD_SELF ){
		for( size_t k=0;k<hud_groups.size();++k ){
			const HudGroup &g=hud_groups[k];
			if( g.kind==kind && (kind!=HUD_ANCHOR || (g.fx==fx && g.fy==fy)) ) return (int)k;
		}
	}
	if( hud_groups.size()>=65535 ) return (int)hud_groups.size()-1;
	HudGroup g={ kind,fx,fy,1e9f,1e9f,-1e9f,-1e9f };
	hud_groups.push_back( g );
	return (int)hud_groups.size()-1;
}

void gxGraphics::hudAnchor( float fx,float fy ){
	if( hud_active ) hud_group=hudFindGroup( fx<0 ? HUD_SELF : HUD_ANCHOR,fx,fy );
}

void gxGraphics::hudPointer(){
	if( hud_active ) hud_group=hudFindGroup( HUD_POINTER,0,0 );
}

void gxGraphics::hudPlain(){
	if( hud_active ) hud_group=hudFindGroup( HUD_PLAIN,0,0 );
}

void gxGraphics::hudRecord( const Vertex2D *v ){
	if( hud_runs.empty() || hud_runs.back().mode!=batch_mode || hud_runs.back().tex!=batch_tex || hud_runs.back().mask!=batch_mask ){
		HudRun r={ batch_mode,batch_tex,batch_mask,(int)hud_verts.size(),0 };
		hud_runs.push_back( r );
	}
	hud_runs.back().count+=4;
	hud_verts.insert( hud_verts.end(),v,v+4 );
	hud_quad_group.push_back( (unsigned short)hud_group );
	HudGroup &g=hud_groups[hud_group];
	if( g.kind==HUD_ANCHOR || g.kind==HUD_SELF ){
		if( v[0].x<g.x0 ) g.x0=v[0].x;
		if( v[2].x>g.x1 ) g.x1=v[2].x;
		if( v[0].y<g.y0 ) g.y0=v[0].y;
		if( v[2].y>g.y1 ) g.y1=v[2].y;
	}
}

void gxGraphics::hudStop(){
	hud_active=false;
	hud_verts.clear();
	hud_quad_group.clear();
	hud_runs.clear();
	hud_groups.clear();
}

//the largest scale <= s keeping [lo,hi] (scaled about a) inside [0,size], for the sides that start inside
static float fitScale( float s,float a,float lo,float hi,float size ){
	if( lo<a && lo>=0 ) s=std::min( s,a/(a-lo) );
	if( hi>a && hi<=size ) s=std::min( s,(size-a)/(hi-a) );
	return s;
}

void gxGraphics::hudReplay( GX2ColorBuffer *cb ){
	int tw=cb->surface.width,th=cb->surface.height;
	float W=back_canvas->getWidth(),H=back_canvas->getHeight();
	float px,py,pw,ph;
	presentRect( (int)W,(int)H,tw,th,&px,&py,&pw,&ph );
	float kx=pw/W,ky=ph/H;

	//each group's anchor and scale
	int n_groups=(int)hud_groups.size();
	vector<float> gs( n_groups ),gax( n_groups ),gay( n_groups );
	vector<bool> used( n_groups );
	for( size_t q=0;q<hud_quad_group.size();++q ) used[hud_quad_group[q]]=true;
	int centre=-1;
	bool plain_seen=false;
	for( int k=0;k<n_groups;++k ){
		const HudGroup &g=hud_groups[k];
		float s=1,ax=W*.5f,ay=H*.5f;
		if( g.kind==HUD_ANCHOR || g.kind==HUD_SELF ){
			if( g.kind==HUD_ANCHOR ){ ax=g.fx*W;ay=g.fy*H; }
			else{ ax=(g.x0+g.x1)*.5f;ay=(g.y0+g.y1)*.5f; }
			s=hud_scale;
			if( g.x1>g.x0 ){
				s=fitScale( s,ax,g.x0,g.x1,W );
				s=fitScale( s,ay,g.y0,g.y1,H );
			}
			if( s<1 ) s=1;
			if( g.kind==HUD_ANCHOR && g.fx==.5f && g.fy==.5f ) centre=k;
		}else if( g.kind==HUD_PLAIN && used[k] ){
			//(HUD_OVERLAY : the debug overlay, not scaled either, but it doesn't take touches)
			plain_seen=true;
		}
		gs[k]=s;gax[k]=ax;gay[k]=ay;
	}
	float centre_s=centre>=0 ? gs[centre] : hud_scale;
	for( int k=0;k<n_groups;++k ){
		if( hud_groups[k].kind==HUD_POINTER ) gs[k]=centre_s;
	}
	//touches map back through the centre scaling, unless menus (not scaled) were drawn
	hud_touch_valid=!plain_seen;
	hud_touch_s=centre_s;hud_touch_ax=W*.5f;hud_touch_ay=H*.5f;

	//the quads, scaled, in target pixels
	size_t n=hud_verts.size();
	if( !n ) return;
	size_t size=n*sizeof(Vertex2D);
	Vertex2D *v=(Vertex2D*)wiiu::gpuTransient( size,GX2_VERTEX_BUFFER_ALIGNMENT );
	if( !v ) return;
	for( size_t q=0;q<n/4;++q ){
		int k=hud_quad_group[q];
		float s=gs[k],ax=gax[k],ay=gay[k];
		for( int j=0;j<4;++j ){
			const Vertex2D &a=hud_verts[q*4+j];
			Vertex2D &b=v[q*4+j];
			b=a;
			b.x=px+(ax+(a.x-ax)*s)*kx;
			b.y=py+(ay+(a.y-ay)*s)*ky;
		}
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,v,size );
	GX2SetViewport( 0,0,tw,th,0.0f,1.0f );
	GX2SetScissor( 0,0,tw,th );
	for( size_t r=0;r<hud_runs.size();++r ){
		const HudRun &run=hud_runs[r];
		if( !run.count ) continue;
		//smooth scaling, except masked images (the mask colour must match exactly)
		setup2D( run.mode,run.tex,run.mask,tw,th,run.mode==MODE_MASKED ? &point_sampler : &linear_sampler );
		GX2SetAttribBuffer( 0,size,sizeof(Vertex2D),v );
		GX2DrawEx( GX2_PRIMITIVE_MODE_QUADS,run.count,run.first,1 );
	}
}

// Debug overlay (fps, memory, draws), toggled with the right stick button. Drawn on the back buffer just
// before presenting, without changing the game's canvas state.
bool gx_overlay_enabled=false;		//off for players; the right stick click shows it
//three short lines in a bigger font : readable on the GamePad (the back buffer is shown at 854x480 there)
enum{ OVERLAY_LINES=3,OVERLAY_FONT=22 };
static char overlay_text[OVERLAY_LINES][96];

void gxGraphics::drawOverlay(){
	if( !gx_overlay_enabled ) return;
	if( !overlay_font ) overlay_font=loadFont( "courier",OVERLAY_FONT,0 );
	gxFont *font=overlay_font ? overlay_font : def_font;
	if( !font ) return;
	int line_h=font==overlay_font ? OVERLAY_FONT+2 : 14;
	int ox,oy,vx,vy,vw,vh;
	back_canvas->getOrigin( &ox,&oy );
	back_canvas->getViewport( &vx,&vy,&vw,&vh );
	back_canvas->setOrigin( 0,0 );
	back_canvas->setViewport( 0,0,back_canvas->getWidth(),back_canvas->getHeight() );
	for( int k=0;k<OVERLAY_LINES;++k ){
		if( !overlay_text[k][0] ) continue;
		int w=font->getWidth( overlay_text[k] );
		draw2DRect( back_canvas,gxRect( 0,k*line_h,w+8,line_h ),0 );
		font->render( back_canvas,0xffffff00,4,k*line_h+1,overlay_text[k] );
	}
	back_canvas->setOrigin( ox,oy );
	back_canvas->setViewport( vx,vy,vw,vh );
}

void gxGraphics::flip( bool vwait ){
	//time the game spent on this frame (from the end of the previous flip, so without the vsync wait)
	static OSTime prev_flip_end;
	OSTime flip_start=OSGetSystemTime();
	if( prev_flip_end ) wiiu::stats.frame_us+=(unsigned)OSTicksToMicroseconds( flip_start-prev_flip_end );

	{
		BB_PROF_SCOPE( "[Flip: overlay + 2D]" );
		//(the overlay : not scaled on the GamePad)
		if( hud_active ) hud_group=hudFindGroup( HUD_OVERLAY,0,0 );
		drawOverlay();
		flush2D();
	}
	wiiu::gpuMark( "2D and the rest" );

	//make the back buffer's rendering visible to the texture units
	GX2Texture *bt=&back_canvas->tex;
	GX2Invalidate( (GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE),bt->surface.image,bt->surface.imageSize );

	//waits until the previous frame has been shown (at most one frame queued)
	OSTime t_wait=OSGetSystemTime();
	WHBGfxBeginRender();
	wiiu::stats.flip_wait_us+=(unsigned)OSTicksToMicroseconds( OSGetSystemTime()-t_wait );

	BB_PROF_SCOPE( "[Flip: present, swap, stats]" );
	WHBGfxBeginRenderTV();
	GX2ColorBuffer *tv=WHBGfxGetTVColourBuffer();
	if( !presentFills( back_canvas,tv ) ) WHBGfxClearColor( 0,0,0,1 );
	present( bt,back_canvas->getWidth(),back_canvas->getHeight(),tv );
	applyGamma( tv->surface.width,tv->surface.height );
	if( present_hook ) present_hook( 0 );
	WHBGfxFinishRenderTV();
	wiiu::gpuMark( "TV copy" );

	WHBGfxBeginRenderDRC();
	GX2ColorBuffer *drc=WHBGfxGetDRCColourBuffer();
	if( !presentFills( back_canvas,drc ) ) WHBGfxClearColor( 0,0,0,1 );
	if( hud_active ){
		//the 3D view from hudBegin, then the HUD bigger
		GX2Invalidate( (GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE),hud_scene.surface.image,hud_scene.surface.imageSize );
		present( &hud_scene,back_canvas->getWidth(),back_canvas->getHeight(),drc );
		hudReplay( drc );
		hudStop();
	}else{
		present( bt,back_canvas->getWidth(),back_canvas->getHeight(),drc );
		hud_touch_valid=false;
	}
	applyGamma( drc->surface.width,drc->surface.height );
	if( present_hook ) present_hook( 1 );
	WHBGfxFinishRenderDRC();
	wiiu::gpuMark( "GamePad copy" );

	//WHBGfxFinishRender() also waits for the GPU to finish (GX2DrawDone), which keeps the CPU and the GPU from
	//working at the same time; the frame fences (gpuEndFrame) already protect memory the GPU may still read
	GX2SwapScanBuffers();
	wiiu::gpuMark( "swap" );
	GX2Flush();
	GX2SetTVEnable( TRUE );
	GX2SetDRCEnable( TRUE );
	//GPU time sample every 30th frame (log lines "GPU after submit", "GPU parts") : the next frame gets GPU
	//timestamps at its marks, and after it is submitted Flip waits for the GPU
	static unsigned gpu_sample_n;
	if( gpu_sample_n==30 ){
		gpu_sample_n=0;
		OSTime t=OSGetSystemTime();
		GX2DrawDone();
		wiiu::stats.gpu_wait_us+=(unsigned)OSTicksToMicroseconds( OSGetSystemTime()-t );
		++wiiu::stats.gpu_samples;
		wiiu::gpuMarkCollect();
	}
	if( ++gpu_sample_n==30 ) wiiu::gpuMarkFrame();
	flushStaging();		//texture staging left from loading : freed once the GPU is done with this frame
	wiiu::gpuEndFrame();
	wiiu::debugFramePresented();
	static bool first_logged;
	if( !first_logged ){ first_logged=true;WHBLogPrintf( "gxGraphics: first game frame presented" ); }

	//statistics : overlay text every second, log line every 5 s (memory = the malloc heap, GPU memory included)
	static unsigned peak;
	static int last_overlay,last_log;
	static wiiu::GpuStats log_stats;
	//mallinfo() walks the heap : only once a second
	static struct mallinfo mi;
	static int last_mallinfo;
	int now_ms=runtime->getMilliSecs();
	if( !last_mallinfo || now_ms-last_mallinfo>=1000 ){
		BB_PROF_SCOPE( "[Flip: mallinfo]" );
		mi=mallinfo();
		last_mallinfo=now_ms;
	}
	if( (unsigned)mi.uordblks>peak ) peak=mi.uordblks;
	wiiu::GpuStats &s=wiiu::stats;
	++s.frames;
	int now=runtime->getMilliSecs();
	if( !last_overlay ){ last_overlay=last_log=now; }
	if( now-last_overlay>=1000 ){
		float secs=(now-last_overlay)/1000.0f;
		float n=(float)s.frames*1000.0f;
		snprintf( overlay_text[0],sizeof(overlay_text[0]),"%.0f fps  CPU %.1f ms  GPU %.1f ms",
			s.frames/secs,s.frame_us/n,s.gpu_samples ? s.gpu_wait_us/1000.0f/s.gpu_samples : 0.0f );
		snprintf( overlay_text[1],sizeof(overlay_text[1]),"logic %.1f  upd %.1f  rend %.1f  wait %.1f",
			(s.frame_us-s.update_us-s.render_us)/n,s.update_us/n,s.render_us/n,s.flip_wait_us/n );
		snprintf( overlay_text[2],sizeof(overlay_text[2]),"draws %.0f  tris %.0fk  mem %u MB (peak %u)",
			(float)s.draws/s.frames,s.tris/n,(unsigned)mi.uordblks>>20,peak>>20 );
		log_stats.frames+=s.frames;log_stats.draws+=s.draws;log_stats.flushes2d+=s.flushes2d;
		log_stats.uploads+=s.uploads;log_stats.upload_bytes+=s.upload_bytes;
		log_stats.readbacks+=s.readbacks;log_stats.syncs+=s.syncs;
		log_stats.tris+=s.tris;log_stats.update_us+=s.update_us;log_stats.render_us+=s.render_us;
		log_stats.frame_us+=s.frame_us;log_stats.flip_wait_us+=s.flip_wait_us;
		log_stats.gpu_wait_us+=s.gpu_wait_us;log_stats.gpu_samples+=s.gpu_samples;
		memset( &s,0,sizeof(s) );
		last_overlay=now;
	}
	if( now-last_log>=5000 ){
		wiiu::GpuStats &l=log_stats;
		float secs=(now-last_log)/1000.0f;
		if( l.frames ){
			float n=(float)l.frames*1000.0f;
			WHBLogPrintf( "perf: %.1f fps | ms per frame: %.1f (logic %.1f, UpdateWorld %.1f, RenderWorld %.1f), flip wait %.1f, GPU after submit %.1f | per frame: %.1f draws, %.0f tris, %.1f 2D flushes, %.2f uploads (%.0f KB), %.2f readbacks, %.2f syncs | memory %u MB (peak %u)",
				l.frames/secs,l.frame_us/n,(l.frame_us-l.update_us-l.render_us)/n,l.update_us/n,l.render_us/n,
				l.flip_wait_us/n,l.gpu_samples ? l.gpu_wait_us/1000.0f/l.gpu_samples : 0.0f,(float)l.draws/l.frames,(float)l.tris/l.frames,(float)l.flushes2d/l.frames,
				(float)l.uploads/l.frames,l.upload_bytes/1024.0f/l.frames,(float)l.readbacks/l.frames,
				(float)l.syncs/l.frames,(unsigned)mi.uordblks>>20,peak>>20 );
		}
		char parts[384];
		if( wiiu::gpuMarkReport( parts,sizeof(parts) ) ) WHBLogPrintf( "%s",parts );
		gxFileSystem::logReadSummary();
		//memory breakdown : every 15 s while the total moves by 2 MB or more
		static int last_mem_log;
		static unsigned last_mem_total;
		unsigned total=(unsigned)mi.uordblks;
		if( !last_mem_log || (now-last_mem_log>=15000 && (total>last_mem_total+(2<<20) || total+(2<<20)<last_mem_total)) ){
			logMemoryReport();
			last_mem_log=now;
			last_mem_total=total;
		}
		memset( &l,0,sizeof(l) );
		last_log=now;
	}

	restoreState();
	prev_flip_end=OSGetSystemTime();
}

void gxGraphics::vwait(){
	GX2WaitForVsync();
}

void gxGraphics::copy( gxCanvas *dest,int dx,int dy,int dw,int dh,gxCanvas *src,int sx,int sy,int sw,int sh ){
	gxRect d( dx,dy,dw,dh ),s( sx,sy,sw,sh );
	if( dest->isRenderTarget() || src->isRenderTarget() || dw!=sw || dh!=sh ){
		draw2DImage( dest,d,src,s,false,0xffffffff );
		dest->gpuModified();
		return;
	}
	int ox,oy,hx,hy;
	dest->getOrigin( &ox,&oy );
	src->getHandle( &hx,&hy );
	dest->blit( dx-ox+hx,dy-oy+hy,src,sx,sy,sw,sh,true );
}

// ---- gamma (stored; not applied yet) ----

void gxGraphics::setGamma( int r,int g,int b,float dr,float dg,float db ){
	gamma_r[r&255]=dr;gamma_g[g&255]=dg;gamma_b[b&255]=db;
}

void gxGraphics::getGamma( int r,int g,int b,float *dr,float *dg,float *db ){
	*dr=gamma_r[r&255];*dg=gamma_g[g&255];*db=gamma_b[b&255];
}

void gxGraphics::updateGamma( bool calibrate ){
}

// ---- accessors ----

int gxGraphics::getWidth()const{ return front_canvas->getWidth(); }
int gxGraphics::getHeight()const{ return front_canvas->getHeight(); }
int gxGraphics::getDepth()const{ return 32; }
int gxGraphics::getScanLine()const{ return 0; }
int gxGraphics::getAvailVidmem()const{ return 256*1024*1024; }
int gxGraphics::getTotalVidmem()const{ return 512*1024*1024; }
gxCanvas *gxGraphics::getFrontCanvas()const{ return front_canvas; }
gxCanvas *gxGraphics::getBackCanvas()const{ return back_canvas; }
gxFont *gxGraphics::getDefaultFont()const{ return def_font; }

// ---- canvases ----

gxCanvas *gxGraphics::createCanvas( int w,int h,int flags ){
	if( flags & gxCanvas::CANVAS_TEXTURE ) imageload::adjustTexSize( &w,&h );
	if( w<=0 || h<=0 ) return 0;
	gxCanvas *c=d_new gxCanvas( this,w,h,flags );
	canvas_set.insert( c );
	c->cls();
	return c;
}

// Precompressed texture written by tools/texconv (<image>.wbc = BC1, <image>.wbc3 = BC3), see that file
static unsigned be32( const unsigned char *p ){
	return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
}

//Big staging surfaces are kept until 16 MB have piled up, then freed after one wait for the GPU (instead of a
//wait per texture while a map loads); at the next frame the rest goes to the deferred free list (flushStaging)
static vector<void*> staging_pending;
static size_t staging_pending_bytes;

static void freeStaging( void *p,size_t size ){
	staging_pending.push_back( p );
	staging_pending_bytes+=size;
	if( staging_pending_bytes<16*1024*1024 ) return;
	wiiu::gpuSync();
	for( size_t k=0;k<staging_pending.size();++k ) wiiu::gpuFree( staging_pending[k] );
	staging_pending.clear();
	staging_pending_bytes=0;
}

static void flushStaging(){
	for( size_t k=0;k<staging_pending.size();++k ) wiiu::gpuFreeDeferred( staging_pending[k] );
	staging_pending.clear();
	staging_pending_bytes=0;
}

gxCanvas *gxGraphics::loadCompressed( const string &path,int flags ){
	FileData data;
	{
		BB_PROF_SCOPE( "[loadCanvas: precompressed, file read]" );
		if( !gxFileSystem::readWholeFile( path,&data ) ) return 0;
	}
	if( data.size()<=24 || memcmp( data.data(),"WBC1",4 ) ) return 0;

	unsigned fmt=be32( &data[4] ),w=be32( &data[8] ),h=be32( &data[12] ),mips=be32( &data[16] );
	int block_bytes=fmt==3 ? 16 : 8;

	GX2Surface s;
	memset( &s,0,sizeof(s) );
	s.dim=GX2_SURFACE_DIM_TEXTURE_2D;
	s.width=w;s.height=h;s.depth=1;
	s.mipLevels=(flags & gxCanvas::CANVAS_TEX_MIPMAP) ? mips : 1;
	s.format=fmt==3 ? GX2_SURFACE_FORMAT_UNORM_BC3 : GX2_SURFACE_FORMAT_UNORM_BC1;
	s.aa=GX2_AA_MODE1X;
	s.use=GX2_SURFACE_USE_TEXTURE;
	s.tileMode=GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment( &s );
	s.image=wiiu::gpuAlloc( s.imageSize,s.alignment );
	s.mipmaps=s.mipLevels>1 ? wiiu::gpuAlloc( s.mipmapSize,s.alignment ) : 0;
	if( !s.image || (s.mipLevels>1 && !s.mipmaps) ){
		wiiu::gpuFree( s.image );wiiu::gpuFree( s.mipmaps );
		return 0;
	}

	flush2D();
	size_t pos=24;
	unsigned lw=w,lh=h;
	for( unsigned level=0;level<s.mipLevels;++level ){
		if( pos+4>data.size() ) break;
		unsigned bytes=be32( &data[pos] );pos+=4;
		if( pos+bytes>data.size() ) break;
		//linear staging surface for this level, then a GPU copy into the tiled texture
		GX2Surface st;
		memset( &st,0,sizeof(st) );
		st.dim=GX2_SURFACE_DIM_TEXTURE_2D;
		st.width=lw;st.height=lh;st.depth=1;st.mipLevels=1;
		st.format=s.format;
		st.aa=GX2_AA_MODE1X;
		st.use=GX2_SURFACE_USE_TEXTURE;
		st.tileMode=GX2_TILE_MODE_LINEAR_ALIGNED;
		GX2CalcSurfaceSizeAndAlignment( &st );
		bool big=st.imageSize>1024*1024;
		st.image=big ? wiiu::gpuAlloc( st.imageSize,st.alignment ) : wiiu::gpuTransient( st.imageSize,st.alignment );
		if( !st.image ) break;
		unsigned bw=(lw+3)/4,bh=(lh+3)/4;
		//row pitch : GX2 gives compressed pitches in blocks (checked against the image size)
		unsigned row=st.pitch*block_bytes;
		if( row*bh>st.imageSize ) row=(st.pitch/4)*block_bytes;
		memset( st.image,0,st.imageSize );
		for( unsigned by=0;by<bh;++by ){
			memcpy( (unsigned char*)st.image+by*row,&data[pos+by*bw*block_bytes],bw*block_bytes );
		}
		GX2Invalidate( GX2_INVALIDATE_MODE_CPU_TEXTURE,st.image,st.imageSize );
		GX2CopySurface( &st,0,0,&s,level,0 );
		if( big ) freeStaging( st.image,st.imageSize );
		pos+=bytes;
		lw=lw>1 ? lw/2 : 1;lh=lh>1 ? lh/2 : 1;
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE,s.image,s.imageSize );
	if( s.mipmaps ) GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE,s.mipmaps,s.mipmapSize );
	restoreState();

	gxCanvas *c=d_new gxCanvas( this,w,h,flags );
	c->setCompressed( s );
	c->name=path;
	canvas_set.insert( c );
	return c;
}

gxCanvas *gxGraphics::loadCanvas( const string &f,int flags ){
	int w,h;
	vector<unsigned> argb;
	string path=gxFileSystem::resolvePath( f );
	//rooms with empty texture names load "GFX\map\" : a folder, not an image (fails on PC too)
	if( path.empty() || path[path.size()-1]=='/' ) return 0;

	//textures : precompressed version if Blitz's flags allow it (no mask; alpha only from the file's alpha)
	if( (flags & gxCanvas::CANVAS_TEXTURE) && !(flags & gxCanvas::CANVAS_TEX_MASK) ){
		BB_PROF_SCOPE( "[loadCanvas: precompressed]" );
		gxCanvas *c=loadCompressed( path+((flags & gxCanvas::CANVAS_TEX_ALPHA) ? ".wbc3" : ".wbc"),flags );
		if( c ) return c;
	}
	{
		BB_PROF_SCOPE( "[loadCanvas: decode]" );
		if( !imageload::load( path,flags,&argb,&w,&h ) ) return 0;
	}
	gxCanvas *c=d_new gxCanvas( this,w,h,flags );
	c->name=path;
	c->setPixels( argb.data(),w,h );
	//loaded textures : upload now and drop the CPU copy (halves their memory); a later LockBuffer reads back
	if( flags & gxCanvas::CANVAS_TEXTURE ){
		BB_PROF_SCOPE( "[loadCanvas: upload + mips]" );
		c->releaseCpuCopy();
	}
	canvas_set.insert( c );
	return c;
}

gxCanvas *gxGraphics::verifyCanvas( gxCanvas *c ){
	return canvas_set.count( c ) || c==front_canvas || c==back_canvas ? c : 0;
}

void gxGraphics::freeCanvas( gxCanvas *c ){
	if( !canvas_set.erase( c ) ) return;
	if( batch_cnt && (batch_tex==c || batch_target==c) ) flush2D();
	if( cur_target==c ) cur_target=0;
	//recorded for the GamePad HUD : left out
	for( size_t k=0;k<hud_runs.size();++k ){
		if( hud_runs[k].tex==c ) hud_runs[k].count=0;
	}
	delete c;
}

// ---- movies (not supported) ----

gxMovie *gxGraphics::openMovie( const string &file,int flags ){ return 0; }
gxMovie *gxGraphics::verifyMovie( gxMovie *m ){ return movie_set.count( m ) ? m : 0; }
void gxGraphics::closeMovie( gxMovie *m ){ if( movie_set.erase( m ) ) delete m; }

// ---- fonts ----

// Windows-1252 code points 0x80..0x9f (Blitz strings are bytes in the system code page)
static const unsigned short cp1252[32]={
	0x20ac,0x0081,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,0x0152,0x008d,0x017d,0x008f,
	0x0090,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x02dc,0x2122,0x0161,0x203a,0x0153,0x009d,0x017e,0x0178
};

static string fontFile( const string &f ){
	string t=tolower( f );
	if( t.find( '.' )!=string::npos ) return gxFileSystem::resolvePath( f );
	if( t.find( "bold" )!=string::npos ) return gxFileSystem::resolvePath( "GFX\\font\\courbd\\Courier New.ttf" );
	//"courier", "Courier New", "Blitz" and anything else : the game's Courier New
	return gxFileSystem::resolvePath( "GFX\\font\\cour\\Courier New.ttf" );
}

gxFont *gxGraphics::loadFont( const string &f,int height,int flags ){
	string path=fontFile( f );
	FILE *fp=fopen( path.c_str(),"rb" );
	if( !fp ){
		WHBLogPrintf( "loadFont: can't open %s",path.c_str() );
		return 0;
	}
	fseek( fp,0,SEEK_END );
	long size=ftell( fp );
	fseek( fp,0,SEEK_SET );
	vector<unsigned char> ttf( size );
	if( fread( ttf.data(),1,size,fp )!=(size_t)size ){ fclose( fp );return 0; }
	fclose( fp );

	stbtt_fontinfo info;
	if( !stbtt_InitFont( &info,ttf.data(),stbtt_GetFontOffsetForIndex( ttf.data(),0 ) ) ) return 0;
	if( height<1 ) height=1;
	float scale=stbtt_ScaleForPixelHeight( &info,height );
	int ascent,descent,gap;
	stbtt_GetFontVMetrics( &info,&ascent,&descent,&gap );
	int baseline=(int)floorf( ascent*scale+.5f );
	bool bold=flags & gxFont::FONT_BOLD;
	bool underline=flags & gxFont::FONT_UNDERLINE;

	const int begin_char=32,end_char=256;
	int n=end_char-begin_char;
	int *offs=d_new int[n],*widths=d_new int[n];

	//glyph widths
	int total=0;
	for( int k=0;k<n;++k ){
		int c=k+begin_char;
		int cp=(c>=0x80 && c<0xa0) ? cp1252[c-0x80] : c;
		int adv,lsb;
		stbtt_GetCodepointHMetrics( &info,cp,&adv,&lsb );
		widths[k]=(int)floorf( adv*scale+.5f )+(bold ? 1 : 0);
		if( widths[k]<1 ) widths[k]=1;
		total+=widths[k];
	}

	//atlas : rows of glyph cells, at most 1024 wide
	int atlas_w=total<1024 ? total : 1024,x=0,y=0;
	for( int k=0;k<n;++k ){
		if( x+widths[k]>atlas_w ){ x=0;y+=height; }
		offs[k]=(x<<16)|y;
		x+=widths[k];
	}
	int atlas_h=y+height;

	vector<unsigned> pix( atlas_w*atlas_h,0 );
	for( int k=0;k<n;++k ){
		int c=k+begin_char;
		int cp=(c>=0x80 && c<0xa0) ? cp1252[c-0x80] : c;
		int gx=(offs[k]>>16)&0xffff,gy=offs[k]&0xffff;
		int x0,y0,x1,y1;
		stbtt_GetCodepointBitmapBox( &info,cp,scale,scale,&x0,&y0,&x1,&y1 );
		int bw=x1-x0,bh=y1-y0;
		if( bw>0 && bh>0 ){
			vector<unsigned char> bmp( bw*bh );
			stbtt_MakeCodepointBitmap( &info,bmp.data(),bw,bh,bw,scale,scale,cp );
			int adv,lsb;
			stbtt_GetCodepointHMetrics( &info,cp,&adv,&lsb );
			int ox=(int)floorf( lsb*scale+.5f );
			for( int j=0;j<bh;++j ){
				int py=gy+baseline+y0+j;
				if( py<gy || py>=gy+height ) continue;
				for( int i=0;i<bw;++i ){
					unsigned cov=bmp[j*bw+i];
					if( !cov ) continue;
					for( int b=0;b<=(bold ? 1 : 0);++b ){
						int px=gx+ox+i+b;
						if( px<gx || px>=gx+widths[k] ) continue;
						//alpha = coverage (stb_truetype doesn't hint, so text is blended on the GPU)
						unsigned &p=pix[py*atlas_w+px];
						if( (p>>24)<cov ) p=(cov<<24)|0xffffff;
					}
				}
			}
		}
		if( underline && baseline+1<height ){
			for( int i=0;i<widths[k];++i ) pix[(gy+baseline+1)*atlas_w+gx+i]=0xffffffff;
		}
	}

	gxCanvas *atlas=d_new gxCanvas( this,atlas_w,atlas_h,CANVAS_TEXTURE_FLAGS|gxCanvas::CANVAS_TEX_ALPHA );
	atlas->setPixels( pix.data(),atlas_w,atlas_h );
	gxFont *font=d_new gxFont( this,atlas,height,begin_char,end_char,'?',offs,widths );
	font_set.insert( font );
	return font;
}

gxFont *gxGraphics::verifyFont( gxFont *f ){
	return font_set.count( f ) || f==def_font ? f : 0;
}

void gxGraphics::freeFont( gxFont *f ){
	if( !font_set.erase( f ) ) return;
	if( batch_cnt && batch_tex==f->atlas() ) flush2D();
	delete f;
}

// ---- scenes and meshes ----

gxScene *gxGraphics::createScene( int flags ){
	if( scene_set.size() ) return 0;
	gxScene *scene=d_new gxScene( this,back_canvas );
	scene_set.insert( scene );
	return scene;
}

gxScene *gxGraphics::verifyScene( gxScene *s ){
	return scene_set.count( s ) ? s : 0;
}

void gxGraphics::freeScene( gxScene *scene ){
	if( scene_set.erase( scene ) ) delete scene;
}

gxMesh *gxGraphics::createMesh( int max_verts,int max_tris,int flags ){
	gxMesh *mesh=d_new gxMesh( this,max_verts,max_tris );
	mesh_set.insert( mesh );
	return mesh;
}

gxMesh *gxGraphics::verifyMesh( gxMesh *m ){
	return mesh_set.count( m ) ? m : 0;
}

void gxGraphics::freeMesh( gxMesh *mesh ){
	if( mesh_set.erase( mesh ) ) delete mesh;
}
