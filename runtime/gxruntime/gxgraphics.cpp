#include "std.h"
#include "gxgraphics.h"
#include "gxruntime.h"
#include "gxfilesystem.h"
#include "imageload.h"
#include "wiiu_debug.h"
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

static int state_epoch=1;

// Our own context state for offscreen rendering. GX2 context states shadow register writes (render targets
// included), so rendering with WHBGfx's TV state active would change the target WHBGfx presents to.
static GX2ContextState *offscreen_state;

int gxGraphics::stateEpoch(){
	return state_epoch;
}

gxGraphics::gxGraphics( gxRuntime *rt,int w,int h ):
runtime(rt),front_canvas(0),back_canvas(0),def_font(0),dummy_mesh(0),cur_target(0),cur_target_depth(false),
white_canvas(0),batch(0),batch_cnt(0),batch_max(4096),batch_mode(0),batch_tex(0),batch_target(0),batch_mask(0){

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
	while( font_set.size() ) freeFont( *font_set.begin() );
	while( canvas_set.size() ) freeCanvas( *canvas_set.begin() );
	while( mesh_set.size() ) freeMesh( *mesh_set.begin() );
	while( scene_set.size() ) freeScene( *scene_set.begin() );
	while( movie_set.size() ) closeMovie( *movie_set.begin() );
	//def_font was in font_set (loadFont) and is already freed
	delete white_canvas;
	delete back_canvas;
	delete[] batch;
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
	wiiu::useShaderProgram( &prog2d );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );

	float screen[4]={ 2.0f/batch_target->getWidth(),-2.0f/batch_target->getHeight(),0,0 };
	if( u2d_screen>=0 ) GX2SetVertexUniformReg( u2d_screen,4,screen );
	float mode[4]={ (float)batch_mode,0,0,0 };
	if( u2d_mode>=0 ) GX2SetPixelUniformReg( u2d_mode,4,mode );
	float mask[4]={ ((batch_mask>>16)&255)/255.0f,((batch_mask>>8)&255)/255.0f,(batch_mask&255)/255.0f,0 };
	if( u2d_mask>=0 ) GX2SetPixelUniformReg( u2d_mask,4,mask );

	GX2Texture *t=batch_tex ? &batch_tex->tex : whiteTexture();
	if( t && s2d_tex>=0 ){
		GX2SetPixelTexture( t,s2d_tex );
		GX2SetPixelSampler( &point_sampler,s2d_tex );
	}

	GX2SetDepthOnlyControl( FALSE,FALSE,GX2_COMPARE_FUNC_ALWAYS );
	if( batch_mode==MODE_GLYPH ){
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_SRC_ALPHA,GX2_BLEND_MODE_INV_SRC_ALPHA,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
	}else{
		GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	}
	GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,FALSE );
	GX2SetAlphaTest( FALSE,GX2_COMPARE_FUNC_ALWAYS,0 );

	GX2SetAttribBuffer( 0,size,sizeof(Vertex2D),verts );
	GX2DrawEx( GX2_PRIMITIVE_MODE_QUADS,n,0,1 );
	++state_epoch;		//the scene must re-apply its shader and state
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

void gxGraphics::present( gxCanvas *src,GX2ColorBuffer *cb ){
	int tw=cb->surface.width,th=cb->surface.height;
	GX2SetColorBuffer( cb,GX2_RENDER_TARGET_0 );
	static int logged=0;
	if( logged<2 ){
		++logged;
		WHBLogPrintf( "present: target %dx%d src %dx%d tex image %p",tw,th,src->getWidth(),src->getHeight(),src->tex.surface.image );
	}
	GX2SetViewport( 0,0,tw,th,0.0f,1.0f );
	GX2SetScissor( 0,0,tw,th );

	//letterbox, keeping the back buffer's aspect ratio
	float sa=(float)src->getWidth()/src->getHeight(),ta=(float)tw/th;
	float w=tw,h=th;
	if( sa>ta ) h=tw/sa;
	else w=th*sa;
	float x0=(tw-w)*.5f,y0=(th-h)*.5f;

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
		GX2SetPixelTexture( &src->tex,s2d_tex );
		GX2SetPixelSampler( &linear_sampler,s2d_tex );
	}
	GX2SetDepthOnlyControl( FALSE,FALSE,GX2_COMPARE_FUNC_ALWAYS );
	GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,FALSE );
	GX2SetAttribBuffer( 0,4*sizeof(Vertex2D),sizeof(Vertex2D),v );
	GX2DrawEx( GX2_PRIMITIVE_MODE_TRIANGLE_STRIP,4,0,1 );
}

// Debug overlay (fps, memory, draws), toggled with the right stick button. Drawn on the back buffer just
// before presenting, without changing the game's canvas state.
bool gx_overlay_enabled=true;
static char overlay_text[2][160];

void gxGraphics::drawOverlay(){
	if( !gx_overlay_enabled || !def_font ) return;
	int ox,oy,vx,vy,vw,vh;
	back_canvas->getOrigin( &ox,&oy );
	back_canvas->getViewport( &vx,&vy,&vw,&vh );
	back_canvas->setOrigin( 0,0 );
	back_canvas->setViewport( 0,0,back_canvas->getWidth(),back_canvas->getHeight() );
	for( int k=0;k<2;++k ){
		if( !overlay_text[k][0] ) continue;
		int w=def_font->getWidth( overlay_text[k] );
		draw2DRect( back_canvas,gxRect( 0,k*14,w+8,14 ),0 );
		def_font->render( back_canvas,0xffffff00,4,k*14+1,overlay_text[k] );
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
		drawOverlay();
		flush2D();
	}

	//make the back buffer's rendering visible to the texture units
	GX2Texture *bt=&back_canvas->tex;
	GX2Invalidate( (GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE),bt->surface.image,bt->surface.imageSize );

	//waits until the previous frame has been shown (at most one frame queued)
	OSTime t_wait=OSGetSystemTime();
	WHBGfxBeginRender();
	wiiu::stats.flip_wait_us+=(unsigned)OSTicksToMicroseconds( OSGetSystemTime()-t_wait );

	BB_PROF_SCOPE( "[Flip: present, swap, stats]" );
	WHBGfxBeginRenderTV();
	WHBGfxClearColor( 0,0,0,1 );
	present( back_canvas,WHBGfxGetTVColourBuffer() );
	WHBGfxFinishRenderTV();

	WHBGfxBeginRenderDRC();
	WHBGfxClearColor( 0,0,0,1 );
	present( back_canvas,WHBGfxGetDRCColourBuffer() );
	WHBGfxFinishRenderDRC();

	//WHBGfxFinishRender() also waits for the GPU to finish (GX2DrawDone), which keeps the CPU and the GPU from
	//working at the same time; the frame fences (gpuEndFrame) already protect memory the GPU may still read
	GX2SwapScanBuffers();
	GX2Flush();
	GX2SetTVEnable( TRUE );
	GX2SetDRCEnable( TRUE );
	//GPU time sample (log line "GPU after submit") : is the GPU what keeps the frame rate at 30 ?
	static unsigned gpu_sample_n;
	if( ++gpu_sample_n>=30 ){
		gpu_sample_n=0;
		OSTime t=OSGetSystemTime();
		GX2DrawDone();
		wiiu::stats.gpu_wait_us+=(unsigned)OSTicksToMicroseconds( OSGetSystemTime()-t );
		++wiiu::stats.gpu_samples;
	}
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
		snprintf( overlay_text[0],sizeof(overlay_text[0]),
			"%.0f fps  frame %.1f ms (logic %.1f upd %.1f rend %.1f) wait %.1f  draws %.0f  tris %.0fk  mem %u MB (peak %u)",
			s.frames/secs,s.frame_us/n,(s.frame_us-s.update_us-s.render_us)/n,s.update_us/n,s.render_us/n,
			s.flip_wait_us/n,(float)s.draws/s.frames,s.tris/n,(unsigned)mi.uordblks>>20,peak>>20 );
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
//wait per texture while a map loads)
static void freeStaging( void *p,size_t size ){
	static vector<void*> pending;
	static size_t pending_bytes;
	pending.push_back( p );
	pending_bytes+=size;
	if( pending_bytes<16*1024*1024 ) return;
	wiiu::gpuSync();
	for( size_t k=0;k<pending.size();++k ) wiiu::gpuFree( pending[k] );
	pending.clear();
	pending_bytes=0;
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
