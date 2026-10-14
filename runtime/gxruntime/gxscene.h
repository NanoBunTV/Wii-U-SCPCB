// Wii U gxScene : DirectX 7 fixed-function pipeline emulated with one GX2 shader pair (shaders/scene.*).
// Keeps the original interface; state is collected here and uploaded as shader uniforms per draw.
#ifndef GXSCENE_H
#define GXSCENE_H

#include <set>
#include <vector>

#include "gxlight.h"
#include "wiiu_gpu.h"

class gxCanvas;
class gxMesh;
class gxLight;
class gxGraphics;

class gxScene{
public:
	gxGraphics *graphics;

	gxScene( gxGraphics *graphics,gxCanvas *target );
	~gxScene();

	void setEnabled( gxLight *light,bool enabled );

	/***** GX INTERFACE *****/
public:
	enum{
		MAX_TEXTURES=	8
	};
	enum{
		FX_FULLBRIGHT=	0x0001,
		FX_VERTEXCOLOR=	0x0002,
		FX_FLATSHADED=	0x0004,
		FX_NOFOG=		0x0008,
		FX_DOUBLESIDED=	0x0010,
		FX_VERTEXALPHA=	0x0020,

		FX_ALPHATEST=	0x2000,
		FX_CONDLIGHT=	0x4000,
		FX_EMISSIVE=	0x8000
	};
	enum{
		BLEND_REPLACE=	0,
		BLEND_ALPHA=	1,
		BLEND_MULTIPLY=	2,
		BLEND_ADD=		3,
		BLEND_DOT3=		4,
		BLEND_MULTIPLY2=5,
	};
	enum{
		ZMODE_NORMAL=	0,
		ZMODE_DISABLE=	1,
		ZMODE_CMPONLY=	2
	};
	enum{
		FOG_NONE=		0,
		FOG_LINEAR=		1
	};
	enum{
		TEX_COORDS2=	0x0001
	};
	struct Matrix{
		float elements[4][3];
	};
	struct RenderState{
		float color[3];
		float shininess,alpha;
		int blend,fx;
		struct TexState{
			gxCanvas *canvas;
			const Matrix *matrix;
			int blend,flags;
		}tex_states[MAX_TEXTURES];
	};

	//state
	int  hwTexUnits();
	int  gfxDriverCaps3D();

	void setWBuffer( bool enable );
	void setHWMultiTex( bool enable );
	void setDither( bool enable );
	void setAntialias( bool enable );
	void setWireframe( bool enable );
	void setFlippedTris( bool enable );
	void setAmbient( const float rgb[3] );
	void setAmbient2( const float rgb[3] );
	void setFogColor( const float rgb[3] );
	void setFogRange( float nr,float fr );
	void setFogMode( int mode );
	void setZMode( int mode );
	void setViewport( int x,int y,int w,int h );
	void setOrthoProj( float nr,float fr,float nr_w,float nr_h );
	void setPerspProj( float nr,float fr,float nr_w,float nr_h );
	void setViewMatrix( const Matrix *matrix );
	void setWorldMatrix( const Matrix *matrix );
	void setRenderState( const RenderState &state );

	//rendering
	bool begin( const std::vector<gxLight*> &lights );
	void clear( const float rgb[3],float alpha,float z,bool clear_argb,bool clear_z );
	void render( gxMesh *mesh,int first_vert,int vert_cnt,int first_tri,int tri_cnt );
	void end();

	//lighting
	gxLight *createLight( int flags );
	void freeLight( gxLight *l );

	//info
	int getTrianglesDrawn()const;

private:
	enum{ HW_STAGES=4, MAX_LIGHTS=8 };

	gxCanvas *target;
	bool wireframe,flipped;
	float ambient[3],ambient2[3],fogcolor[3];
	int fogmode,zmode;
	float fogrange_nr,fogrange_fr;
	int vp_x,vp_y,vp_w,vp_h;
	bool ortho_proj;
	float frustum_nr,frustum_fr,frustum_w,frustum_h;
	float projmatrix[4][4];
	Matrix viewmatrix,worldmatrix;

	RenderState rs;
	int n_texs,tris_drawn;
	bool hw_multitex;

	std::set<gxLight*> _allLights;
	std::vector<gxLight*> _curLights;

	// GPU state
	wiiu::ShaderProgram prog;
	struct Uniforms;
	Uniforms *uni;
	GX2Sampler samplers[2][2][2];		// [clampu][clampv][mipmapped]
	bool in_scene;

	void applyRasterState();
	void drawPass( gxMesh *m,int first_tri,int tri_cnt,int first_stage,int n_stages,bool single_pass_blend,int pass_blend );
};

#endif
