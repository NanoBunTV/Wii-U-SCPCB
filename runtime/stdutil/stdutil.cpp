
#include "stdutil.h"

#include <set>
#include <math.h>
#include <stdlib.h>
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace std;

#ifdef MEMDEBUG

struct Mem{
	Mem *next,*prev;
	const char *file;
	int line,size,tag;
};

static bool track;

static Mem head,tail;
static Mem x_head,x_tail;

static void remove( Mem *m ){
	m->next->prev=m->prev;
	m->prev->next=m->next;
}

static void insert( Mem *m,Mem *next ){
	m->next=next;
	m->prev=next->prev;
	next->prev->next=m;
	next->prev=m;
}

static void init(){
	if( head.next ) return;
	head.next=head.prev=&tail;head.tag='HEAD';
	tail.next=tail.prev=&head;tail.tag='TAIL';
	x_head.next=x_head.prev=&x_tail;x_head.tag='HEAD';
	x_tail.next=x_tail.prev=&x_head;x_tail.tag='TAIL';
}

static void check( Mem *m ){
	if( m->tag!='DNEW' ){
		MessageBox( GetDesktopWindow(),"mem_check: pre_tag!='DNEW'","Memory error",MB_OK|MB_ICONWARNING );
		if( m->tag=='NDWE' ){
			string t="Probable double delete";
			t+="- d_new file: "+string(m->file)+" line:"+itoa(m->line);
			MessageBox( GetDesktopWindow(),t.c_str(),"Memory error",MB_OK|MB_ICONWARNING );
		}
		ExitProcess( 0 );
	}
	int *t=(int*)( (char*)(m+1)+m->size );
	if( *t!='dnew' ){
		MessageBox( GetDesktopWindow(),"mem_check: post_tag!='dnew'","Memory error",MB_OK|MB_ICONWARNING );
		string t="Probable memory overwrite - d_new file: "+string(m->file)+" line:"+itoa(m->line);
		MessageBox( GetDesktopWindow(),t.c_str(),"Memory error",MB_OK|MB_ICONWARNING );
		ExitProcess( 0 );
	}
}

static void *op_new( size_t size,const char *file="<unknown>",int line=0 ){
	init();
	Mem *m=(Mem*)malloc( sizeof(Mem)+size+sizeof(int) );
	memset( m+1,0xcc,size );
	m->file=file;m->line=line;m->size=size;m->tag='DNEW';
	int *t=(int*)( (char*)(m+1)+size );*t='dnew';
	if( track ) insert( m,head.next );
	else insert( m,x_head.next );
	return m+1;
}

static void op_delete( void *q ){
	init();
	if( !q ) return;
	Mem *m=(Mem*)q-1;
	check( m );
	remove( m );
	m->tag='NDWE';
	*(int*)( (char*)(m+1)+m->size )='ndwe';
	free( m );
}

void trackmem( bool enable ){
	init();
	if( track==enable ) return;
	track=enable;
	Mem *m;
	while( (m=head.next)!=&tail ){
		remove( m );insert( m,x_head.next );
	}
}

void checkmem( ostream &out ){
	init();
	Mem *m,*next;
	int sum=0,usum=0,xsum=0;
	for( m=head.next;m!=&tail;m=next ){
		check( m );
		next=m->next;
		if( m->line ){
			out<<m->file<<" line:"<<m->line<<" "<<m->size<<" bytes"<<endl;
			sum+=m->size;
		}else{
			usum+=m->size;
		}
	}
	for( m=x_head.next;m!=&x_tail;m=m->next ){
		check( m );
		xsum+=m->size;
	}
	out<<"Tracked blitz mem in use:"<<sum<<endl;
	out<<"Tracked other mem in use:"<<usum<<endl;
	out<<"Untracked mem in use:"<<xsum<<endl;
	out<<"Total mem in use:"<<(sum+usum+xsum)<<endl;
}

void * _cdecl operator new( size_t size ){ return op_new( size ); }
void * _cdecl operator new[]( size_t size ){ return op_new( size ); }
void * _cdecl operator new( size_t size,const char *file,int line ){ return op_new( size,file,line ); }
void * _cdecl operator new[]( size_t size,const char *file,int line ){ return op_new( size,file,line ); }
void _cdecl operator delete( void *q ){ op_delete( q ); }
void _cdecl operator delete[]( void *q ){ op_delete( q ); }
void _cdecl operator delete( void *q,const char *file,int line ){ op_delete( q ); }
void _cdecl operator delete[]( void *q,const char *file,int line ){ op_delete( q ); }

#else

void trackmem( bool enable ){
}

void checkmem( ostream &out ){
}

#endif

int atoi( const string &s ){
	return atoi( s.c_str() );
}

double atof( const string &s ){
	return atof( s.c_str() );
}

string itoa( int n ){
	char buff[32];snprintf( buff,sizeof(buff),"%d",n );
	return string( buff );
}

// Wii U port : portable replacements for the MSVC CRT functions ftoa() relies on

static int bb_finite( double n ){
	return std::isfinite( n );
}

static int bb_isnan( double n ){
	return std::isnan( n );
}

// MSVC _ecvt : `digits` significant digits (rounded), decimal point position and sign
static const char *bb_ecvt( double n,int digits,int *dec,int *sign ){
	static char out[64];
	*sign=n<0;
	n=fabs( n );
	if( n==0 ){
		memset( out,'0',digits );out[digits]=0;
		*dec=0;
		return out;
	}
	char buf[64];
	snprintf( buf,sizeof(buf),"%.*e",digits-1,n );
	int k=0;
	for( const char *p=buf;*p && *p!='e';++p ) if( *p>='0' && *p<='9' ) out[k++]=*p;
	out[k]=0;
	const char *e=strchr( buf,'e' );
	*dec=(e ? atoi( e+1 ) : 0)+1;
	return out;
}

// MSVC _gcvt : like %g, with MSVC's 3 digit exponent ("1e+010")
static char *bb_gcvt( double n,int digits,char *buffer ){
	char buf[64];
	snprintf( buf,sizeof(buf),"%.*g",digits,n );
	char *e=strchr( buf,'e' );
	if( e ){
		int x=atoi( e+1 );
		*e=0;
		//MSVC keeps a trailing '.' on the mantissa when it has no fraction ("1.e+010") : %g doesn't
		sprintf( buffer,"%se%c%03d",buf,x<0 ? '-' : '+',x<0 ? -x : x );
	}else{
		strcpy( buffer,buf );
	}
	return buffer;
}

/////////////
//By FLOYD!//
/////////////
string ftoa( float n ){

	static const int digits=6;

	int eNeg = -4, ePos = 8;	// limits for e notation.

	char buffer[50]; // from MSDN example, 25 would probably suffice
	string t;
	int dec, sign;

	if ( bb_finite( n ) ){

//		if ( digits < 1 ) digits = 1;	// less than one digit is nonsense
//		if ( digits > 8 ) digits = 8;	// practical maximum for float
		
		t = bb_ecvt( n, digits, &dec, &sign );

		if ( dec <= eNeg + 1 || dec > ePos ){

			bb_gcvt( n, digits, buffer );
			t = buffer;
			return t;
		}
		
		// Here is the tricky case. We want a nicely formatted
		// number with no e-notation or multiple trailing zeroes.
	
		if ( dec <= 0 ){

			t = "0." + string( -dec, '0' ) + t;
			dec = 1;	// new location for decimal point

		}
		else if( dec < digits ){

			t = t.substr( 0, dec ) + "." + t.substr( dec );

		}
		else{

			t = t + string( dec - digits, '0' ) + ".0";
			dec += dec - digits;

		}
	
		// Finally, trim off excess zeroes.

		int dp1 = dec + 1, p = t.length();	
		while( --p > dp1 && t[p] == '0' );
		t = string( t, 0, ++p );

		return sign ? "-" + t : t;

	}	// end of finite case

	if ( bb_isnan( n ) )	return "NaN";
	if ( n > 0.0 )		return "Infinity";
	if ( n < 0.0 )		return "-Infinity";

	abort();
}

/*
string ftoa( float n ){

	static const float min=.000001f,max=9999999.0f;

	int i=*(int*)&n;
	int e=(i>>23)&0xff;
	int f=i&0x007fffff;

	if( e==0xff && f ) return "NAN";

	string t;
	int s=(i>>31)&0x01;

	if( e==0xff ){
		t="INFINITY";
	}else if( !e && !f ){
		t="0.000000";
	}else if( n>=min && n<=max ){
		int dec,sgn;
		t=_fcvt( fabs(n),6,&dec,&sgn );
		if( dec<=0 ){
			t="0."+string( -dec,'0' )+t;
		}else if( dec<t.size() ){
			t=t.substr( 0,dec )+"."+t.substr( dec );
		}else{
			t=t+string( '0',dec-t.size() )+".000000";
		}
	}else{
		char buff[32];
		bb_gcvt( fabs(n),7,buff );
		t=buff;
	}
	return s ? "-"+t : t;
}
*/

string tolower( const string &s ){
	string t=s;
	for( int k=0;k<t.size();++k ) t[k]=tolower(t[k]);
	return t;
}

string toupper( const string &s ){
	string t=s;
	for( int k=0;k<t.size();++k ) t[k]=toupper(t[k]);
	return t;
}

// Wii U port : no current-directory expansion; paths are split on '/' or backslash
static size_t lastSep( const string &t ){
	size_t a=t.find_last_of( '/' ),b=t.find_last_of( (char)92 );
	if( a==string::npos ) return b;
	if( b==string::npos ) return a;
	return a>b ? a : b;
}

string fullfilename( const string &t ){
	return t;
}

string filenamepath( const string &t ){
	size_t k=lastSep( t );
	return k==string::npos ? "" : t.substr( 0,k+1 );
}

string filenamefile( const string &t ){
	size_t k=lastSep( t );
	return k==string::npos ? t : t.substr( k+1 );
}

const int MIN_SIZE=256;

qstreambuf::qstreambuf(){
	buf=d_new char[MIN_SIZE];
	setg( buf,buf,buf );
	setp( buf,buf+MIN_SIZE );
}

qstreambuf::~qstreambuf(){
	delete[] buf;
}

int qstreambuf::size(){
	return pptr()-gptr();
}

char *qstreambuf::data(){
	return gptr();
}

qstreambuf::int_type qstreambuf::underflow(){
	if( gptr()==egptr() ){
		if( gptr()==pptr() ) return traits_type::eof();
		setg( gptr(),gptr(),pptr() );
	}

	return traits_type::to_int_type( *gptr() );
}

qstreambuf::int_type qstreambuf::overflow( qstreambuf::int_type c ){
	if( c==traits_type::eof() ) return c;

	if( pptr()==epptr() ){
		int sz=size();
		int n_sz=sz*2;if( n_sz<MIN_SIZE ) n_sz=MIN_SIZE;
		char *n_buf=d_new char[ n_sz ];
		memcpy( n_buf,gptr(),sz );
		delete[] buf;buf=n_buf;
		setg( buf,buf,buf+sz );
		setp( buf+sz,buf+n_sz );
	}

	*pptr()=traits_type::to_char_type( c );
	pbump( 1 );return traits_type::not_eof( c );
}
