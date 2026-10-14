#include "std.h"
#include "wiiu_prof.h"
#include "gxscene.h"
#include "gxgraphics.h"
#include "gxcanvas.h"
#include "gxmesh.h"

#include <gx2/draw.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/texture.h>
#include <whb/log.h>

// Uniform register offsets and values (see shaders/scene.vert / scene.frag)
struct gxScene::Uniforms{
	int wv,proj,proj_mode,material,ambient,specular,vflags;
	int light_pos,light_dir,light_col,light_spot;
	int fog,stage,texmat;
	int ps_stage,fogcolor,ps_flags;
	int sampler[4];

	float v_wv[12],v_proj[4],v_proj_mode[4];
	float v_material[4],v_ambient[4],v_specular[4],v_vflags[4];
	float v_light_pos[32],v_light_dir[32],v_light_col[32],v_light_spot[32];
	float v_fog[4],v_stage[16],v_texmat[32];
	float v_ps_stage[16],v_fogcolor[4],v_ps_flags[4];

	int epoch;				//graphics state epoch the GPU state was last applied in
	bool lights_dirty;
};

static void mulAffine( const gxScene::Matrix &a,const gxScene::Matrix &b,gxScene::Matrix *c ){
	for( int i=0;i<4;++i ){
		for( int j=0;j<3;++j ){
			float t=a.elements[i][0]*b.elements[0][j]+a.elements[i][1]*b.elements[1][j]+a.elements[i][2]*b.elements[2][j];
			if( i==3 ) t+=b.elements[3][j];
			c->elements[i][j]=t;
		}
	}
}

static const gxScene::Matrix IDENTITY={{ {1,0,0},{0,1,0},{0,0,1},{0,0,0} }};

gxScene::gxScene( gxGraphics *g,gxCanvas *t ):
graphics(g),target(t),wireframe(false),flipped(false),fogmode(FOG_NONE),zmode(ZMODE_NORMAL),
fogrange_nr(1),fogrange_fr(1000),ortho_proj(false),n_texs(0),tris_drawn(0),hw_multitex(true),in_scene(false){

	if( !wiiu::loadShaderProgram( &prog,"fs:/vol/content/shaders/scene.gsh" ) ){
		WHBLogPrintf( "gxScene: scene shader missing" );
	}
	wiiu::addAttribute( &prog,"a_pos",0,0,GX2_ATTRIB_FORMAT_FLOAT_32_32_32 );
	wiiu::addAttribute( &prog,"a_normal",0,12,GX2_ATTRIB_FORMAT_FLOAT_32_32_32 );
	wiiu::addAttribute( &prog,"a_argb",0,24,GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 );
	wiiu::addAttribute( &prog,"a_uv0",0,28,GX2_ATTRIB_FORMAT_FLOAT_32_32 );
	wiiu::addAttribute( &prog,"a_uv1",0,36,GX2_ATTRIB_FORMAT_FLOAT_32_32 );
	wiiu::finishFetchShader( &prog );

	uni=new Uniforms;
	memset( uni,0,sizeof(Uniforms) );
	uni->wv=prog.vsUniform( "u_wv" );
	uni->proj=prog.vsUniform( "u_proj" );
	uni->proj_mode=prog.vsUniform( "u_proj_mode" );
	uni->material=prog.vsUniform( "u_material" );
	uni->ambient=prog.vsUniform( "u_ambient" );
	uni->specular=prog.vsUniform( "u_specular" );
	uni->vflags=prog.vsUniform( "u_vflags" );
	uni->light_pos=prog.vsUniform( "u_light_pos" );
	uni->light_dir=prog.vsUniform( "u_light_dir" );
	uni->light_col=prog.vsUniform( "u_light_col" );
	uni->light_spot=prog.vsUniform( "u_light_spot" );
	uni->fog=prog.vsUniform( "u_fog" );
	uni->stage=prog.vsUniform( "u_stage" );
	uni->texmat=prog.vsUniform( "u_texmat" );
	uni->ps_stage=prog.psUniform( "u_ps_stage" );
	uni->fogcolor=prog.psUniform( "u_fogcolor" );
	uni->ps_flags=prog.psUniform( "u_ps_flags" );
	static const char *snames[4]={ "u_tex0","u_tex1","u_tex2","u_tex3" };
	for( int k=0;k<4;++k ) uni->sampler[k]=prog.psSampler( snames[k] );

	for( int u=0;u<2;++u ){
		for( int v=0;v<2;++v ){
			for( int m=0;m<2;++m ){
				GX2Sampler *s=&samplers[u][v][m];
				GX2InitSampler( s,GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_XY_FILTER_MODE_LINEAR );
				GX2InitSamplerClamping( s,u ? GX2_TEX_CLAMP_MODE_CLAMP : GX2_TEX_CLAMP_MODE_WRAP,
					v ? GX2_TEX_CLAMP_MODE_CLAMP : GX2_TEX_CLAMP_MODE_WRAP,GX2_TEX_CLAMP_MODE_WRAP );
				GX2InitSamplerXYFilter( s,GX2_TEX_XY_FILTER_MODE_LINEAR,GX2_TEX_XY_FILTER_MODE_LINEAR,GX2_TEX_ANISO_RATIO_NONE );
				GX2InitSamplerZMFilter( s,GX2_TEX_Z_FILTER_MODE_LINEAR,m ? GX2_TEX_MIP_FILTER_MODE_LINEAR : GX2_TEX_MIP_FILTER_MODE_NONE );
			}
		}
	}

	ambient[0]=ambient[1]=ambient[2]=.5f;
	ambient2[0]=ambient2[1]=ambient2[2]=0;
	fogcolor[0]=fogcolor[1]=fogcolor[2]=0;
	viewmatrix=IDENTITY;
	worldmatrix=IDENTITY;
	vp_x=vp_y=0;vp_w=target->getWidth();vp_h=target->getHeight();
	frustum_nr=frustum_fr=frustum_w=frustum_h=0;
	setPerspProj( 1,1000,1,1 );

	memset( &rs,0,sizeof(rs) );
	rs.color[0]=rs.color[1]=rs.color[2]=rs.alpha=1;
	rs.blend=BLEND_REPLACE;
	uni->lights_dirty=true;
}

gxScene::~gxScene(){
	while( _allLights.size() ) freeLight( *_allLights.begin() );
	delete uni;
}

// ---- state ----

int gxScene::hwTexUnits(){
	return hw_multitex ? HW_STAGES : 1;
}

int gxScene::gfxDriverCaps3D(){
	return 110;		//cube map capable
}

void gxScene::setWBuffer( bool enable ){}
void gxScene::setDither( bool enable ){}
void gxScene::setAntialias( bool enable ){}

void gxScene::setHWMultiTex( bool enable ){
	hw_multitex=enable;
}

void gxScene::setWireframe( bool enable ){
	wireframe=enable;
}

void gxScene::setFlippedTris( bool enable ){
	flipped=enable;
}

void gxScene::setAmbient( const float rgb[3] ){
	memcpy( ambient,rgb,12 );
}

void gxScene::setAmbient2( const float rgb[3] ){
	memcpy( ambient2,rgb,12 );
}

void gxScene::setFogColor( const float rgb[3] ){
	memcpy( fogcolor,rgb,12 );
}

void gxScene::setFogRange( float nr,float fr ){
	fogrange_nr=nr;fogrange_fr=fr;
}

void gxScene::setFogMode( int mode ){
	fogmode=mode;
}

void gxScene::setZMode( int mode ){
	zmode=mode;
}

void gxScene::setViewport( int x,int y,int w,int h ){
	vp_x=x;vp_y=y;vp_w=w;vp_h=h;
	if( in_scene ){
		GX2SetViewport( vp_x,vp_y,vp_w,vp_h,0.0f,1.0f );
		GX2SetScissor( vp_x,vp_y,vp_w,vp_h );
	}
}

void gxScene::setOrthoProj( float nr,float fr,float w,float h ){
	frustum_nr=nr;frustum_fr=fr;frustum_w=w;frustum_h=h;ortho_proj=true;
	float Q=1/(fr-nr);
	uni->v_proj[0]=2/w;uni->v_proj[1]=2/h;uni->v_proj[2]=Q;uni->v_proj[3]=-Q*nr;
	uni->v_proj_mode[0]=0;
}

void gxScene::setPerspProj( float nr,float fr,float w,float h ){
	frustum_nr=nr;frustum_fr=fr;frustum_w=w;frustum_h=h;ortho_proj=false;
	float Q=fr/(fr-nr);
	uni->v_proj[0]=2*nr/w;uni->v_proj[1]=2*nr/h;uni->v_proj[2]=Q;uni->v_proj[3]=-Q*nr;
	uni->v_proj_mode[0]=1;
}

void gxScene::setViewMatrix( const Matrix *m ){
	viewmatrix=m ? *m : IDENTITY;
	uni->lights_dirty=true;
}

void gxScene::setWorldMatrix( const Matrix *m ){
	worldmatrix=m ? *m : IDENTITY;
}

void gxScene::setRenderState( const RenderState &state ){
	rs=state;
	n_texs=0;
	for( int k=0;k<MAX_TEXTURES;++k ){
		if( rs.tex_states[k].canvas && rs.tex_states[k].blend ) ++n_texs;
	}
}

// ---- rendering ----

bool gxScene::begin( const vector<gxLight*> &lights ){
	_curLights.clear();
	for( size_t n=0;n<lights.size() && n<MAX_LIGHTS;++n ) _curLights.push_back( lights[n] );
	uni->lights_dirty=true;
	uni->epoch=0;
	in_scene=true;
	return true;
}

void gxScene::clear( const float rgb[3],float alpha,float z,bool clear_argb,bool clear_z ){
	if( !clear_argb && !clear_z ) return;
	unsigned argb=(int(alpha*255.0f)<<24)|(int(rgb[0]*255.0f)<<16)|(int(rgb[1]*255.0f)<<8)|int(rgb[2]*255.0f);
	graphics->clearRect( target,gxRect( vp_x,vp_y,vp_w,vp_h ),argb,clear_argb,z,clear_z );
	target->gpuModified();
	uni->epoch=0;
}

void gxScene::end(){
	in_scene=false;
	target->gpuModified();
}

void gxScene::applyRasterState(){
	graphics->bindTarget( target,true );
	GX2SetViewport( vp_x,vp_y,vp_w,vp_h,0.0f,1.0f );
	GX2SetScissor( vp_x,vp_y,vp_w,vp_h );
	GX2SetRasterizerClipControlHalfZ( TRUE,TRUE,TRUE );
	wiiu::useShaderProgram( &prog );
	GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );
	GX2SetAlphaTest( FALSE,GX2_COMPARE_FUNC_ALWAYS,0 );
}

static void setBlend( int blend ){
	switch( blend ){
	case gxScene::BLEND_ALPHA:
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_SRC_ALPHA,GX2_BLEND_MODE_INV_SRC_ALPHA,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
		break;
	case gxScene::BLEND_MULTIPLY:
	case gxScene::BLEND_DOT3:
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_DST_COLOR,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
		break;
	case gxScene::BLEND_ADD:
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_SRC_ALPHA,GX2_BLEND_MODE_ONE,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
		break;
	case 100:	//extra pass : ONE,ONE
		GX2SetBlendControl( GX2_RENDER_TARGET_0,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ONE,GX2_BLEND_COMBINE_MODE_ADD,
			FALSE,GX2_BLEND_MODE_ONE,GX2_BLEND_MODE_ZERO,GX2_BLEND_COMBINE_MODE_ADD );
		GX2SetColorControl( GX2_LOGIC_OP_COPY,1,FALSE,TRUE );
		break;
	default:
		GX2SetColorControl( GX2_LOGIC_OP_COPY,0,FALSE,TRUE );
	}
}

void gxScene::render( gxMesh *m,int first_vert,int vert_cnt,int first_tri,int tri_cnt ){
	BB_PROF_SCOPE( "[gxScene::render, per draw]" );
	if( tri_cnt<=0 ) return;

	//textures first : uploads may run GPU copies that reset the GPU state
	int n_stages=0;
	const RenderState::TexState *stages[MAX_TEXTURES];
	GX2Texture *texs[MAX_TEXTURES];
	for( int k=0;k<MAX_TEXTURES;++k ){
		const RenderState::TexState &ts=rs.tex_states[k];
		if( !ts.canvas || !ts.blend ) continue;
		GX2Texture *t=ts.canvas->getTexture();
		if( !t ) continue;
		stages[n_stages]=&ts;
		texs[n_stages]=t;
		++n_stages;
	}
	graphics->flush2D();

	if( uni->epoch!=gxGraphics::stateEpoch() ){
		applyRasterState();
		uni->epoch=gxGraphics::stateEpoch();
	}

	//transform
	Matrix wv;
	mulAffine( worldmatrix,viewmatrix,&wv );
	for( int j=0;j<3;++j ){
		uni->v_wv[j*4+0]=wv.elements[0][j];
		uni->v_wv[j*4+1]=wv.elements[1][j];
		uni->v_wv[j*4+2]=wv.elements[2][j];
		uni->v_wv[j*4+3]=wv.elements[3][j];
	}
	if( uni->wv>=0 ) GX2SetVertexUniformReg( uni->wv,12,uni->v_wv );
	if( uni->proj>=0 ) GX2SetVertexUniformReg( uni->proj,4,uni->v_proj );
	if( uni->proj_mode>=0 ) GX2SetVertexUniformReg( uni->proj_mode,4,uni->v_proj_mode );

	//lights, in view space
	int fx=rs.fx;
	if( uni->lights_dirty ){
		memset( uni->v_light_pos,0,sizeof(uni->v_light_pos) );
		for( size_t n=0;n<_curLights.size();++n ){
			gxLight *l=_curLights[n];
			const float *p=l->position,*d=l->direction;
			float *vp=uni->v_light_pos+n*4,*vd=uni->v_light_dir+n*4;
			for( int j=0;j<3;++j ){
				vp[j]=p[0]*viewmatrix.elements[0][j]+p[1]*viewmatrix.elements[1][j]+p[2]*viewmatrix.elements[2][j]+viewmatrix.elements[3][j];
				vd[j]=d[0]*viewmatrix.elements[0][j]+d[1]*viewmatrix.elements[1][j]+d[2]*viewmatrix.elements[2][j];
			}
			float len=sqrtf( vd[0]*vd[0]+vd[1]*vd[1]+vd[2]*vd[2] );
			if( len>0 ){ vd[0]/=len;vd[1]/=len;vd[2]/=len; }
			vp[3]=l->type==gxLight::LIGHT_POINT ? 2 : (l->type==gxLight::LIGHT_SPOT ? 3 : 1);
			vd[3]=l->atten1;
			float *c=uni->v_light_col+n*4;
			c[0]=l->diffuse[0];c[1]=l->diffuse[1];c[2]=l->diffuse[2];c[3]=1;
			float *s=uni->v_light_spot+n*4;
			s[0]=cosf( l->theta*.5f );s[1]=cosf( l->phi*.5f );s[2]=1;s[3]=0;
		}
		uni->lights_dirty=false;
	}
	//light enables depend on the brush fx
	float light_pos[32];
	memcpy( light_pos,uni->v_light_pos,sizeof(light_pos) );
	for( int n=0;n<MAX_LIGHTS;++n ){
		if( fx & FX_FULLBRIGHT ) light_pos[n*4+3]=0;
		else if( (fx & FX_CONDLIGHT) && light_pos[n*4+3]==1 ) light_pos[n*4+3]=0;
	}
	if( uni->light_pos>=0 ) GX2SetVertexUniformReg( uni->light_pos,32,light_pos );
	if( uni->light_dir>=0 ) GX2SetVertexUniformReg( uni->light_dir,32,uni->v_light_dir );
	if( uni->light_col>=0 ) GX2SetVertexUniformReg( uni->light_col,32,uni->v_light_col );
	if( uni->light_spot>=0 ) GX2SetVertexUniformReg( uni->light_spot,32,uni->v_light_spot );

	//material
	float material[4]={ rs.color[0],rs.color[1],rs.color[2],rs.alpha };
	const float *amb=(fx & FX_FULLBRIGHT) ? 0 : ((fx & FX_CONDLIGHT) ? ambient2 : ambient);
	float amb4[4]={ 1,1,1,1 };
	if( amb ){ amb4[0]=amb[0];amb4[1]=amb[1];amb4[2]=amb[2]; }
	float t=rs.shininess>0 ? (rs.shininess<1 ? rs.shininess : 1) : 0;
	float specular[4]={ t,rs.shininess*128,rs.shininess>0 ? 1.0f : 0.0f,0 };
	bool emissive=fx & FX_EMISSIVE;
	float vflags[4]={ ((fx & FX_VERTEXCOLOR) && !emissive) ? 1.0f : 0.0f,emissive ? 1.0f : 0.0f,1,0 };
	if( uni->material>=0 ) GX2SetVertexUniformReg( uni->material,4,material );
	if( uni->ambient>=0 ) GX2SetVertexUniformReg( uni->ambient,4,amb4 );
	if( uni->specular>=0 ) GX2SetVertexUniformReg( uni->specular,4,specular );
	if( uni->vflags>=0 ) GX2SetVertexUniformReg( uni->vflags,4,vflags );

	//fog
	bool fog=fogmode==FOG_LINEAR && !(fx & FX_NOFOG);
	float fogv[4]={ fogrange_nr,fogrange_fr,fog ? 1.0f : 0.0f,0 };
	if( uni->fog>=0 ) GX2SetVertexUniformReg( uni->fog,4,fogv );
	float fogc[4]={ fogcolor[0],fogcolor[1],fogcolor[2],1 };
	if( uni->fogcolor>=0 ) GX2SetPixelUniformReg( uni->fogcolor,4,fogc );

	//alpha test : D3D ALPHAFUNC GREATER with ref 128*alpha (0 with vertex alpha)
	float ref=(fx & FX_VERTEXALPHA) ? 0 : (float)(int)(128*rs.alpha)/255.0f;
	float ps_flags[4]={ (fx & FX_ALPHATEST) ? 1.0f : 0.0f,ref,rs.shininess>0 ? 1.0f : 0.0f,0 };
	if( uni->ps_flags>=0 ) GX2SetPixelUniformReg( uni->ps_flags,4,ps_flags );

	//raster state
	switch( zmode ){
	case ZMODE_DISABLE: GX2SetDepthOnlyControl( FALSE,FALSE,GX2_COMPARE_FUNC_LEQUAL );break;
	case ZMODE_CMPONLY: GX2SetDepthOnlyControl( TRUE,FALSE,GX2_COMPARE_FUNC_LEQUAL );break;
	default: GX2SetDepthOnlyControl( TRUE,TRUE,GX2_COMPARE_FUNC_LEQUAL );break;
	}
	//D3D culls counter-clockwise triangles by default (clockwise when flipped)
	if( fx & FX_DOUBLESIDED ) GX2SetCullOnlyControl( GX2_FRONT_FACE_CW,FALSE,FALSE );
	else if( flipped ) GX2SetCullOnlyControl( GX2_FRONT_FACE_CCW,FALSE,TRUE );
	else GX2SetCullOnlyControl( GX2_FRONT_FACE_CW,FALSE,TRUE );

	int hw=hwTexUnits();
	int first_pass=n_stages<hw ? n_stages : hw;

	//pass 1 : up to `hw` stages, with the brush blend
	setBlend( rs.blend );
	float stage[16],ps_stage[16],texmat[32];
	memset( stage,0,sizeof(stage) );
	memset( ps_stage,0,sizeof(ps_stage) );
	memset( texmat,0,sizeof(texmat) );
	for( int k=0;k<HW_STAGES;++k ){
		int sampler=uni->sampler[k];
		if( k>=first_pass ){
			if( sampler>=0 ){
				GX2Texture *w=graphics->whiteTexture();
				if( w ) GX2SetPixelTexture( w,sampler );
				GX2SetPixelSampler( &samplers[0][0][0],sampler );
			}
			continue;
		}
		const RenderState::TexState &ts=*stages[k];
		int flags=ts.canvas->getFlags();
		stage[k*4+0]=ts.blend;
		stage[k*4+1]=(flags & gxCanvas::CANVAS_TEX_ALPHA) ? 1 : 0;
		stage[k*4+2]=(ts.flags & TEX_COORDS2) ? 1 : 0;
		if( flags & gxCanvas::CANVAS_TEX_SPHERE ) stage[k*4+3]=1;
		else if( flags & gxCanvas::CANVAS_TEX_CUBE ){
			int cm=ts.canvas->cubeMode()&3;
			stage[k*4+3]=cm==gxCanvas::CUBEMODE_NORMAL ? 2 : (cm==gxCanvas::CUBEMODE_POSITION ? 3 : 4);
		}
		ps_stage[k*4+0]=stage[k*4+0];
		ps_stage[k*4+1]=stage[k*4+1];
		float *tm=texmat+k*8;
		if( ts.matrix ){
			const float (*e)[3]=ts.matrix->elements;
			tm[0]=e[0][0];tm[1]=e[1][0];tm[2]=e[2][0];
			tm[4]=e[0][1];tm[5]=e[1][1];tm[6]=e[2][1];
		}else{
			tm[0]=1;tm[5]=1;
		}
		if( sampler>=0 ){
			GX2SetPixelTexture( texs[k],sampler );
			bool mip=texs[k]->surface.mipLevels>1;
			GX2SetPixelSampler( &samplers[(flags & gxCanvas::CANVAS_TEX_CLAMPU) ? 1 : 0][(flags & gxCanvas::CANVAS_TEX_CLAMPV) ? 1 : 0][mip ? 1 : 0],sampler );
		}
	}
	if( uni->stage>=0 ) GX2SetVertexUniformReg( uni->stage,16,stage );
	if( uni->texmat>=0 ) GX2SetVertexUniformReg( uni->texmat,32,texmat );
	if( uni->ps_stage>=0 ) GX2SetPixelUniformReg( uni->ps_stage,16,ps_stage );

	m->render( first_vert,vert_cnt,first_tri,tri_cnt );
	tris_drawn+=tri_cnt;

	//extra passes for stages the hardware path doesn't cover : texture only, blended onto the result
	for( int k=first_pass;k<n_stages;++k ){
		const RenderState::TexState &ts=*stages[k];
		int b=ts.blend==BLEND_ADD ? 100 : ts.blend;
		setBlend( b );
		float st[16],ps[16],tmx[32];
		memset( st,0,sizeof(st) );memset( ps,0,sizeof(ps) );memset( tmx,0,sizeof(tmx) );
		st[0]=ps[0]=1;	//BLENDTEXTUREALPHA over white diffuse = texture colour
		st[2]=(ts.flags & TEX_COORDS2) ? 1 : 0;
		if( ts.matrix ){
			const float (*e)[3]=ts.matrix->elements;
			tmx[0]=e[0][0];tmx[1]=e[1][0];tmx[2]=e[2][0];
			tmx[4]=e[0][1];tmx[5]=e[1][1];tmx[6]=e[2][1];
		}else{ tmx[0]=1;tmx[5]=1; }
		float white[4]={ 1,1,1,1 },noflags[4]={ 0,0,0,0 };	//unlit
		if( uni->material>=0 ) GX2SetVertexUniformReg( uni->material,4,white );
		if( uni->vflags>=0 ) GX2SetVertexUniformReg( uni->vflags,4,noflags );
		if( uni->stage>=0 ) GX2SetVertexUniformReg( uni->stage,16,st );
		if( uni->texmat>=0 ) GX2SetVertexUniformReg( uni->texmat,32,tmx );
		if( uni->ps_stage>=0 ) GX2SetPixelUniformReg( uni->ps_stage,16,ps );
		if( uni->sampler[0]>=0 ){
			GX2SetPixelTexture( texs[k],uni->sampler[0] );
			GX2SetPixelSampler( &samplers[0][0][texs[k]->surface.mipLevels>1 ? 1 : 0],uni->sampler[0] );
		}
		m->render( first_vert,vert_cnt,first_tri,tri_cnt );
		tris_drawn+=tri_cnt;
	}
}

// ---- lights ----

void gxScene::setEnabled( gxLight *light,bool enabled ){
}

gxLight *gxScene::createLight( int flags ){
	gxLight *l=d_new gxLight( this,flags );
	_allLights.insert( l );
	return l;
}

void gxScene::freeLight( gxLight *l ){
	if( _allLights.erase( l ) ) delete l;
}

int gxScene::getTrianglesDrawn()const{
	return tris_drawn;
}
