// bb2cpp : BlitzBasic (Blitz3D) -> C++ transpiler for the SCP:CB Wii U port.
//
//   bb2cpp <main.bb> <out_dir> <bbapi.txt> [userlib.decls ...]
//
// The source directory of <main.bb> becomes the current directory (Include paths are relative to it).
// Front end = the original Blitz3D compiler (tokenizer, parser, semantic pass); back end = cppgen.cpp.
// The runtime command list comes from tools/bbapi.txt (tools/gen_bbapi.py), the userlib functions from the
// .decls files, the same way the original compiler loaded them from the runtime DLL and userlibs folder
// (blitz/libs.cpp).
#include "std.h"
#include "toker.h"
#include "parser.h"
#include "cppgen.h"

#include <filesystem>
#include <sstream>

static Environ *runtimeEnviron;
static map<FuncType*,RuntimeFunc> rtFuncs;

static Type *typeOf( int c ){
	switch( c ){
	case '%':return Type::int_type;
	case '#':return Type::float_type;
	case '$':return Type::string_type;
	}
	return Type::void_type;
}

// one rtSym signature, parsed like linkRuntime() in blitz/libs.cpp
static void addRuntimeFunc( string s,const RuntimeFunc &rf ){
	bool cfunc=false;
	if( s[0]=='!' ){ cfunc=true;s=s.substr( 1 ); }
	int start=0,k;
	Type *t=Type::void_type;
	if( !isalpha( (unsigned char)s[0] ) ){ start=1;t=typeOf( s[0] ); }
	for( k=1;k<(int)s.size();++k ){
		if( !isalnum( (unsigned char)s[k] ) && s[k]!='_' ) break;
	}
	DeclSeq *params=d_new DeclSeq();
	string n=s.substr( start,k-start );
	while( k<(int)s.size() ){
		Type *pt=typeOf( s[k++] );
		int from=k;
		for( ;k<(int)s.size() && (isalnum( (unsigned char)s[k] ) || s[k]=='_');++k ){}
		string str=s.substr( from,k-from );
		ConstType *defType=0;
		if( k<(int)s.size() && s[k]=='=' ){
			int from=++k;
			if( s[k]=='\"' ){
				for( ++k;s[k]!='\"';++k ){}
				string v=s.substr( from+1,k-from-1 );
				defType=d_new ConstType( v );++k;
			}else{
				if( s[k]=='-' ) ++k;
				for( ;k<(int)s.size() && (isdigit( (unsigned char)s[k] ) || s[k]=='.');++k ){}
				if( pt==Type::int_type ){
					defType=d_new ConstType( atoi( s.substr( from,k-from ) ) );
				}else{
					defType=d_new ConstType( (float)atof( s.substr( from,k-from ) ) );
				}
			}
		}
		params->insertDecl( str,pt,DECL_PARAM,defType );
	}
	FuncType *f=d_new FuncType( t,params,false,cfunc );
	runtimeEnviron->funcDecls->insertDecl( tolower( n ),f,DECL_FUNC );
	rtFuncs[f]=rf;
}

static bool loadApi( const string &path ){
	ifstream in( path.c_str() );
	if( !in.good() ) return false;
	string line;
	while( getline( in,line ) ){
		if( line.empty() ) continue;
		vector<string> cols;
		stringstream ss( line );
		string c;
		while( getline( ss,c,'\t' ) ) cols.push_back( c );
		while( cols.size()<4 ) cols.push_back( "" );
		RuntimeFunc rf;
		rf.cpp=cols[1];
		rf.ret=cols[2];
		stringstream ps( cols[3] );
		while( getline( ps,c,';' ) ) if( c.size() ) rf.params.push_back( c );
		addRuntimeFunc( cols[0],rf );
	}
	return true;
}

// .decls parser, as loadUserLib() in blitz/libs.cpp
static int curr;
static string text;

static int next( istream &in ){
	text="";
	int t=0;
	for(;;){
		while( isspace( in.peek() ) ) in.get();
		if( in.eof() ) return curr=0;
		t=in.get();if( t!=';' ) break;
		while( !in.eof() && in.get()!='\n' ){}
	}
	if( isalpha( t ) ){
		text+=(char)t;
		while( isalnum( in.peek() ) || in.peek()=='_' ) text+=(char)in.get();
		return curr=-1;
	}
	if( t=='\"' ){
		while( in.peek()!='\"' && !in.eof() ) text=text+(char)in.get();
		in.get();
		return curr=-2;
	}
	return curr=t;
}

static set<string> ulibkws;

static const char *loadUserLib( const string &file ){
	ifstream in( file.c_str() );
	if( !in.good() ) return "can't open";
	string lib;
	next( in );
	while( curr ){
		if( curr=='.' ){
			if( next( in )!=-1 ) return "expecting identifier after '.'";
			if( text=="lib" ){
				if( next( in )!=-2 ) return "expecting string after lib directive";
				lib=text;
			}else{
				return "unknown decl directive";
			}
			next( in );
		}else if( curr==-1 ){
			if( !lib.size() ) return "function decl without lib directive";
			string id=text;
			string lower_id=tolower( id );
			if( ulibkws.count( lower_id ) ) return "duplicate identifier";
			ulibkws.insert( lower_id );
			Type *ty=0;
			switch( next( in ) ){
			case '%':ty=Type::int_type;break;
			case '#':ty=Type::float_type;break;
			case '$':ty=Type::string_type;break;
			}
			if( ty ) next( in );
			else ty=Type::void_type;
			DeclSeq *params=d_new DeclSeq();
			if( curr!='(' ) return "expecting '(' after function identifier";
			next( in );
			if( curr!=')' ){
				for(;;){
					if( curr!=-1 ) break;
					string arg=text;
					Type *pt=0;
					switch( next( in ) ){
					case '%':pt=Type::int_type;break;
					case '#':pt=Type::float_type;break;
					case '$':pt=Type::string_type;break;
					case '*':pt=Type::null_type;break;
					}
					if( pt ) next( in );
					else pt=Type::int_type;
					params->insertDecl( arg,pt,DECL_PARAM,0 );
					if( curr!=',' ) break;
					next( in );
				}
			}
			if( curr!=')' ) return "expecting ')' after function decl";
			FuncType *fn=d_new FuncType( ty,params,true,true );
			runtimeEnviron->funcDecls->insertDecl( lower_id,fn,DECL_FUNC );
			if( next( in )==':' ){
				next( in );
				if( curr!=-1 && curr!=-2 ) return "expecting identifier or string after alias";
				next( in );
			}
		}else{
			return "syntax error";
		}
	}
	return 0;
}

static void showError( const Ex &x ){
	cerr<<x.file;
	if( x.pos>=0 ) cerr<<":"<<((x.pos>>16)+1)<<":"<<((x.pos&0xffff)+1);
	cerr<<": error: "<<x.ex<<endl;
}

int main( int argc,char **argv ){
	if( argc<4 ){
		cerr<<"usage: bb2cpp <main.bb> <out_dir> <bbapi.txt> [userlib.decls ...]"<<endl;
		return 1;
	}
	namespace fs=std::filesystem;
	string main_file=fs::absolute( argv[1] ).string();
	string out_dir=fs::absolute( argv[2] ).string();
	string api=fs::absolute( argv[3] ).string();
	vector<string> decls;
	for( int k=4;k<argc;++k ) decls.push_back( fs::absolute( argv[k] ).string() );

	fs::create_directories( out_dir );

	runtimeEnviron=d_new Environ( "",Type::int_type,0,0 );
	if( !loadApi( api ) ){
		cerr<<"can't read "<<api<<endl;
		return 1;
	}
	for( size_t k=0;k<decls.size();++k ){
		if( const char *e=loadUserLib( decls[k] ) ){
			cerr<<decls[k]<<": "<<e<<endl;
			return 1;
		}
	}

	//includes are relative to the main file's folder
	fs::current_path( fs::path( main_file ).parent_path() );

	ifstream in( main_file.c_str() );
	if( !in.good() ){
		cerr<<"can't open "<<main_file<<endl;
		return 1;
	}

	try{
		Toker toker( in );
		Parser parser( toker );
		ProgNode *prog=parser.parse( tolower( main_file ) );
		prog->semant( runtimeEnviron );
		CppGen gen( runtimeEnviron,rtFuncs );
		gen.generate( prog,out_dir,40 );
		cout<<"bb2cpp: "<<prog->funcs->size()<<" functions, "<<gen.used_userlibs.size()<<" userlib functions used"<<endl;
	}catch( Ex &x ){
		showError( x );
		return 1;
	}
	return 0;
}
