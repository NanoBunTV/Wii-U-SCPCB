
#include "std.h"
#include "meshcollider.h"
#include <algorithm>

static const int MAX_COLL_TRIS=16;
static vector<Vector> tri_centres;

extern float stats3d[10];

extern gxRuntime *gx_runtime;

static bool triTest( const Vector a[3],const Vector b[3] ){
	bool pb0=false,pb1=false,pb2=false;
	Plane p( a[0],a[1],a[2] ),p0,p1,p2;
	for( int k=0;k<3;++k ){
		Line l( b[k],b[(k+1)%3]-b[k] );
		float t=p.t_intersect( l );
		if( t<0 || t>1 ) continue;
		Vector i=l*t;
		if( !pb0 ){ p0=Plane( a[0]+p.n,a[1],a[0] );pb0=true; }
		if( p0.distance( i )<0 ) continue;
		if( !pb1 ){ p1=Plane( a[1]+p.n,a[2],a[1] );pb1=true; }
		if( p1.distance( i )<0 ) continue;
		if( !pb2 ){ p2=Plane( a[2]+p.n,a[0],a[2] );pb2=true; }
		if( p2.distance( i )<0 ) continue;
		return true;
	}
	return false;
}

static bool trisIntersect( const Vector a[3],const Vector b[3] ){
	return triTest( a,b ) || triTest( b,a );
}

MeshCollider::MeshCollider( const vector<Vertex> &verts,const vector<Triangle> &tris ):
vertices(verts),triangles(tris){
	vector<int> ts;
	tri_centres.clear();
	for( int k=0;k<triangles.size();++k ){
		const MeshCollider::Triangle &t=triangles[k];
		const Vector &v0=vertices[t.verts[0]].coords;
		const Vector &v1=vertices[t.verts[1]].coords;
		const Vector &v2=vertices[t.verts[2]].coords;
		tri_centres.push_back( (v0+v1+v2)/3 );
		ts.push_back( k );
	}
	tree=createNode( ts );
}

MeshCollider::~MeshCollider(){
	delete tree;
}

//Wii U port : does the swept segment (line, t in 0..1, grown by radius) cross the box? Slab test : prunes the
//tree much better than the segment's bounding box for long diagonal lines (EntityVisible, picks).
struct SegTest{
	float o[3],inv[3],r;
	bool flat[3];
	SegTest( const Line &l,float radius ):r( radius ){
		const float *lo=&l.o.x,*ld=&l.d.x;
		for( int a=0;a<3;++a ){
			o[a]=lo[a];
			flat[a]=fabs( ld[a] )<1e-12f;
			inv[a]=flat[a] ? 0 : 1/ld[a];
		}
	}
	bool hits( const Box &b )const{
		const float *ba=&b.a.x,*bb=&b.b.x;
		float t0=0,t1=1;
		for( int a=0;a<3;++a ){
			float lo=ba[a]-r,hi=bb[a]+r;
			if( flat[a] ){
				if( o[a]<lo || o[a]>hi ) return false;
				continue;
			}
			float ta=(lo-o[a])*inv[a],tb=(hi-o[a])*inv[a];
			if( ta>tb ){ float t=ta;ta=tb;tb=t; }
			if( ta>t0 ) t0=ta;
			if( tb<t1 ) t1=tb;
			if( t0>t1 ) return false;
		}
		return true;
	}
};
static const SegTest *seg_test;

//Wii U port : true if the transform is a rotation and a uniform scale (no shear, no mirroring), so a sphere
//stays a sphere in the mesh's own space
static bool isSimilarity( const Matrix &m,float *scale ){
	float li=m.i.length(),lj=m.j.length(),lk=m.k.length();
	if( !(li>0) ) return false;
	float tol=li*1e-4f;
	if( fabs( li-lj )>tol || fabs( li-lk )>tol ) return false;
	float tol2=li*li*1e-4f;
	if( fabs( m.i.dot( m.j ) )>tol2 || fabs( m.j.dot( m.k ) )>tol2 || fabs( m.k.dot( m.i ) )>tol2 ) return false;
	if( m.i.cross( m.j ).dot( m.k )<=0 ) return false;
	*scale=li;
	return true;
}

bool MeshCollider::collide( const Line &line,float radius,Collision *curr_coll,const Transform &t ){

	if( !tree ) return false;

	float scale;
	if( isSimilarity( t.m,&scale ) ){
		//Wii U port : sweep in the mesh's space (line and radius transformed once) against triangle planes
		//computed once per collider, instead of transforming 3 vertices and building 4 normalised planes per
		//triangle. The collision time is the same in both spaces; the normal is turned back to world space.
		if( tri_planes.empty() ) makePlanes();
		Transform inv=-t;
		Line local_line( inv*line.o,inv.m*line.d );
		float local_radius=radius/scale;
		Box local_box( local_line );
		local_box.expand( local_radius );
		float saved_eps=collision_eps;
		collision_eps=COLLISION_EPSILON/scale;	//distances in update() / triangleCollide are in local units
		SegTest st( local_line,local_radius );
		seg_test=&st;
		bool hit=collideLocal( local_box,local_line,local_radius,curr_coll,tree );
		seg_test=0;
		collision_eps=saved_eps;
		if( hit ) curr_coll->normal=(t.m*curr_coll->normal).normalized();
		return hit;
	}

	//create local box
	Box box( line );
	box.expand( radius );
	Box local_box=-t * box;

	return collide( local_box,line,radius,t,curr_coll,tree );
}

void MeshCollider::makePlanes(){
	tri_planes.resize( triangles.size()*4 );
	for( size_t k=0;k<triangles.size();++k ){
		const Triangle &tri=triangles[k];
		const Vector &v0=vertices[ tri.verts[0] ].coords;
		const Vector &v1=vertices[ tri.verts[1] ].coords;
		const Vector &v2=vertices[ tri.verts[2] ].coords;
		Plane p( v0,v1,v2 );
		tri_planes[k*4]=p;
		tri_planes[k*4+1]=Plane( v0+p.n,v1,v0 );
		tri_planes[k*4+2]=Plane( v1+p.n,v2,v1 );
		tri_planes[k*4+3]=Plane( v2+p.n,v0,v2 );
	}
}

bool MeshCollider::collideLocal( const Box &line_box,const Line &line,float radius,Collision *curr_coll,MeshCollider::Node *node ){
	++stats3d[2];
	if( !line_box.overlaps( node->box ) ) return false;
	if( seg_test && !seg_test->hits( node->box ) ) return false;

	bool hit=false;
	if( !node->triangles.size() ){
		if( node->left ) hit|=collideLocal( line_box,line,radius,curr_coll,node->left );
		if( node->right ) hit|=collideLocal( line_box,line,radius,curr_coll,node->right );
		return hit;
	}

	stats3d[0]+=node->triangles.size();

	for( int k=0;k<node->triangles.size();++k ){
		int n=node->triangles[k];
		const Triangle &tri=triangles[n];
		const Vector &v0=vertices[ tri.verts[0] ].coords;
		const Vector &v1=vertices[ tri.verts[1] ].coords;
		const Vector &v2=vertices[ tri.verts[2] ].coords;

		Box tri_box( v0 );
		tri_box.update( v1 );
		tri_box.update( v2 );
		if( !tri_box.overlaps( line_box ) ) continue;
		++stats3d[3];

		const Plane *p=&tri_planes[n*4];
		if( !curr_coll->triangleCollide( line,radius,v0,v1,v2,p[0],p[1],p[2],p[3] ) ) continue;

		curr_coll->surface=tri.surface;
		curr_coll->index=tri.index;
		hit=true;
	}
	return hit;
}

bool MeshCollider::collide( const Box &line_box,const Line &line,float radius,const Transform &tform,Collision *curr_coll,MeshCollider::Node *node ){
	++stats3d[2];
	if( !line_box.overlaps( node->box ) ){
		return false;
	}

	bool hit=false;
	if( !node->triangles.size() ){
		if( node->left ) hit|=collide( line_box,line,radius,tform,curr_coll,node->left );
		if( node->right ) hit|=collide( line_box,line,radius,tform,curr_coll,node->right );
		return hit;
	}

	stats3d[0]+=node->triangles.size();

	for( int k=0;k<node->triangles.size();++k ){

		const Triangle &tri=triangles[node->triangles[k]];
		const Vector &t_v0=vertices[ tri.verts[0] ].coords;
		const Vector &t_v1=vertices[ tri.verts[1] ].coords;
		const Vector &t_v2=vertices[ tri.verts[2] ].coords;

		//tri box
		Box tri_box( t_v0 );
		tri_box.update( t_v1 );
		tri_box.update( t_v2 );
		if( !tri_box.overlaps( line_box ) ) continue;
		++stats3d[3];

		if( !curr_coll->triangleCollide( line,radius,tform*t_v0,tform*t_v1,tform*t_v2 ) ) continue;

		curr_coll->surface=tri.surface;
		curr_coll->index=tri.index;
		
		hit=true;
	}
	return hit;
}

Box MeshCollider::nodeBox( const vector<int> &tris ){
	Box box;
	for( int k=0;k<tris.size();++k ){
		const Triangle &t=triangles[ tris[k] ];
		for( int j=0;j<3;++j ) box.update( vertices[t.verts[j]].coords );
	}
	return box;
}

MeshCollider::Node *MeshCollider::createLeaf( const vector<int> &tris ){

	Node *c=d_new Node;
	c->box=nodeBox( tris );
	c->triangles=tris;
	leaves.push_back( c );
	return c;
}

MeshCollider::Node *MeshCollider::createNode( const vector<int> &tris ){

	if( tris.size()<=MAX_COLL_TRIS ) return createLeaf( tris );

	Node *c=d_new Node;
	c->box=nodeBox( tris );

	//find longest axis
	//
	float max=c->box.width();
	if( c->box.height()>max ) max=c->box.height();
	if( c->box.depth()>max ) max=c->box.depth();
	int axis=0;
	if( max==c->box.height() ) axis=1;
	else if( max==c->box.depth() ) axis=2;

	//split at the median along the axis
	//Wii U port : nth_element instead of sorting through a multimap (one allocation per triangle and level;
	//collider builds took seconds of loading on the console)
	vector<int> sorted( tris );
	size_t half=sorted.size()/2;
	nth_element( sorted.begin(),sorted.begin()+half,sorted.end(),
		[axis]( int a,int b ){ return tri_centres[a][axis]<tri_centres[b][axis]; } );

	c->left=createNode( vector<int>( sorted.begin(),sorted.begin()+half ) );
	c->right=createNode( vector<int>( sorted.begin()+half,sorted.end() ) );

	return c;
}

bool MeshCollider::intersects( const MeshCollider &c,const Transform &t )const{

	static Vector a[MAX_COLL_TRIS][3],b[3];
	
	if( !(t * tree->box).overlaps( c.tree->box ) ) return false;
	for( int k=0;k<leaves.size();++k ){
		Node *p=leaves[k];
		Box box=t*p->box;
		bool tformed=false;
		for( int j=0;j<c.leaves.size();++j ){
			Node *q=c.leaves[j];
			if( !box.overlaps( q->box ) ) continue;
			if( !tformed ){
				for( int n=0;n<p->triangles.size();++n ){
					const Triangle &tri=triangles[p->triangles[n]];
					a[n][0]=t * vertices[tri.verts[0]].coords;
					a[n][1]=t * vertices[tri.verts[1]].coords;
					a[n][2]=t * vertices[tri.verts[2]].coords;
				}
				tformed=true;
			}
			for( int n=0;n<q->triangles.size();++n ){
				const Triangle &tri=c.triangles[q->triangles[n]];
				b[0]=c.vertices[tri.verts[0]].coords;
				b[1]=c.vertices[tri.verts[1]].coords;
				b[2]=c.vertices[tri.verts[2]].coords;
				for( int t=0;t<p->triangles.size();++t ){
					if( trisIntersect( a[t],b ) ) return true;
				}
			}
		}
	}
	return false;
}
