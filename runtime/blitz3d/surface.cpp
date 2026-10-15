
#include "../gxruntime/wiiu_prof.h"
#include "std.h"
#include "surface.h"

unsigned Surface::_geom_gen;
#include "../gxruntime/wiiu_gpu.h"
#include <gx2/mem.h>
#include <gx2/enum.h>

extern gxGraphics *gx_graphics;

static Surface::Monitor nop_mon;

Surface::Surface():
mesh(0),mesh_vs(0),mesh_ts(0),valid_vs(0),valid_ts(0),mon( &nop_mon ),skin_pose(0),skin_geom(0),cpu_skinned(false),skin_buf(0),skin_buf_vs(0),skin_buf_geom(0){
	link();
}

Surface::Surface( Monitor *m ):
mesh(0),mesh_vs(0),mesh_ts(0),valid_vs(0),valid_ts(0),mon(m),skin_pose(0),skin_geom(0),cpu_skinned(false),skin_buf(0),skin_buf_vs(0),skin_buf_geom(0){
	link();
}

Surface::~Surface(){
	if( mesh ) gx_graphics->freeMesh( mesh );
	wiiu::gpuFreeDeferred( skin_buf );
	if( _prev ) _prev->_next=_next;else _first=_next;
	if( _next ) _next->_prev=_prev;
}

Surface *Surface::_first;

void Surface::link(){
	_prev=0;_next=_first;
	if( _first ) _first->_prev=this;
	_first=this;
}

void Surface::memoryStats( size_t *cpu_bytes,size_t *skin_bytes,int *count ){
	size_t b=0,sk=0;int n=0;
	size_t nv=0,nt=0,cv=0,ct=0,mesh_v=0;int with_mesh=0;
	for( Surface *s=_first;s;s=s->_next ){
		b+=s->vertices.capacity()*sizeof(Vertex)+s->triangles.capacity()*sizeof(Triangle);
		nv+=s->vertices.size();nt+=s->triangles.size();
		cv+=s->vertices.capacity();ct+=s->triangles.capacity();
		if( s->mesh ){ ++with_mesh;mesh_v+=s->vertices.size(); }
		if( s->skin_buf ) sk+=s->skin_buf_vs*32;
		++n;
	}
	*cpu_bytes=b;*skin_bytes=sk;*count=n;
	extern gxRuntime *gx_runtime;
	gx_runtime->debugLog( ("memory : surfaces : "+itoa( nv )+" vertices (capacity "+itoa( cv )+"), "+itoa( nt )+
		" triangles (capacity "+itoa( ct )+"); "+itoa( with_mesh )+" drawn ("+itoa( mesh_v )+" vertices)").c_str() );
}

void Surface::setBrush( const Brush &b ){
	brush=b;
	++mon->brush_changes;
}

void Surface::setName( const string &n ){
	name=n;
}

void Surface::clear( bool verts,bool tris ){
	if( verts ){ vertices.clear();valid_vs=0; }
	if( tris ){ triangles.clear();valid_ts=0; }
	++mon->geom_changes;++_geom_gen;
}

void Surface::addVertices( const vector<Vertex> &verts ){
	reserveMore( verts.size(),0 );
	vertices.insert( vertices.end(),verts.begin(),verts.end() );
	++mon->geom_changes;++_geom_gen;
}

void Surface::setColor( int n,const Vector &v ){
	int r=floor(v.x*255);if(r<0)r=0;else if(r>255)r=255;
	int g=floor(v.y*255);if(g<0)g=0;else if(g>255)g=255;
	int b=floor(v.z*255);if(b<0)b=0;else if(b>255)b=255;

	unsigned argb=0xff000000|(r<<16)|(g<<8)|b;

	vertices[n].color=argb;
	if( n<valid_vs ) valid_vs=n;
}

Vector Surface::getColor( int n )const{
	float r=(vertices[n].color&0x00ff0000)>>16;
	float g=(vertices[n].color&0x0000ff00)>>8;
	float b= vertices[n].color&0x000000ff;
	return Vector( r/255.0f,g/255.0f,b/255.0f );
}

void Surface::addTriangles( const vector<Triangle> &tris ){
	reserveMore( 0,tris.size() );
	triangles.insert( triangles.end(),tris.begin(),tris.end() );
}

//exact growth : a mesh built in a few big steps (LoadRMesh's AddMesh calls) ends up with no unused capacity,
//and a mesh built in many small ones still grows by a quarter at least (not quadratic copying)
template<class T> static void growFor( vector<T> &v,size_t more ){
	size_t need=v.size()+more;
	if( need<=v.capacity() ) return;
	size_t step=v.capacity()+v.capacity()/4;
	v.reserve( need>step ? need : step );
}

void Surface::reserveMore( int nv,int nt ){
	if( nv>0 ) growFor( vertices,nv );
	if( nt>0 ) growFor( triangles,nt );
}

void Surface::addReversedTriangles(){
	int n=triangles.size();
	if( !n ) return;
	reserveMore( 0,n );
	for( int k=0;k<n;++k ){
		Triangle t=triangles[k];
		std::swap( t.verts[1],t.verts[2] );
		triangles.push_back( t );
	}
	++mon->geom_changes;++_geom_gen;
}

void Surface::updateNormals(){
	int k;
	map<Vector,Vector> norm_map;
	for( k=0;k<triangles.size();++k ){
		const Triangle &t=triangles[k];
		const Vector &v0=vertices[t.verts[0]].coords;
		const Vector &v1=vertices[t.verts[1]].coords;
		const Vector &v2=vertices[t.verts[2]].coords;
		Vector n=(v1-v0).cross(v2-v0);
		if( n.length()<=EPSILON ) continue;
		n.normalize();
		norm_map[v0]+=n;
		norm_map[v1]+=n;
		norm_map[v2]+=n;
	}
	for( k=0;k<vertices.size();++k ){
		Vertex *v=&vertices[k];
		v->normal=norm_map[v->coords].normalized();
	}
}

gxMesh *Surface::getMesh(){
	if( mesh && mesh->dirty() ) valid_vs=0;

	if( valid_vs==vertices.size() && valid_ts==triangles.size() ) return mesh;

	BB_PROF_SCOPE( "[Surface::getMesh, rebuild]" );
	valid_vs=valid_ts=0;

	if( mesh_vs<vertices.size() || mesh_ts<triangles.size() ){
		if( mesh ){
			gx_graphics->freeMesh( mesh );
			mesh_vs=vertices.size()+mesh_vs/2;
			mesh_ts=triangles.size()+mesh_ts/2;
		}else{
			mesh_vs=vertices.size();
			mesh_ts=triangles.size();
		}
		mesh=gx_graphics->createMesh( mesh_vs,mesh_ts,0 );
	}

	mesh->lock( true );
	for( ;valid_vs<vertices.size();++valid_vs ){
		mesh->setVertex( valid_vs,&vertices[valid_vs] );
	}
	for( ;valid_ts<triangles.size();++valid_ts ){
		const Triangle &t=triangles[valid_ts];
		mesh->setTriangle( valid_ts,t.verts[0],t.verts[1],t.verts[2] );
	}
	mesh->unlock();
	return mesh;
}

gxMesh *Surface::getBindMesh(){
	if( cpu_skinned ){ valid_vs=valid_ts=0;cpu_skinned=false;skin_pose=0; }
	return getMesh();
}

const void *Surface::getSkinBuffer( int *bytes ){
	int n=vertices.size();
	*bytes=n*32;
	if( skin_buf && skin_buf_vs==n && skin_buf_geom==mon->geom_changes ) return skin_buf;
	wiiu::gpuFreeDeferred( skin_buf );
	skin_buf=wiiu::gpuAlloc( n>0 ? n*32 : 32,GX2_VERTEX_BUFFER_ALIGNMENT );
	if( !skin_buf ) return 0;
	float *p=(float*)skin_buf;
	for( int k=0;k<n;++k,p+=8 ){
		const Vertex &v=vertices[k];
		for( int j=0;j<8;++j ) p[j]=0;
		if( v.bone_bones[0]==255 ){
			p[4]=1;						//no bone : bone 0
		}else if( v.bone_bones[1]==255 ){
			p[0]=v.bone_bones[0];p[4]=1;	//one bone : weight 1 whatever is stored (as the CPU path)
		}else{
			for( int j=0;j<MAX_SURFACE_BONES && v.bone_bones[j]!=255;++j ){
				p[j]=v.bone_bones[j];p[4+j]=v.bone_weights[j];
			}
		}
	}
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER,skin_buf,n*32 );
	skin_buf_vs=n;
	skin_buf_geom=mon->geom_changes;
	return skin_buf;
}

gxMesh *Surface::getMesh( const vector<Bone> &bones,unsigned pose ){
	if( pose && pose==skin_pose && skin_geom==mon->geom_changes && mesh && !mesh->dirty() &&
		valid_vs==vertices.size() && valid_ts==triangles.size() ) return mesh;
	gxMesh *m=getMesh( bones );
	skin_pose=pose;
	skin_geom=mon->geom_changes;
	return m;
}

gxMesh *Surface::getMesh( const vector<Bone> &bones ){
	BB_PROF_SCOPE( "[Surface::getMesh, skinning]" );
	skin_pose=0;
	cpu_skinned=true;
	{
		static int n,nv,nt;
		++n;nv+=vertices.size();nt+=triangles.size();
		if( n==600 ){
			extern gxRuntime *gx_runtime;
			gx_runtime->debugLog( ("skinning : "+itoa( nv/n )+" vertices, "+itoa( nt/n )+" triangles per surface").c_str() );
			n=nv=nt=0;
		}
	}

	valid_vs=valid_ts=0;

	if( mesh_vs<vertices.size() || mesh_ts<triangles.size() ){
		if( mesh ) gx_graphics->freeMesh( mesh );
		mesh_vs=vertices.size();
		mesh_ts=triangles.size();
		mesh=gx_graphics->createMesh( mesh_vs,mesh_ts,0 );
	}

	mesh->lock( true );
	for( ;valid_vs<vertices.size();++valid_vs ){
		const Vertex &v=vertices[valid_vs];
		if( v.bone_bones[0]==255 ){
			//no bone!
			const Bone &bone=bones[0];
			mesh->setVertex( valid_vs,bone.coord_tform * v.coords,bone.normal_tform * v.normal,v.color,v.tex_coords );
		}else if( v.bone_bones[1]==255 ){
			//one bone only
			const Bone &bone=bones[v.bone_bones[0]];
			mesh->setVertex( valid_vs,bone.coord_tform * v.coords,bone.normal_tform * v.normal,v.color,v.tex_coords );
		}else{
			const Vertex &v=vertices[valid_vs];
			//two or more bones
			Vector tv,tn;
			for( int n=0;n<MAX_SURFACE_BONES;++n ){
				if( v.bone_bones[n]==255 ) break;
				const Bone &bone=bones[v.bone_bones[n]];
				tv+=bone.coord_tform * v.coords * v.bone_weights[n];
				tn+=bone.normal_tform * v.normal * v.bone_weights[n];
			}
			mesh->setVertex( valid_vs,tv,tn.normalized(),v.color,v.tex_coords );
		}
	}
	for( ;valid_ts<triangles.size();++valid_ts ){
		const Triangle &t=triangles[valid_ts];
		mesh->setTriangle( valid_ts,t.verts[0],t.verts[1],t.verts[2] );
	}
	mesh->unlock();
	return mesh;
}

/*
gxMesh *Surface::getMesh(){
	if( mesh && mesh->dirty() ) valid_vs=0;

	if( valid_vs==vertices.size() && valid_ts==triangles.size() ) return mesh;

	valid_vs=0;
	valid_ts=0;

	if( mesh ){
		int maxvs=mesh->maxVerts(),maxts=mesh->maxTris();
		if( maxvs<vertices.size() || maxts<triangles.size() ){
			gx_graphics->freeMesh( mesh );
			mesh=gx_graphics->createMesh( vertices.size()+maxvs/2,triangles.size()+maxts/2,0 );
		}
	}else if( vertices.size() || triangles.size() ){
		mesh=gx_graphics->createMesh( vertices.size(),triangles.size(),0 );
	}

	mesh->lock( true );
	for( ;valid_vs<vertices.size();++valid_vs ){
		mesh->setVertex( valid_vs,&vertices[valid_vs] );
	}
	for( ;valid_ts<triangles.size();++valid_ts ){
		const Triangle &t=triangles[valid_ts];
		mesh->setTriangle( valid_ts,t.verts[0],t.verts[1],t.verts[2] );
	}
	mesh->unlock();
	return mesh;
}

gxMesh *Surface::getMesh( const vector<Bone> &bones,gxMesh *mesh ){

	valid_vs=0;
	valid_ts=0;

	if( mesh ){
		int maxvs=mesh->maxVerts(),maxts=mesh->maxTris();
		if( maxvs<vertices.size() || maxts<triangles.size() ){
			gx_graphics->freeMesh( mesh );
			mesh=gx_graphics->createMesh( vertices.size(),triangles.size(),0 );

		}
	}else if( vertices.size() || triangles.size() ){
		mesh=gx_graphics->createMesh( vertices.size(),triangles.size(),0 );
	}

	mesh->lock( true );
	for( ;valid_vs<vertices.size();++valid_vs ){
		const Vertex &v=vertices[valid_vs];
		if( v.bone_bones[0]==255 ){
			//no bone!
			const Bone &bone=bones[0];
			mesh->setVertex( valid_vs,bone.coord_tform * v.coords,bone.normal_tform * v.normal,v.color,v.tex_coords );
		}else if( v.bone_bones[1]==255 ){
			//one bone only
			const Bone &bone=bones[v.bone_bones[0]];
			mesh->setVertex( valid_vs,bone.coord_tform * v.coords,bone.normal_tform * v.normal,v.color,v.tex_coords );
		}else{
			const Vertex &v=vertices[valid_vs];
			//two or more bones
			Vector tv,tn;
			for( int n=0;n<MAX_SURFACE_BONES;++n ){
				if( v.bone_bones[n]==255 ) break;
				const Bone &bone=bones[v.bone_bones[n]];
				tv+=bone.coord_tform * v.coords * v.bone_weights[n];
				tn+=bone.normal_tform * v.normal * v.bone_weights[n];
			}
			mesh->setVertex( valid_vs,tv,tn.normalized(),v.color,v.tex_coords );
		}
	}
	for( ;valid_ts<triangles.size();++valid_ts ){
		const Triangle &t=triangles[valid_ts];
		mesh->setTriangle( valid_ts,t.verts[0],t.verts[1],t.verts[2] );
	}
	mesh->unlock();
	return mesh;
}

*/

