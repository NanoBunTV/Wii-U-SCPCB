
#include "std.h"
#include "bbsys.h"
#include "userlibs.h"

// Wii U port : there are no DLLs. The functions SCP:CB imported from DLLs through .decls files (FMOD, zlib,
// Win32...) are implemented natively and called directly by the transpiled code, so _bbLoadLibs does nothing.

struct Str{
	char *p;
	int size;
};

static Str _strs[256];
static int _nextStr;

void _bbLoadLibs( char *p ){
}

const char*  _bbStrToCStr( BBStr *str ){

	Str &t=_strs[_nextStr++ & 255];

	int size=str->size();

	if( !t.p || t.size<size ){
		delete[] t.p;
		t.p=new char[size+1];
		t.size=size;
	}

	memcpy( t.p,str->data(),size );
	t.p[size]=0;
	delete str;
	return t.p;
}

BBStr*		 _bbCStrToStr( const char *str ){
	return new BBStr( str );
}

bool userlibs_create(){
	return true;
}

void userlibs_destroy(){
}

void userlibs_link( void(*rtSym)(const char*,void*) ){
	rtSym( "_bbLoadLibs",_bbLoadLibs );
	rtSym( "_bbStrToCStr",_bbStrToCStr );
	rtSym( "_bbCStrToStr",_bbCStrToStr );
}


