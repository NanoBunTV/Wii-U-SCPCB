// Wii U port : DirectX .x model loader.
//
// The original used DirectX's IDirectXFile API, which doesn't exist here. This file parses text .x files
// ("xof 0302txt" / "xof 0303txt") into the same kind of object tree (template name, instance name, data
// values, child objects and references) and keeps the original loader's logic : same frames, meshes,
// material lists, texture coordinates, vertex colours, normals, transforms and animation keys.
// Binary / compressed .x files aren't supported (SCP:CB has one, converted to text by the asset pipeline).
#include "std.h"
#include "loader_x.h"
#include "meshmodel.h"
#include "animation.h"
#include "pivot.h"

#include <cstdio>
#include <cstdlib>

extern gxRuntime *gx_runtime;

namespace{

// ---- .x text object tree ----

struct XObject{
	string type,name;			// template and instance name ("" if unnamed)
	vector<string> data;		// numbers and strings, in order
	vector<XObject*> children;
	vector<string> refs;		// references to named objects ({ Name })
	size_t cur;					// read cursor into data
	XObject():cur(0){}
	~XObject(){ for( size_t k=0;k<children.size();++k ) delete children[k]; }

	int readInt(){ return cur<data.size() ? atoi( data[cur++].c_str() ) : 0; }
	float readFloat(){ return cur<data.size() ? (float)atof( data[cur++].c_str() ) : 0; }
	string readString(){ return cur<data.size() ? data[cur++] : ""; }
};

class XParser{
public:
	XParser( const string &t ):text(t),pos(0){}

	// top level objects
	bool parse( vector<XObject*> &objs ){
		if( text.compare( 0,4,"xof " ) ) return false;
		if( text.compare( 8,3,"txt" ) ) return false;	//binary / compressed
		pos=16;
		for(;;){
			string t=token();
			if( t.empty() ) break;
			if( t=="template" ){ skipTemplate();continue; }
			if( isIdent( t ) ){
				XObject *o=object( t );
				if( !o ) return false;
				objs.push_back( o );
			}
		}
		return true;
	}

private:
	const string &text;
	size_t pos;
	string peeked;
	bool has_peek=false;

	static bool isIdent( const string &t ){
		if( t.empty() ) return false;
		unsigned char c=t[0];
		return isalpha( c ) || c=='_';
	}

	string rawToken(){
		for(;;){
			while( pos<text.size() && isspace( (unsigned char)text[pos] ) ) ++pos;
			if( pos>=text.size() ) return "";
			if( text[pos]=='#' || (text[pos]=='/' && pos+1<text.size() && text[pos+1]=='/') ){
				while( pos<text.size() && text[pos]!='\n' ) ++pos;
				continue;
			}
			break;
		}
		char c=text[pos];
		if( c=='{' || c=='}' || c==';' || c==',' ){ ++pos;return string( 1,c ); }
		if( c=='"' ){
			size_t e=text.find( '"',pos+1 );
			if( e==string::npos ) e=text.size();
			string s="\""+text.substr( pos+1,e-pos-1 );		//strings keep a leading quote as a marker
			pos=e+1;
			return s;
		}
		if( c=='<' ){
			size_t e=text.find( '>',pos );
			if( e==string::npos ) e=text.size();
			string s=text.substr( pos,e-pos+1 );
			pos=e+1;
			return s;
		}
		size_t s=pos;
		while( pos<text.size() ){
			c=text[pos];
			if( isspace( (unsigned char)c ) || c=='{' || c=='}' || c==';' || c==',' || c=='"' ) break;
			++pos;
		}
		return text.substr( s,pos-s );
	}

	string token(){
		if( has_peek ){ has_peek=false;return peeked; }
		return rawToken();
	}

	string peek(){
		if( !has_peek ){ peeked=rawToken();has_peek=true; }
		return peeked;
	}

	void skipTemplate(){
		int depth=0;
		for(;;){
			string t=token();
			if( t.empty() ) return;
			if( t=="{" ) ++depth;
			else if( t=="}" ){ if( --depth<=0 ) return; }
		}
	}

	// <type> [name] { [<guid>] data... children... }
	XObject *object( const string &type ){
		XObject *o=d_new XObject;
		o->type=type;
		string t=token();
		if( t!="{" ){
			o->name=t;
			t=token();
		}
		if( t!="{" ){ delete o;return 0; }
		for(;;){
			t=token();
			if( t.empty() || t=="}" ) break;
			if( t==";" || t=="," ) continue;
			if( t[0]=='<' ) continue;							//GUID
			if( t=="{" ){
				//reference : { Name [GUID] }
				string name;
				for(;;){
					string r=token();
					if( r.empty() || r=="}" ) break;
					if( r[0]!='<' && name.empty() ) name=r;
				}
				if( name.size() ) o->refs.push_back( name );
				continue;
			}
			if( t[0]=='"' ){ o->data.push_back( t.substr( 1 ) );continue; }
			if( isIdent( t ) ){
				string n=peek();
				if( n=="{" || (n!=";" && n!="," && n!="}" && isIdent( n ) ) ){
					XObject *c=object( t );
					if( !c ){ delete o;return 0; }
					o->children.push_back( c );
					continue;
				}
			}
			o->data.push_back( t );
		}
		return o;
	}
};

// ---- loader state (as the original) ----

map<string,MeshModel*> frames_map;
map<string,XObject*> named_objs;
int anim_len;

bool conv,flip_tris;
Transform conv_tform;
bool collapse,animonly;

void collectNamed( XObject *o ){
	if( o->name.size() ) named_objs[o->name]=o;
	for( size_t k=0;k<o->children.size();++k ) collectNamed( o->children[k] );
}

void parseAnimKey( XObject *x,MeshModel *e ){
	x->cur=0;
	int type=x->readInt();
	int cnt=x->readInt();
	Animation anim=e->getAnimation();
	for( int k=0;k<cnt;++k ){
		int time=x->readInt();
		int n=x->readInt();
		if( time>anim_len ) anim_len=time;
		float v[16];
		for( int j=0;j<n;++j ){
			float f=x->readFloat();
			if( j<16 ) v[j]=f;
		}
		switch( type ){
		case 0:
			if( n==4 ){
				Quat rot( v[0],Vector( v[1],v[2],v[3] ) );
				if( conv ){
					if( fabs(rot.w)<1-EPSILON ){
						rot.normalize();
						//quat-to-axis/angle
						float half=acosf( rot.w );
						if( flip_tris ) half=-half;
						rot=Quat( cosf( half ),(conv_tform.m*rot.v).normalized()*sinf( half ) );
					}else rot=Quat();
				}
				anim.setRotationKey( time,rot );
			}
			break;
		case 1:
			if( n==3 ){
				Vector scl( v[0],v[1],v[2] );
				if( conv ) scl=conv_tform.m * scl;
				scl.x=fabs(scl.x);scl.y=fabs(scl.y);scl.z=fabs(scl.z);
				anim.setScaleKey( time,scl );
			}
			break;
		case 2:
			if( n==3 ){
				Vector pos( v[0],v[1],v[2] );
				if( conv ) pos=conv_tform*pos;
				anim.setPositionKey( time,pos );
			}
			break;
		}
	}
	e->setAnimation( anim );
}

void parseAnim( XObject *x ){
	MeshModel *frame=0;
	//the frame reference (or a frame named inline)
	for( size_t k=0;k<x->refs.size() && !frame;++k ){
		map<string,MeshModel*>::iterator it=frames_map.find( x->refs[k] );
		if( it!=frames_map.end() ) frame=it->second;
	}
	if( !frame ) return;
	for( size_t k=0;k<x->children.size();++k ){
		if( x->children[k]->type=="AnimationKey" ) parseAnimKey( x->children[k],frame );
	}
}

void parseAnimSet( XObject *x ){
	for( size_t k=0;k<x->children.size();++k ){
		if( x->children[k]->type=="Animation" ) parseAnim( x->children[k] );
	}
}

Brush parseMaterial( XObject *x ){
	Brush brush;
	x->cur=0;
	float r=x->readFloat(),g=x->readFloat(),b=x->readFloat(),a=x->readFloat();
	brush.setColor( Vector( r,g,b ) );
	if( a ) brush.setAlpha( a );

	for( size_t k=0;k<x->children.size();++k ){
		XObject *c=x->children[k];
		if( c->type=="TextureFilename" || c->type=="TextureFileName" ){
			c->cur=0;
			string name=c->readString();
			//.x strings may contain escaped backslashes
			string t;
			for( size_t j=0;j<name.size();++j ){
				if( name[j]=='\\' && j+1<name.size() && name[j+1]=='\\' ) ++j;
				t+=name[j];
			}
			brush.setTexture( 0,Texture( t,0 ),0 );
			brush.setColor( Vector( 1,1,1 ) );
		}
	}
	return brush;
}

void parseMaterialList( XObject *x,vector<Brush> &mats ){
	for( size_t k=0;k<x->children.size();++k ){
		if( x->children[k]->type=="Material" ) mats.push_back( parseMaterial( x->children[k] ) );
	}
	for( size_t k=0;k<x->refs.size();++k ){
		map<string,XObject*>::iterator it=named_objs.find( x->refs[k] );
		if( it!=named_objs.end() && it->second->type=="Material" ) mats.push_back( parseMaterial( it->second ) );
	}
}

struct FaceX{
	vector<int> verts;
	int mat_index;
	FaceX():mat_index(0){}
};

void parseMesh( XObject *x,MeshModel *mesh ){
	vector<FaceX> faces;
	vector<Brush> mats;

	MeshLoader::beginMesh();

	x->cur=0;
	//setup vertices
	int num_verts=x->readInt();
	int k;
	for( k=0;k<num_verts;++k ){
		Surface::Vertex v;
		v.coords.x=x->readFloat();v.coords.y=x->readFloat();v.coords.z=x->readFloat();
		if( conv ) v.coords=conv_tform * v.coords;
		v.color=0xffffffff;
		MeshLoader::addVertex( v );
	}

	//setup faces
	int num_faces=x->readInt();
	for( k=0;k<num_faces;++k ){
		FaceX f;
		int n=x->readInt();
		for( int j=0;j<n;++j ) f.verts.push_back( x->readInt() );
		faces.push_back( f );
	}

	bool normals=false;

	//get material and texture info
	for( size_t c=0;c<x->children.size();++c ){
		XObject *ch=x->children[c];
		ch->cur=0;
		if( ch->type=="MeshMaterialList" ){
			ch->readInt();	//materials
			int nf=ch->readInt();
			for( int j=0;j<nf;++j ){
				int m=ch->readInt();
				if( j<(int)faces.size() ) faces[j].mat_index=m;
			}
			//some exporters write fewer indices than faces : the last one repeats
			for( int j=nf;j<(int)faces.size() && nf>0;++j ) faces[j].mat_index=faces[nf-1].mat_index;
			parseMaterialList( ch,mats );
		}else if( ch->type=="MeshTextureCoords" ){
			int num_coords=ch->readInt();
			if( num_coords==num_verts ){
				for( int j=0;j<num_coords;++j ){
					Surface::Vertex &v=MeshLoader::refVertex( j );
					float tu=ch->readFloat(),tv=ch->readFloat();
					v.tex_coords[0][0]=v.tex_coords[1][0]=tu;
					v.tex_coords[0][1]=v.tex_coords[1][1]=tv;
				}
			}
		}else if( ch->type=="MeshVertexColors" ){
			int num_colors=ch->readInt();
			if( num_colors==num_verts ){
				for( int j=0;j<num_colors;++j ){
					int idx=ch->readInt();
					float r=ch->readFloat(),g=ch->readFloat(),b=ch->readFloat();
					ch->readFloat();
					if( idx<0 || idx>=num_verts ) continue;
					Surface::Vertex &v=MeshLoader::refVertex( idx );
					v.color=0xff000000|(int(r*255)<<16)|(int(g*255)<<8)|int(b*255);
				}
			}
		}else if( ch->type=="MeshNormals" ){
			int num_normals=ch->readInt();
			if( num_normals==num_verts ){
				Matrix co=conv_tform.m.cofactor();
				for( int j=0;j<num_normals;++j ){
					Surface::Vertex &v=MeshLoader::refVertex( j );
					Vector n;
					n.x=ch->readFloat();n.y=ch->readFloat();n.z=ch->readFloat();
					v.normal=(co * n).normalized();
				}
				normals=true;
			}
		}
	}
	if( !mats.size() ) mats.push_back( Brush() );

	for( k=0;k<(int)faces.size();++k ){
		const FaceX &f=faces[k];
		int cnt=f.verts.size();if( cnt<3 ) continue;
		int mi=f.mat_index>=0 && f.mat_index<(int)mats.size() ? f.mat_index : 0;
		int tri[3];
		tri[0]=f.verts[0];
		for( int j=2;j<cnt;++j ){
			tri[1]=f.verts[j-1+flip_tris];
			tri[2]=f.verts[j-flip_tris];
			MeshLoader::addTriangle( tri,mats[mi] );
		}
	}

	MeshLoader::endMesh( mesh );

	if( !normals ) mesh->updateNormals();
}

MeshModel *parseFrame( XObject *x ){
	MeshModel *e=d_new MeshModel();
	e->setName( x->name );
	frames_map[x->name]=e;

	for( size_t k=0;k<x->children.size();++k ){
		XObject *c=x->children[k];
		if( c->type=="FrameTransformMatrix" ){
			c->cur=0;
			float m[16];
			for( int j=0;j<16;++j ) m[j]=c->readFloat();
			Transform tform=Transform( Matrix(
				Vector( m[0],m[1],m[2] ),
				Vector( m[4],m[5],m[6] ),
				Vector( m[8],m[9],m[10] ) ),
				Vector( m[12],m[13],m[14] ) );
			if( conv ) tform=conv_tform * tform * -conv_tform;
			e->setLocalTform( tform );
		}else if( c->type=="Mesh" ){
			if( !animonly ) parseMesh( c,e );
		}else if( c->type=="Frame" ){
			MeshModel *t=parseFrame( c );
			t->setParent( e );
		}
	}
	return e;
}

MeshModel *parseFile( const string &file ){
	FILE *f=fopen( gxFileSystem::resolvePath( file ).c_str(),"rb" );
	if( !f ) return 0;
	fseek( f,0,SEEK_END );
	long size=ftell( f );
	fseek( f,0,SEEK_SET );
	string text( size>0 ? size : 0,' ' );
	if( size>0 && fread( &text[0],1,size,f )!=(size_t)size ) text.clear();
	fclose( f );

	vector<XObject*> objs;
	XParser parser( text );
	if( !parser.parse( objs ) ){
		gx_runtime->debugLog( ("LoadMesh: unsupported .x file (binary or bad syntax) "+file).c_str() );
		for( size_t k=0;k<objs.size();++k ) delete objs[k];
		return 0;
	}
	for( size_t k=0;k<objs.size();++k ) collectNamed( objs[k] );

	anim_len=0;
	MeshModel *e=d_new MeshModel();
	for( size_t k=0;k<objs.size();++k ){
		XObject *o=objs[k];
		if( o->type=="Mesh" ){
			if( !animonly ) parseMesh( o,e );
		}else if( o->type=="Frame" ){
			MeshModel *t=parseFrame( o );
			t->setParent( e );
		}else if( o->type=="AnimationSet" ){
			if( !collapse ) parseAnimSet( o );
		}
	}

	if( !collapse ){
		e->setAnimator( d_new Animator( e,anim_len ) );
	}

	for( size_t k=0;k<objs.size();++k ) delete objs[k];
	named_objs.clear();
	return e;
}

}

MeshModel *Loader_X::load( const string &filename,const Transform &t,int hint ){
	conv_tform=t;
	conv=flip_tris=false;
	if( conv_tform!=Transform() ){
		conv=true;
		if( conv_tform.m.i.cross(conv_tform.m.j).dot(conv_tform.m.k)<0 ) flip_tris=true;
	}
	collapse=!!(hint&MeshLoader::HINT_COLLAPSE);
	animonly=!!(hint&MeshLoader::HINT_ANIMONLY);

	MeshModel *e=parseFile( filename );
	frames_map.clear();
	return e;
}
