#include "std.h"
#include "gxmesh.h"
#include "gxgraphics.h"
#include "wiiu_gpu.h"
#include "wiiu_prof.h"

#include <gx2/draw.h>
#include <gx2/mem.h>

static size_t vertBytes( int n ){ return (size_t)(n>0 ? n : 1)*sizeof(gxMesh::dxVertex); }
static size_t indexBytes( int n ){ return (size_t)(n>0 ? n : 1)*3*sizeof(uint16_t); }

gxMesh::gxMesh( gxGraphics *g,int mv,int mt ):
graphics(g),max_verts(mv),max_tris(mt),mesh_dirty(false),locked_verts(0),used_frame(0),locked_all(false),n_spares(0){
	verts=(dxVertex*)wiiu::gpuAlloc( vertBytes( max_verts ),GX2_VERTEX_BUFFER_ALIGNMENT );
	tri_indices=(uint16_t*)wiiu::gpuAlloc( indexBytes( max_tris ),GX2_INDEX_BUFFER_ALIGNMENT );
}

gxMesh::~gxMesh(){
	wiiu::gpuFreeDeferred( verts );
	wiiu::gpuFreeDeferred( tri_indices );
	for( int k=0;k<n_spares;++k ){
		wiiu::gpuFreeDeferred( spares[k].verts );
		wiiu::gpuFreeDeferred( spares[k].tri_indices );
	}
}

bool gxMesh::lock( bool all ){
	if( !verts || !tri_indices ) return false;
	if( wiiu::gpuFrameBusy( used_frame ) ){
		//the GPU may still read the current buffers : write into others (a spare pair the GPU is done with, or
		//new ones)
		dxVertex *nv=0;uint16_t *ni=0;
		for( int k=0;k<n_spares;++k ){
			if( wiiu::gpuFrameBusy( spares[k].frame ) ) continue;
			nv=spares[k].verts;ni=spares[k].tri_indices;
			spares[k]=spares[--n_spares];
			break;
		}
		if( !nv ){
			BB_PROF_SCOPE( "[gxMesh::lock, new buffers]" );
			nv=(dxVertex*)wiiu::gpuAlloc( vertBytes( max_verts ),GX2_VERTEX_BUFFER_ALIGNMENT );
			ni=(uint16_t*)wiiu::gpuAlloc( indexBytes( max_tris ),GX2_INDEX_BUFFER_ALIGNMENT );
		}
		if( !nv || !ni ){
			wiiu::gpuFree( nv );wiiu::gpuFree( ni );
			wiiu::gpuSync();
		}else{
			if( !all ) memcpy( nv,verts,vertBytes( max_verts ) );
			memcpy( ni,tri_indices,indexBytes( max_tris ) );
			if( n_spares<MAX_SPARES ){
				Spare s={ verts,tri_indices,used_frame };
				spares[n_spares++]=s;
			}else{
				wiiu::gpuFreeDeferred( verts );
				wiiu::gpuFreeDeferred( tri_indices );
			}
			verts=nv;tri_indices=ni;
		}
		used_frame=0;
	}
	locked_verts=verts;
	locked_all=all;
	return true;
}

void gxMesh::unlock(){
	if( !locked_verts ) return;
	BB_PROF_SCOPE( "[gxMesh::unlock, cache flush]" );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,verts,vertBytes( max_verts ) );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,tri_indices,indexBytes( max_tris ) );
	locked_verts=0;
	mesh_dirty=false;
}

void gxMesh::render( int first_vert,int vert_cnt,int first_tri,int tri_cnt ){
	if( !verts || tri_cnt<=0 ) return;
	++wiiu::stats.draws;
	GX2SetAttribBuffer( 0,vertBytes( max_verts ),sizeof(dxVertex),verts );
	GX2DrawIndexedEx( GX2_PRIMITIVE_MODE_TRIANGLES,tri_cnt*3,GX2_INDEX_TYPE_U16,tri_indices+first_tri*3,first_vert,1 );
	used_frame=wiiu::gpuFrame();
}
