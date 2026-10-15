
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

//Wii U port : does the swept segment (line, t in 0..1, grown by radius) cross the box? Slab test : prunes the
//tree much better than the segment's bounding box for long diagonal lines (EntityVisible, picks); also the
//world-box rejection of EntityVisible / picks (World::checkLOS, traceRay).
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

#endif