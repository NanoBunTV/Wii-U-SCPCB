
#ifndef MESHCOLLIDER_H
#define MESHCOLLIDER_H

#include "collision.h"

class MeshCollider{
public:
	struct Vertex{
		Vector coords;
	};
	struct Triangle{
		void *surface;
		int verts[3],index;
	};
	MeshCollider( const vector<Vertex> &verts,const vector<Triangle> &tris );
	~MeshCollider();

	//sphere collision
	bool collide( const Line &line,float radius,Collision *curr_coll,const Transform &tform );

	bool intersects( const MeshCollider &c,const Transform &t )const;

	//Wii U port : local bounding box of all triangles (empty without triangles)
	Box bounds()const{ return tree ? tree->box : Box(); }

	//Wii U port : memory report (all colliders; the tree's inner nodes are estimated)
	static void memoryStats( size_t *bytes,int *count );

private:
	static MeshCollider *_first;
	MeshCollider *_prev,*_next;

	vector<Vertex> vertices;
	vector<Triangle> triangles;

	struct Node{
		Box box;
		Node *left,*right;
		vector<int> triangles;
		//Wii U port : leaves' triangle planes (4 per triangle), made on first use for the y scale planes_ys, in a
		//cache of bounded size shared by all colliders (see leafPlanes)
		Plane *planes;
		float planes_ys;
		int cache_slot;
		Node():left(0),right(0),planes(0),planes_ys(0),cache_slot(-1){}
		~Node();
	};

	Node *tree;
	vector<Node*> leaves;

	Box nodeBox( const vector<int> &tris );
	Node *createLeaf( const vector<int> &tris );
	Node *createNode( const vector<int> &tris );
	bool collide( const Box &box,const Line &line,float radius,const Transform &tform,Collision *curr_coll,Node *node );

	//Wii U port : local-space sweep (see collide()) against 4 planes per triangle (triangle, 3 edges) for the
	//vertices scaled in y by ys (colliders with different x / y radii, which the world scales in y)
	const Plane *leafPlanes( Node *leaf,float ys );
	bool collideLocal( const Box &box,const Line &line,float radius,Collision *curr_coll,Node *node,float ys );
};

#endif