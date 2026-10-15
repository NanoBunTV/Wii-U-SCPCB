
#include "../gxruntime/wiiu_prof.h"
#include "std.h"
#include <queue>
#include "world.h"
#include "surface.h"

//0=tris compared for collision
//1=max proj err of terrain
float stats3d[10];

extern gxScene *gx_scene;
extern gxRuntime *gx_runtime;

static vector<Object*> _enabled,_visible;

//Wii U port : the lists are rebuilt only when the entity tree changed (Entity::treeGen). Walking the whole tree
//cost ≈ 16 ms on the console with a full map, and picks / EntityVisible did it on every call.
static unsigned enabled_gen=~0u,visible_gen=~0u;

static void enumEnabled(){
	if( enabled_gen==Entity::treeGen() ) return;
	enabled_gen=Entity::treeGen();
	_enabled.clear();
	for( Entity *e=Entity::orphans();e;e=e->successor() ){
		e->enumEnabled( _enabled );
	}
}

//Wii U port : enabled objects with a pick mode, and those of them that are obscurers (EntityVisible). While a map
//loads, ≈ 17000 objects are enabled but only ≈ 2000 are obscurers, and every EntityVisible walked all of them.
static vector<Object*> _pickables,_obscurers;
static unsigned pick_tree_gen=~0u,pick_gen=~0u,pick_rebuilds,pick_rebuilds_total;

static void enumPickables(){
	enumEnabled();
	if( pick_tree_gen==enabled_gen && pick_gen==Object::pickGen() ) return;
	pick_tree_gen=enabled_gen;
	pick_gen=Object::pickGen();
	++pick_rebuilds;++pick_rebuilds_total;
	_pickables.clear();
	_obscurers.clear();
	for( vector<Object*>::const_iterator it=_enabled.begin();it!=_enabled.end();++it ){
		Object *obj=*it;
		if( !obj->getPickGeometry() ) continue;
		_pickables.push_back( obj );
		if( obj->getObscurer() ) _obscurers.push_back( obj );
	}
}

static void enumVisible(){
	if( visible_gen==Entity::treeGen() ) return;
	visible_gen=Entity::treeGen();
	_visible.clear();
	for( Entity *e=Entity::orphans();e;e=e->successor() ){
		e->enumVisible( _visible );
	}
}

/******************************* Update *******************************/

static vector<Object*> _objsByType[1000];

static vector<ObjCollision*> free_colls,used_colls;

static ObjCollision *allocObjColl( Object *with,const Vector &coords,const Collision &coll ){
	ObjCollision *c;
	if( free_colls.size() ){
		c=free_colls.back();
		free_colls.pop_back();
	}else{
		c=new ObjCollision();
	}
	used_colls.push_back( c );
	c->with=with;
	c->coords=coords;
	c->collision=coll;
	return c;
}

static void collided( Object *src,Object *dest,const Line &line,const Collision &coll,float y_scale ){

	ObjCollision *c;
	const Vector &coords=line*coll.time-coll.normal*src->getCollisionRadii().x;

	c=allocObjColl( dest,coords,coll );
	c->coords.y*=y_scale;
	src->addCollision( c );

	c=allocObjColl( src,coords,coll );
	c->coords.y*=y_scale;
	dest->addCollision( c );
}

void World::clearCollisions(){
	for( int k=0;k<1000;++k ){
		_collInfo[k].clear();
	}
}

void World::addCollision( int src_type,int dst_type,int method,int response ){

	vector<CollInfo> &info=_collInfo[src_type];
	for( int k=0;k<info.size();++k ){
		const CollInfo &t=info[k];
		if( dst_type==t.dst_type ) return;
	}

	CollInfo co={dst_type,method,response};
	_collInfo[src_type].push_back(co);
}

//Wii U port : world boxes of the pick lists' objects in one flat array, so EntityVisible / picks only touch the
//objects whose box the line passes through (while a map loads, ≈ 2000 obscurers per EntityVisible). Rebuilt when
//any entity moved, any geometry or collision radius / box changed, or the lists changed. Meshes get their cached
//world box (worldCollideBounds), spheres / boxes their slightly enlarged world box; anything else always passes.
struct PickBoxes{
	vector<Box> boxes;
	unsigned tform_gen,geom_gen,bounds_gen,list_gen;
	bool valid;
	PickBoxes():valid(false){}
};
static PickBoxes los_boxes,ray_boxes;
static unsigned box_rebuilds;

static Box pickBox( Object *obj ){
	const Transform &tf=obj->getWorldTform();
	switch( obj->getPickGeometry() ){
	case World::COLLISION_METHOD_SPHERE:{
		Box b( tf.v );
		b.expand( fabsf( obj->getCollisionRadii().x )*1.001f+.001f );
		return b;
	}
	case World::COLLISION_METHOD_POLYGON:{
		Box wb;
		if( obj->worldCollideBounds( tf,&wb ) ) return wb;
		break;
	}
	case World::COLLISION_METHOD_BOX:{
		const Box &cb=obj->getCollisionBox();
		if( cb.empty() ) break;
		Transform t=tf;
		t.m.i.normalize();t.m.j.normalize();t.m.k.normalize();
		Box b=t*cb;
		b.expand( (b.width()+b.height()+b.depth())*.001f+.001f );
		return b;
	}
	}
	return Box( Vector( -INFINITY,-INFINITY,-INFINITY ),Vector( INFINITY,INFINITY,INFINITY ) );
}

static const vector<Box> &pickBoxes( PickBoxes &pb,const vector<Object*> &objs ){
	if( pb.valid && pb.tform_gen==Entity::tformGen() && pb.geom_gen==Surface::geomGen() &&
		pb.bounds_gen==Object::boundsGen() && pb.list_gen==pick_rebuilds_total ) return pb.boxes;
	++box_rebuilds;
	pb.boxes.resize( objs.size() );
	for( size_t k=0;k<objs.size();++k ) pb.boxes[k]=pickBox( objs[k] );
	//computing the boxes validates world transforms (no generation change), so read the counters afterwards
	pb.tform_gen=Entity::tformGen();pb.geom_gen=Surface::geomGen();
	pb.bounds_gen=Object::boundsGen();pb.list_gen=pick_rebuilds_total;
	pb.valid=true;
	return pb.boxes;
}

bool World::hitTest( const Line &line,float radius,Object *obj,const Transform &tf,int method,Collision *curr_coll  ){
	switch( method ){
	case COLLISION_METHOD_SPHERE:
		return curr_coll->sphereCollide( line,radius,tf.v,obj->getCollisionRadii().x );
	case COLLISION_METHOD_POLYGON:
		return obj->collide( line,radius,curr_coll,tf );
	case COLLISION_METHOD_BOX:
		Transform t=tf;
		t.m.i.normalize();t.m.j.normalize();t.m.k.normalize();
		if( curr_coll->boxCollide( ~t*line,radius,obj->getCollisionBox() ) ){
			curr_coll->normal=t.m*curr_coll->normal;
			return true;
		}
	}
	return false;
}

//Wii U port : EntityVisible counters, logged every 1000 calls : obscurers, objects past the box test (meshes,
//spheres / boxes), tree nodes, exact triangle tests, pick list rebuilds
static unsigned los_stats[6],los_calls;
static uint64_t los_ticks;

static void losLog(){
	los_stats[4]+=(unsigned)stats3d[2];los_stats[5]+=(unsigned)stats3d[3];
	stats3d[2]=stats3d[3]=0;
	if( ++los_calls<1000 ) return;
	float n=los_calls;
	gx_runtime->debugLog( ("EntityVisible per call : "+ftoa( los_stats[0]/n )+" obscurers, "+ftoa( los_stats[1]/n )+
		" past the box test ("+ftoa( los_stats[2]/n )+" meshes, "+ftoa( los_stats[3]/n )+" spheres / boxes), "+
		ftoa( los_stats[4]/n )+" nodes, "+ftoa( los_stats[5]/n )+" exact triangle tests, "+itoa( pick_rebuilds )+
		" list rebuilds, "+itoa( box_rebuilds )+" box rebuilds, "+ftoa( (float)OSTicksToMicroseconds( los_ticks )/n )+" us").c_str() );
	los_ticks=0;
	pick_rebuilds=box_rebuilds=0;
	memset( los_stats,0,sizeof(los_stats) );
	los_calls=0;
}

bool World::checkLOS( Object *src,Object *dest ){

	OSTime t0=OSGetSystemTime();
	enumPickables();

	Collision curr_coll;

	Line line( src->getWorldPosition(),dest->getWorldPosition()-src->getWorldPosition() );

	float s2=stats3d[2],s3=stats3d[3];
	stats3d[2]=stats3d[3]=0;
	los_stats[0]+=_obscurers.size();

	bool visible=true;
	const vector<Box> &boxes=pickBoxes( los_boxes,_obscurers );
	Box lb( line );
	SegTest st( line,.001f );

	for( size_t k=0;k<_obscurers.size();++k ){
		if( !boxes[k].overlaps( lb ) || !st.hits( boxes[k] ) ) continue;
		Object *obj=_obscurers[k];

		if( obj==src || obj==dest ) continue;
		++los_stats[1];
		if( obj->getPickGeometry()==COLLISION_METHOD_POLYGON ) ++los_stats[2];
		else ++los_stats[3];

		if( hitTest( line,0,obj,obj->getWorldTform(),obj->getPickGeometry(),&curr_coll ) ){
			visible=false;
			break;
		}
	}
	los_ticks+=OSGetSystemTime()-t0;
	losLog();
	stats3d[2]=s2;stats3d[3]=s3;
	return visible;
}

Object *World::traceRay( const Line &line,float radius,ObjCollision *curr_coll ){

	enumPickables();

	Object *coll_obj=0;

	const vector<Box> &boxes=pickBoxes( ray_boxes,_pickables );
	Box lb( line );
	lb.expand( radius );
	SegTest st( line,radius+.001f );

	for( size_t k=0;k<_pickables.size();++k ){
		if( !boxes[k].overlaps( lb ) || !st.hits( boxes[k] ) ) continue;
		Object *obj=_pickables[k];

		if( hitTest( line,radius,obj,obj->getWorldTform(),obj->getPickGeometry(),&curr_coll->collision ) ){
			coll_obj=obj;
		}
	}
	if( curr_coll->with=coll_obj ){
		curr_coll->coords=line*curr_coll->collision.time-curr_coll->collision.normal*radius;
	}
	return coll_obj;
}

//Wii U port : collision counters, logged every 600 updates : moving colliders, mesh tests, tests past the box
//rejection, sweep iterations, triangles in the visited leaves (stats3d[0]), tree nodes visited (stats3d[2]),
//triangles given to the exact test (stats3d[3])
static unsigned coll_stats[7],coll_updates;

//
// NEW VERSION
//
void World::collide( Object *src ){

	static const int MAX_HITS=10;

	Vector dv=src->getWorldTform().v;
	Vector sv=src->getPrevWorldTform().v;

	if( sv==dv ){
		if( dv.x!=sv.x || dv.y!=sv.y || dv.z!=sv.z ){
			src->setWorldPosition( sv );
		}
		return;
	}

	Vector panic=sv;
	++coll_stats[0];

	static Transform y_tform;

	const Vector &radii=src->getCollisionRadii();

	float radius=radii.x,inv_y_scale;
	float y_scale=inv_y_scale=y_tform.m.j.y=1;

	if( radii.x!=radii.y ){
		y_scale=y_tform.m.j.y=radius/radii.y;
		inv_y_scale=1/y_scale;
		sv.y*=y_scale;
		dv.y*=y_scale;
	}

	int n_hit=0;
	Plane planes[2];
	Line coll_line( sv,dv-sv );
	Vector dir=coll_line.d;

	float td=coll_line.d.length();
	float td_xz=Vector( coll_line.d.x,0,coll_line.d.z ).length();

	const vector<CollInfo> &collinfos=_collInfo[src->getCollisionType()];

	int hits=0;
	for(;;){

		Collision coll;
		Object *coll_obj=0;
		vector<CollInfo>::const_iterator coll_it,coll_info;

		//Wii U port : the swept sphere's box, to reject meshes by their cached world box before the exact test
		//(which inverts the mesh's transform first)
		Box line_box( coll_line );
		line_box.expand( radius );

		for( coll_it=collinfos.begin();coll_it!=collinfos.end();++coll_it ){

			vector<Object*>::const_iterator dst_it;

			const vector<Object*> &dst_objs=_objsByType[coll_it->dst_type];

			for( dst_it=dst_objs.begin();dst_it!=dst_objs.end();++dst_it ){

				Object *dst=*dst_it;

				if( src==dst ) continue;

				const Transform &dst_tform=dst->getPrevWorldTform();

				++coll_stats[1];
				if( coll_it->method==COLLISION_METHOD_POLYGON ){
					Box wb;
					if( dst->worldCollideBounds( dst_tform,&wb ) ){
						if( wb.empty() ) continue;
						if( y_scale!=1 ){ wb.a.y*=y_scale;wb.b.y*=y_scale; }
						if( !wb.overlaps( line_box ) ) continue;
					}
				}
				++coll_stats[2];
				BB_PROF_SCOPE( "[UpdateWorld: collide, exact tests]" );

				if( y_scale==1 ){
					if( hitTest( 
					coll_line,radius,dst,dst_tform,
					coll_it->method,&coll ) ){
						coll_obj=dst;
						coll_info=coll_it;
					}
				}else{
					if( hitTest( 
					coll_line,radius,dst,y_tform * dst_tform,
					coll_it->method,&coll ) ){
						coll_obj=dst;
						coll_info=coll_it;
					}
				}
			}
		}
		++coll_stats[3];
		if( !coll_obj ) break;

		//register collision
		if( ++hits==MAX_HITS ){
//			exit(0);
			break;
		}

		collided( src,coll_obj,coll_line,coll,inv_y_scale );

		Plane coll_plane( coll_line*coll.time,coll.normal );

		coll_plane.d-=COLLISION_EPSILON;
		coll.time=coll_plane.t_intersect( coll_line );

		if( coll.time>0 ){// && fabs(coll.normal.dot( coll_line.d ))>EPSILON ){
			//update source position - ONLY IF AHEAD!
			sv=coll_line*coll.time;
			td*=1-coll.time;
			td_xz*=1-coll.time;
		}

		if( coll_info->response==COLLISION_RESPONSE_STOP ){
			dv=sv;
			break;
		}

		//find nearest point on plane to dest
		Vector nv=coll_plane.nearest( dv );

		if( n_hit==0 ){
			dv=nv;
		}else if( n_hit==1 ){
			if( planes[0].distance(nv)>=0 ){
				dv=nv;n_hit=0;
			}else if( fabs( planes[0].n.dot( coll_plane.n ) )<1-EPSILON ){
				dv=coll_plane.intersect( planes[0] ).nearest( dv );
			}else{
				//SQUISHED!
				//exit(0);
				hits=MAX_HITS;break;
			}
		}else if( planes[0].distance(nv)>=0 && planes[1].distance(nv)>=0 ){
			dv=nv;n_hit=0;
		}else{
			dv=sv;break;
		}

		Vector dd( dv-sv );

		//going behind initial direction? really necessary?
		if( dd.dot( dir )<=0 ){ dv=sv;break; }

		if( coll_info->response==COLLISION_RESPONSE_SLIDE ){
			float d=dd.length();
			if( d<=EPSILON ){ dv=sv;break; }
			if( d>td ) dd*=td/d;
		}else if( coll_info->response==COLLISION_RESPONSE_SLIDEXZ ){
			float d=Vector( dd.x,0,dd.z ).length();
			if( d<=EPSILON ){ dv=sv;break; }
			if( d>td_xz ) dd*=td_xz/d;
		}

		coll_line.o=sv;
		coll_line.d=dd;dv=sv+dd;
		planes[n_hit++]=coll_plane;
	}

	if( hits ){
		if( hits<MAX_HITS ){
			dv.y*=inv_y_scale;
			src->setWorldPosition( dv );
		}else{
			src->setWorldPosition( panic );
		}
	}
}


void World::update( float elapsed ){

	stats3d[0]=0;

	for( ;used_colls.size();used_colls.pop_back() ){
		free_colls.push_back( used_colls.back() );
	}

	enumEnabled();

	vector<Object*>::const_iterator it;
	for( it=_enabled.begin();it!=_enabled.end();++it ){
		Object *o=*it;

		if( int n=o->getCollisionType() ){
			_objsByType[n].push_back(o);
		}
	}

	for( it=_enabled.begin();it!=_enabled.end();++it ){
		Object *o=*it;

		{
			BB_PROF_SCOPE( "[UpdateWorld: animate]" );
			o->beginUpdate( elapsed );
		}

		if( o->getCollisionType() ){
			BB_PROF_SCOPE( "[UpdateWorld: collide]" );
			collide( o );
		}

		o->endUpdate();
	}

	for( int k=0;k<1000;++k ){
		_objsByType[k].clear();
	}

	coll_stats[4]+=(unsigned)stats3d[0];coll_stats[5]+=(unsigned)stats3d[2];coll_stats[6]+=(unsigned)stats3d[3];
	stats3d[2]=stats3d[3]=0;
	if( ++coll_updates==600 ){
		float n=coll_updates;
		gx_runtime->debugLog( ("collisions per update : "+ftoa( coll_stats[0]/n )+" moving, "+ftoa( coll_stats[1]/n )+
			" mesh tests, "+ftoa( coll_stats[2]/n )+" past the box test, "+ftoa( coll_stats[3]/n )+" sweeps, "+
			ftoa( coll_stats[4]/n )+" triangles, "+ftoa( coll_stats[5]/n )+" nodes, "+ftoa( coll_stats[6]/n )+" exact triangle tests").c_str() );
		memset( coll_stats,0,sizeof(coll_stats) );
		coll_updates=0;
	}
}

/****************************** Render *********************************/

static Transform cam_tform;		//current camera transform

static vector<gxLight*> _lights;
static vector<Mirror*> _mirrors;
static vector<Listener*> _listeners;

struct OrderComp{
	bool operator()( Object *a,Object *b ){
		return a->getOrder()<b->getOrder();
	}
};

struct TransComp{
	bool operator()( Model *a,Model *b )const{
		return 
		cam_tform.v.distance( a->getRenderTform().v )<
		cam_tform.v.distance( b->getRenderTform().v );
	}
};

static vector<Model*> ord_mods,unord_mods;

static priority_queue<Model*,vector<Model*>,OrderComp> ord_que;

static priority_queue<Camera*,vector<Camera*>,OrderComp> cam_que;

static priority_queue<Model*,vector<Model*>,TransComp> transparents;

void World::capture(){

	enumVisible();

	vector<Object*>::const_iterator it;
	for( it=_visible.begin();it!=_visible.end();++it ){
		(*it)->capture();
	}
}

void World::render( float tween ){

	//set render tweens, and build ordered and unordered model lists...
	ord_mods.clear();
	unord_mods.clear();

	_lights.clear();
	_mirrors.clear();
	_listeners.clear();

	{
		BB_PROF_SCOPE( "[RenderWorld: enumVisible]" );
		enumVisible();
	}
	{
	BB_PROF_SCOPE( "[RenderWorld: beginRender]" );
	vector<Object*>::const_iterator it;
	for( it=_visible.begin();it!=_visible.end();++it ){
		Object *o=*it;

		if( !o->beginRender(tween) ) continue;

		if( Light *t=o->getLight() ) _lights.push_back(t->getGxLight());
		else if( Camera *t=o->getCamera() ) cam_que.push(t);
		else if( Mirror *t=o->getMirror() ) _mirrors.push_back(t);
		else if( Listener *t=o->getListener() ) _listeners.push_back(t);
		else if( Model *t=o->getModel() ){
			if( t->getOrder() ) ord_que.push( t );
			else unord_mods.push_back( t );
		}
	}
	}

	for( ;ord_que.size();ord_que.pop() ) ord_mods.push_back( ord_que.top() );

//	gx_runtime->debugLog( "RenderWorld" );

	if( !gx_scene->begin( _lights ) ) return;

	for( ;cam_que.size();cam_que.pop() ){

		Camera *cam=cam_que.top();

		if( !cam->beginRenderFrame() ) continue;

		vector<Mirror*>::const_iterator mir_it;
		for( mir_it=_mirrors.begin();mir_it!=_mirrors.end();++mir_it ){
			render( cam,*mir_it );
		}

		render( cam,0 );
	}

	gx_scene->end();

//	gx_runtime->debugLog( "End RenderWorld" );

	vector<Listener*>::const_iterator lis_it;
	for( lis_it=_listeners.begin();lis_it!=_listeners.end();++lis_it ){
		(*lis_it)->renderListener();
	}
}

void World::render( Camera *cam,Mirror *mirror ){

	if( mirror ){
		const Transform &t=mirror->getRenderTform();
		cam_tform=t * Transform( scaleMatrix( 1,-1,1 ) ) * -t * cam->getRenderTform();
		gx_scene->setFlippedTris( true );
	}else{
		cam_tform=cam->getRenderTform();
		gx_scene->setFlippedTris( false );
	}

	//set camera matrix
	gx_scene->setViewMatrix( (gxScene::Matrix*)&(-cam_tform) );

	//initialize render context
	RenderContext rc( cam_tform,cam->getFrustum(),mirror!=0 );

	//draw everything in order
	int ord=0;
	gx_scene->setZMode( gxScene::ZMODE_DISABLE );
	while( ord<ord_mods.size() && ord_mods[ord]->getOrder()>0 ){
		Model *mod=ord_mods[ord++];
		if( !mod->doAutoFade( cam_tform.v ) ) continue;
		render( mod,rc );
		flushTransparent();
	}

	gx_scene->setZMode( gxScene::ZMODE_NORMAL );
	for( int k=0;k<unord_mods.size();++k ){
		Model *mod=unord_mods[k];
		if( !mod->doAutoFade( cam_tform.v ) ) continue;
		render( mod,rc );
	}
	gx_scene->setZMode( gxScene::ZMODE_CMPONLY );
	flushTransparent();

	gx_scene->setZMode( gxScene::ZMODE_DISABLE );
	while( ord<ord_mods.size() ){
		Model *mod=ord_mods[ord++];
		if( !mod->doAutoFade( cam_tform.v ) ) continue;
		render( mod,rc );
		flushTransparent();
	}
}

void World::render( Model *mod,const RenderContext &rc ){

	bool trans;
	{
		BB_PROF_SCOPE( "[RenderWorld: cull + queue]" );
		trans=mod->render( rc );
	}

	BB_PROF_SCOPE( "[RenderWorld: draw queues]" );
	if( mod->queueSize( Model::QUEUE_OPAQUE ) ){
		if( mod->getRenderSpace()==Model::RENDER_SPACE_LOCAL ){
			gx_scene->setWorldMatrix( (gxScene::Matrix*)&mod->getRenderTform() );
		}else{
			gx_scene->setWorldMatrix( 0 );
		}
		mod->renderQueue( Model::QUEUE_OPAQUE );
	}

	if( trans || mod->queueSize( Model::QUEUE_TRANSPARENT ) ){
		transparents.push( mod );
	}
}

void World::flushTransparent(){
	BB_PROF_SCOPE( "[RenderWorld: transparents]" );

	bool local=true;

	for( ;transparents.size();transparents.pop() ){
		Model *mod=transparents.top();
		if( mod->getRenderSpace()==Model::RENDER_SPACE_LOCAL ){
			gx_scene->setWorldMatrix( (gxScene::Matrix*)&mod->getRenderTform() );
			local=true;
		}else if( local ){
			gx_scene->setWorldMatrix( 0 );
			local=false;
		}
		mod->renderQueue( Model::QUEUE_TRANSPARENT );
	}
}
