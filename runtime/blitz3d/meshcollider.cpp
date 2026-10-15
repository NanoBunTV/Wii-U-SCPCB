
#include "std.h"
#include "meshcollider.h"
#include "../gxruntime/wiiu_prof.h"
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
	_prev=0;_next=_first;
	if( _first ) _first->_prev=this;
	_first=this;
}

MeshCollider::~MeshCollider(){
	delete tree;
	if( _prev ) _prev->_next=_next;else _first=_next;
	if( _next ) _next->_prev=_prev;
}

MeshCollider *MeshCollider::_first;

void MeshCollider::memoryStats( size_t *bytes,int *count ){
	size_t vb=0,tb=0,pb=0,nb=0;int n=0;
	for( MeshCollider *c=_first;c;c=c->_next ){
		vb+=c->vertices.capacity()*sizeof(Vertex);
		tb+=c->triangles.capacity()*sizeof(Triangle);
		for( size_t k=0;k<c->leaves.size();++k ){
			if( c->leaves[k]->planes ) pb+=c->leaves[k]->triangles.size()*4*sizeof(Plane);
		}
		//a binary tree : about as many inner nodes as leaves, the triangle lists only in the leaves
		nb+=sizeof(MeshCollider)+c->leaves.capacity()*sizeof(Node*)+c->leaves.size()*2*(sizeof(Node)+16);
		for( size_t k=0;k<c->leaves.size();++k ) nb+=c->leaves[k]->triangles.capacity()*sizeof(int);
		++n;
	}
	*bytes=vb+tb+pb+nb;*count=n;
	extern gxRuntime *gx_runtime;
	gx_runtime->debugLog( ("memory : colliders : vertices "+itoa( vb>>10 )+" KB, triangles "+itoa( tb>>10 )+
		" KB, planes "+itoa( pb>>10 )+" KB, tree "+itoa( nb>>10 )+" KB").c_str() );
}

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

//Wii U port : m = Y * M, where Y scales y by ys and M is a similarity turning only about the y axis (rooms and
//props in SCP:CB). Y and such a turn commute, so a sphere swept in the y-scaled world against the mesh is a sphere
//swept against the mesh's vertices scaled in y by ys, under the similarity *out (m with j.y divided by ys).
static bool yScaledSimilarity( const Matrix &m,Matrix *out,float *ys ){
	float li=m.i.length();
	if( !(li>0) ) return false;
	float tol=li*1e-4f;
	if( fabs( m.i.y )>tol || fabs( m.k.y )>tol || fabs( m.j.x )>tol || fabs( m.j.z )>tol || !(m.j.y>0) ) return false;
	*ys=m.j.y/li;
	*out=m;
	out->j.y=li;
	return true;
}

bool MeshCollider::collide( const Line &line,float radius,Collision *curr_coll,const Transform &t ){

	if( !tree ) return false;

	float scale,ys=1;
	Transform lt=t;
	bool local=isSimilarity( t.m,&scale );
	if( !local && yScaledSimilarity( t.m,&lt.m,&ys ) ) local=isSimilarity( lt.m,&scale );
	if( local ){
		//Wii U port : sweep in the mesh's space (line and radius transformed once) against triangle planes
		//cached per leaf, instead of transforming 3 vertices and building 4 normalised planes per triangle.
		//The collision time is the same in both spaces; the normal is turned back to world space.
		Transform inv=-lt;
		Line local_line( inv*line.o,inv.m*line.d );
		float local_radius=radius/scale;
		Box local_box( local_line );
		local_box.expand( local_radius );
		float saved_eps=collision_eps;
		collision_eps=COLLISION_EPSILON/scale;	//distances in update() / triangleCollide are in local units
		SegTest st( local_line,local_radius );
		seg_test=&st;
		bool hit=collideLocal( local_box,local_line,local_radius,curr_coll,tree,ys );
		seg_test=0;
		collision_eps=saved_eps;
		if( hit ) curr_coll->normal=(lt.m*curr_coll->normal).normalized();
		return hit;
	}

	//create local box
	Box box( line );
	box.expand( radius );
	Box local_box=-t * box;

	return collide( local_box,line,radius,t,curr_coll,tree );
}

// Leaf plane cache : planes for every triangle of a whole map took ≈ 65 MB (64 bytes per triangle, more for each
// y scale), though only the leaves near moving colliders are ever tested. Leaves get their planes on first use;
// past PLANE_CACHE_BYTES the whole cache is dropped and refills with the leaves still in use.
enum{ PLANE_CACHE_BYTES=4*1024*1024 };
static vector<void*> plane_cache;		//leaves (MeshCollider::Node) holding planes (0 : freed slot)
static size_t plane_cache_bytes;

MeshCollider::Node::~Node(){
	delete left;delete right;
	if( planes ){
		plane_cache_bytes-=triangles.size()*4*sizeof(Plane);
		plane_cache[cache_slot]=0;
		delete[] planes;
	}
}

const Plane *MeshCollider::leafPlanes( Node *leaf,float ys ){
	if( leaf->planes && leaf->planes_ys==ys ) return leaf->planes;
	BB_PROF_SCOPE( "[collide: leaf planes]" );
	if( !leaf->planes ){
		size_t bytes=leaf->triangles.size()*4*sizeof(Plane);
		if( plane_cache_bytes+bytes>PLANE_CACHE_BYTES ){
			for( size_t k=0;k<plane_cache.size();++k ){
				if( Node *n=(Node*)plane_cache[k] ){ delete[] n->planes;n->planes=0;n->cache_slot=-1; }
			}
			plane_cache.clear();
			plane_cache_bytes=0;
		}
		leaf->planes=d_new Plane[leaf->triangles.size()*4];
		leaf->cache_slot=plane_cache.size();
		plane_cache.push_back( leaf );
		plane_cache_bytes+=bytes;
	}
	Plane *planes=leaf->planes;
	for( size_t k=0;k<leaf->triangles.size();++k,planes+=4 ){
		const Triangle &tri=triangles[ leaf->triangles[k] ];
		Vector v0=vertices[ tri.verts[0] ].coords;
		Vector v1=vertices[ tri.verts[1] ].coords;
		Vector v2=vertices[ tri.verts[2] ].coords;
		v0.y*=ys;v1.y*=ys;v2.y*=ys;
		Plane p( v0,v1,v2 );
		planes[0]=p;
		planes[1]=Plane( v0+p.n,v1,v0 );
		planes[2]=Plane( v1+p.n,v2,v1 );
		planes[3]=Plane( v2+p.n,v0,v2 );
	}
	leaf->planes_ys=ys;
	return leaf->planes;
}

bool MeshCollider::collideLocal( const Box &line_box,const Line &line,float radius,Collision *curr_coll,MeshCollider::Node *node,
	float ys ){
	++stats3d[2];
	Box nb=node->box;
	if( ys!=1 ){ nb.a.y*=ys;nb.b.y*=ys; }	//ys>0
	if( !line_box.overlaps( nb ) ) return false;
	if( seg_test && !seg_test->hits( nb ) ) return false;

	bool hit=false;
	if( !node->triangles.size() ){
		if( node->left ) hit|=collideLocal( line_box,line,radius,curr_coll,node->left,ys );
		if( node->right ) hit|=collideLocal( line_box,line,radius,curr_coll,node->right,ys );
		return hit;
	}

	stats3d[0]+=node->triangles.size();

	const Plane *planes=0;
	for( int k=0;k<node->triangles.size();++k ){
		const Triangle &tri=triangles[node->triangles[k]];
		Vector v0=vertices[ tri.verts[0] ].coords;
		Vector v1=vertices[ tri.verts[1] ].coords;
		Vector v2=vertices[ tri.verts[2] ].coords;
		v0.y*=ys;v1.y*=ys;v2.y*=ys;

		Box tri_box( v0 );
		tri_box.update( v1 );
		tri_box.update( v2 );
		if( !tri_box.overlaps( line_box ) ) continue;
		++stats3d[3];

		if( !planes ) planes=leafPlanes( node,ys );		//only leaves with a triangle near the line
		const Plane *p=&planes[k*4];
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
