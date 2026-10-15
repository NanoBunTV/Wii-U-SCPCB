
#ifndef COLLISION_H
#define COLLISION_H

#include "geom.h"

extern const float COLLISION_EPSILON;
//Wii U port : the epsilon update() and triangleCollide() use; MeshCollider scales it while it sweeps in a mesh's
//own space (COLLISION_EPSILON otherwise)
extern float collision_eps;

struct Collision{
	float time;
	Vector normal;
	void *surface;
	unsigned short index;

	Collision():time(1),surface(0),index(~0){}

	bool update( const Line &line,float time,const Vector &normal );

	bool sphereCollide( const Line &src_line,float src_radius,const Vector &dest,float dest_radius );
	bool sphereCollide( const Line &line,float radius,const Vector &dest,const Vector &radii );

	bool triangleCollide( const Line &src_line,float src_radius,const Vector &v0,const Vector &v1,const Vector &v2 );
	//Wii U port : same with the triangle's plane and edge planes precomputed (MeshCollider caches them)
	bool triangleCollide( const Line &line,float radius,const Vector &v0,const Vector &v1,const Vector &v2,
		const Plane &p,const Plane &p0,const Plane &p1,const Plane &p2 );

	bool boxCollide( const Line &src_line,float src_radius,const Box &box );
};

#endif