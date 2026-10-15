// wiiu.decls : bulk readers for LoadRMesh (MapSystem.bb). The game read every room mesh with ReadFloat / ReadByte /
// ReadInt and AddVertex / VertexTexCoords / VertexColor / AddTriangle calls : about 14 runtime calls per vertex
// (LoadRMesh took ≈ 6.5 s of a map's loading on the console). These do the same work with one read per block.
#include "../bbruntime/std.h"
#include "../bbruntime/bbstream.h"
#include "../bbruntime/endian.h"
#include "../blitz3d/surface.h"
#include "../gxruntime/gxfilesystem.h"

//aligned : big reads go straight from the file system into it
static FileData rmesh_buf;

//reads n bytes (zeros past the end of the file, as ReadFloat / ReadInt return 0 there)
static const unsigned char *readBlock( bbStream *s,int n ){
	rmesh_buf.resize( n>0 ? n : 0 );
	if( n<=0 ) return rmesh_buf.data();
	int got=s->read( (char*)rmesh_buf.data(),n );
	if( got<0 ) got=0;
	if( got<n ) memset( rmesh_buf.data()+got,0,n-got );
	return rmesh_buf.data();
}

static inline float leFloat( const unsigned char *p ){
	return bbIntBitsToFloat( bbGetLE32( p ) );
}

// full=1 : per vertex x,y,z, two (u,v) sets and r,g,b bytes (drawn meshes); full=0 : x,y,z only (collision,
// trigger boxes). Same results as AddVertex + VertexTexCoords + VertexColor ...,1.0
void _fwiiu_readrmeshvertices( int surf,int file,int count,int full ){
	Surface *s=(Surface*)surf;
	bbStream *f=(bbStream*)file;
	if( count<=0 ) return;
	int size=full ? 31 : 12;
	const unsigned char *p=readBlock( f,count*size );
	for( int k=0;k<count;++k,p+=size ){
		Surface::Vertex v;
		v.coords=Vector( leFloat( p ),leFloat( p+4 ),leFloat( p+8 ) );
		if( full ){
			v.tex_coords[0][0]=leFloat( p+12 );v.tex_coords[0][1]=leFloat( p+16 );
			v.tex_coords[1][0]=leFloat( p+20 );v.tex_coords[1][1]=leFloat( p+24 );
			v.color=0xff000000|(p[28]<<16)|(p[29]<<8)|p[30];
		}
		s->addVertex( v );
	}
}

// both=1 : each triangle is also added reversed (AddTriangle v0,v2,v1 right after), as the collision meshes do
void _fwiiu_readrmeshtriangles( int surf,int file,int count,int both ){
	Surface *s=(Surface*)surf;
	bbStream *f=(bbStream*)file;
	if( count<=0 ) return;
	const unsigned char *p=readBlock( f,count*12 );
	for( int k=0;k<count;++k,p+=12 ){
		Surface::Triangle t;
		t.verts[0]=(int)bbGetLE32( p );t.verts[1]=(int)bbGetLE32( p+4 );t.verts[2]=(int)bbGetLE32( p+8 );
		s->addTriangle( t );
		if( both ){
			int v=t.verts[1];t.verts[1]=t.verts[2];t.verts[2]=v;
			s->addTriangle( t );
		}
	}
}
