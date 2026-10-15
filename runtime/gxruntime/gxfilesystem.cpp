#include "std.h"
#include "gxfilesystem.h"

#include <dirent.h>
#include <fcntl.h>
#include <coreinit/time.h>
#include <whb/log.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <map>
#include <algorithm>
#include <zstd.h>
#include <whb/log.h>

static const char *CONTENT_ROOT="fs:/vol/content/";
static const char *WRITE_ROOT="fs:/vol/external01/wiiu/apps/scpcb/";

static string cur_dir;			//relative to the roots, '/' separated, empty or ending with '/'
static set<gxDir*> dir_set;

// directory listings, keyed by real directory path (no trailing slash)
static map<string,vector<string> > dir_cache;

// The content folder's listings come from content/filelist.txt (written at build time by
// tools/gen_filelist.cmake) : listing /vol/content with opendir doesn't work on real hardware (Aroma), and
// its file names are case-sensitive there, unlike in Cemu on Windows.
static bool content_index_loaded;

static void loadContentIndex(){
	content_index_loaded=true;
	string root( CONTENT_ROOT,strlen( CONTENT_ROOT )-1 );
	FILE *f=fopen( (string( CONTENT_ROOT )+"filelist.txt").c_str(),"rb" );
	if( !f ){
		WHBLogPrintf( "gxFileSystem: no content/filelist.txt, listing the content folder instead" );
		return;
	}
	dir_cache[root];		//the root exists even if empty
	char line[1024];
	int n=0;
	while( fgets( line,sizeof(line),f ) ){
		size_t len=strcspn( line,"\r\n" );
		line[len]=0;
		if( !len ) continue;
		bool is_dir=line[len-1]=='/';
		if( is_dir ) line[--len]=0;
		string rel( line );
		size_t k=rel.find_last_of( '/' );
		string parent=k==string::npos ? root : root+"/"+rel.substr( 0,k );
		string name=k==string::npos ? rel : rel.substr( k+1 );
		dir_cache[parent].push_back( name );
		if( is_dir ) dir_cache[root+"/"+rel];
		++n;
	}
	fclose( f );
	WHBLogPrintf( "gxFileSystem: content index, %d entries",n );
}

static bool inContent( const string &dir ){
	size_t n=strlen( CONTENT_ROOT )-1;
	return dir.compare( 0,n,CONTENT_ROOT,n )==0 && (dir.size()==n || dir[n]=='/');
}

static const vector<string> *listDir( const string &dir ){
	if( !content_index_loaded && inContent( dir ) ) loadContentIndex();
	map<string,vector<string> >::iterator it=dir_cache.find( dir );
	if( it!=dir_cache.end() ) return &it->second;
	DIR *d=opendir( dir.c_str() );
	if( !d ) return 0;
	vector<string> &names=dir_cache[dir];
	while( dirent *e=readdir( d ) ){
		if( !strcmp( e->d_name,"." ) || !strcmp( e->d_name,".." ) ) continue;
		names.push_back( e->d_name );
	}
	closedir( d );
	return &names;
}

// Bytes read per file type since the last summary (see logReadSummary)
struct ReadTally{
	unsigned files;
	uint64_t bytes,ticks;
};
static map<string,ReadTally> read_tally;
static OSTime last_read_time;
static uint64_t decompress_ticks,decompress_bytes;		//compressed files : time spent decompressing, bytes out

bool gxFileSystem::maybeCompressed( const string &path ){
	size_t dot=path.find_last_of( '.' ),slash=path.find_last_of( '/' );
	if( dot==string::npos || (slash!=string::npos && dot<slash) ) return false;
	string ext=tolower( path.substr( dot ) );
	return ext==".wbc" || ext==".wbc3" || ext==".rmesh" || ext==".b3d";
}

void gxFileSystem::countRead( const string &path,size_t bytes,uint64_t ticks,bool new_file ){
	size_t dot=path.find_last_of( '.' ),slash=path.find_last_of( '/' );
	string ext=dot!=string::npos && (slash==string::npos || dot>slash) ? tolower( path.substr( dot ) ) : "(none)";
	ReadTally &t=read_tally[ext];
	if( new_file ) ++t.files;
	t.bytes+=bytes;t.ticks+=ticks;
	last_read_time=OSGetSystemTime();
}

void gxFileSystem::logReadSummary(){
	//once reading has stopped for 2 s (the end of a loading screen)
	if( read_tally.empty() || OSTicksToMilliseconds( OSGetSystemTime()-last_read_time )<2000 ) return;
	vector<pair<uint64_t,string> > order;
	uint64_t bytes=0,ticks=0;unsigned files=0;
	for( map<string,ReadTally>::const_iterator it=read_tally.begin();it!=read_tally.end();++it ){
		order.push_back( make_pair( it->second.bytes,it->first ) );
		bytes+=it->second.bytes;ticks+=it->second.ticks;files+=it->second.files;
	}
	sort( order.rbegin(),order.rend() );
	string s;
	char buf[96];
	for( size_t k=0;k<order.size() && k<10;++k ){
		const ReadTally &t=read_tally[order[k].second];
		snprintf( buf,sizeof(buf),"%s%s %u files %.1f MB %.0f ms",k ? ", " : "",order[k].second.c_str(),t.files,
			t.bytes/1048576.0f,OSTicksToMicroseconds( t.ticks )/1000.0f );
		s+=buf;
	}
	WHBLogPrintf( "file reads, total : %u files, %.1f MB in %.0f ms; decompressed %.1f MB in %.0f ms",files,
		bytes/1048576.0f,OSTicksToMicroseconds( ticks )/1000.0f,decompress_bytes/1048576.0f,
		OSTicksToMicroseconds( decompress_ticks )/1000.0f );
	WHBLogPrintf( "file reads, by type : %s",s.c_str() );
	read_tally.clear();
	decompress_ticks=decompress_bytes=0;
}

bool gxFileSystem::readWholeFile( const string &real_path,FileData *out ){
	out->clear();
	OSTime t0=OSGetSystemTime();
	int fd=open( real_path.c_str(),O_RDONLY );
	if( fd<0 ) return false;
	struct stat st;
	bool ok=fstat( fd,&st )==0 && st.st_size>=0;
	if( ok ){
		out->resize( st.st_size );
		size_t done=0;
		while( ok && done<out->size() ){
			ssize_t n=read( fd,out->data()+done,out->size()-done );
			if( n<=0 ) ok=false;
			else done+=n;
		}
	}
	close( fd );
	if( !ok ) out->clear();
	size_t read_bytes=out->size();
	countRead( real_path,read_bytes,OSGetSystemTime()-t0,true );
	//compressed at build time (tools/pack_content.py) : zstd frame magic 28 B5 2F FD
	if( ok && out->size()>=4 && (*out)[0]==0x28 && (*out)[1]==0xb5 && (*out)[2]==0x2f && (*out)[3]==0xfd ){
		OSTime t1=OSGetSystemTime();
		unsigned long long n=ZSTD_getFrameContentSize( out->data(),out->size() );
		FileData plain;
		if( n==ZSTD_CONTENTSIZE_ERROR || n==ZSTD_CONTENTSIZE_UNKNOWN || n>0x40000000ull ){
			WHBLogPrintf( "gxFileSystem: bad compressed file %s",real_path.c_str() );
			ok=false;
		}else{
			static ZSTD_DCtx *dctx;
			if( !dctx ) dctx=ZSTD_createDCtx();
			plain.resize( (size_t)n );
			size_t r=ZSTD_decompressDCtx( dctx,plain.data(),plain.size(),out->data(),out->size() );
			if( ZSTD_isError( r ) || r!=n ){
				WHBLogPrintf( "gxFileSystem: can't decompress %s (%s)",real_path.c_str(),ZSTD_isError( r ) ? ZSTD_getErrorName( r ) : "size" );
				ok=false;
			}
		}
		out->swap( plain );
		if( !ok ) out->clear();
		decompress_ticks+=OSGetSystemTime()-t1;
		decompress_bytes+=out->size();
	}
	//read speed in the log every 100 files (open + read + close, from the content folder or the SD card)
	static unsigned files;
	static uint64_t bytes,ticks;
	++files;bytes+=out->size();ticks+=OSGetSystemTime()-t0;
	if( files==100 ){
		float ms=OSTicksToMicroseconds( ticks )/1000.0f;
		WHBLogPrintf( "file reads : 100 files, %.1f MB in %.0f ms (%.1f MB/s, %.1f ms per file)",
			bytes/1048576.0f,ms,ms>0 ? bytes/1048576.0f/(ms/1000.0f) : 0.0f,ms/100 );
		files=0;bytes=ticks=0;
	}
	return ok;
}

void gxFileSystem::invalidateCache(){
	size_t n=strlen( WRITE_ROOT )-1;
	for( map<string,vector<string> >::iterator it=dir_cache.begin();it!=dir_cache.end(); ){
		if( it->first.compare( 0,n,WRITE_ROOT,n )==0 ) dir_cache.erase( it++ );
		else ++it;
	}
}

// Finds `rel` (components separated by '/') under `root` (ending with '/'), ignoring case.
// Returns the real path, or "" if some component doesn't exist.
static string findCaseless( const string &root,const string &rel ){
	string path=root;
	size_t i=0;
	while( i<rel.size() ){
		size_t j=rel.find( '/',i );
		if( j==string::npos ) j=rel.size();
		string comp=rel.substr( i,j-i );
		i=j+1;
		if( comp.empty() || comp=="." ) continue;
		if( comp==".." ){
			if( path.size()>root.size() ){
				size_t k=path.find_last_of( '/',path.size()-2 );
				if( k!=string::npos ) path.resize( k+1 );
			}
			continue;
		}
		const vector<string> *names=listDir( path.substr( 0,path.size()-1 ) );
		if( !names ) return "";
		string found;
		for( size_t n=0;n<names->size();++n ){
			if( (*names)[n]==comp ){ found=comp;break; }
			if( found.empty() && !strcasecmp( (*names)[n].c_str(),comp.c_str() ) ) found=(*names)[n];
		}
		if( found.empty() ) return "";
		path+=found;
		if( j<rel.size() ) path+='/';
	}
	return path;
}

static string normalise( const string &path ){
	string t=path;
	for( size_t k=0;k<t.size();++k ) if( t[k]==(char)92 ) t[k]='/';
	string r;
	for( size_t k=0;k<t.size();++k ){
		//collapse "//" (but keep "fs:/")
		if( t[k]=='/' && !r.empty() && r[r.size()-1]=='/' ) continue;
		r+=t[k];
	}
	while( r.compare( 0,2,"./" )==0 ) r=r.substr( 2 );
	return r;
}

static bool isAbsolute( const string &t ){
	return t.compare( 0,3,"fs:" )==0 || (t.size() && t[0]=='/');
}

static void makeParents( const string &path ){
	size_t start=strlen( "fs:/vol/external01/" );
	for( size_t k=start;k<path.size();++k ){
		if( path[k]!='/' ) continue;
		mkdir( path.substr( 0,k ).c_str(),0777 );
	}
}

string gxFileSystem::mapPath( const string &path,bool write ){
	string t=normalise( path );
	if( isAbsolute( t ) ) return t;
	string rel=cur_dir+t;

	string w=findCaseless( WRITE_ROOT,rel );
	if( write ){
		if( w.empty() ){
			//keep the case of folders that already exist, append the rest as given
			string parent,leaf=rel;
			size_t k=rel.find_last_of( '/' );
			if( k!=string::npos ){ parent=rel.substr( 0,k );leaf=rel.substr( k+1 ); }
			string p=parent.empty() ? string( WRITE_ROOT ) : findCaseless( WRITE_ROOT,parent );
			if( p.empty() ) w=string( WRITE_ROOT )+rel;
			else w=(parent.empty() ? p : p+"/")+leaf;
			makeParents( w );
			invalidateCache();
		}
		return w;
	}
	if( !w.empty() ) return w;
	string c=findCaseless( CONTENT_ROOT,rel );
	if( !c.empty() ) return c;
	static int not_found_logged;
	if( not_found_logged<200 ){
		++not_found_logged;
		WHBLogPrintf( "gxFileSystem: not found : %s",rel.c_str() );
	}
	return string( CONTENT_ROOT )+rel;
}

string gxFileSystem::writableRoot(){
	return WRITE_ROOT;
}

static bool inOverlay( const string &p ){
	return p.compare( 0,strlen( WRITE_ROOT ),WRITE_ROOT )==0;
}

bool gxFileSystem::isWritablePath( const string &p ){
	return inOverlay( p );
}

gxFileSystem::gxFileSystem(){
	makeParents( WRITE_ROOT );
}

gxFileSystem::~gxFileSystem(){
	while( dir_set.size() ) closeDir( *dir_set.begin() );
}

bool gxFileSystem::createDir( const string &dir ){
	string p=mapPath( dir,true );
	bool ok=mkdir( p.c_str(),0777 )==0;
	invalidateCache();
	return ok;
}

bool gxFileSystem::deleteDir( const string &dir ){
	string p=mapPath( dir,false );
	if( !inOverlay( p ) ) return false;
	bool ok=rmdir( p.c_str() )==0;
	invalidateCache();
	return ok;
}

bool gxFileSystem::createFile( const string &file ){
	FILE *f=fopen( mapPath( file,true ).c_str(),"wb" );
	if( !f ) return false;
	fclose( f );
	invalidateCache();
	return true;
}

bool gxFileSystem::deleteFile( const string &file ){
	string p=mapPath( file,false );
	if( !inOverlay( p ) ) return false;
	bool ok=remove( p.c_str() )==0;
	invalidateCache();
	return ok;
}

bool gxFileSystem::copyFile( const string &src,const string &dest ){
	FILE *in=fopen( mapPath( src,false ).c_str(),"rb" );
	if( !in ) return false;
	FILE *out=fopen( mapPath( dest,true ).c_str(),"wb" );
	if( !out ){ fclose( in );return false; }
	char buf[16384];
	size_t n;
	while( (n=fread( buf,1,sizeof(buf),in ))>0 ) fwrite( buf,1,n,out );
	fclose( in );fclose( out );
	invalidateCache();
	return true;
}

bool gxFileSystem::renameFile( const string &src,const string &dest ){
	string s=mapPath( src,false );
	if( !inOverlay( s ) ) return false;
	bool ok=rename( s.c_str(),mapPath( dest,true ).c_str() )==0;
	invalidateCache();
	return ok;
}

bool gxFileSystem::setCurrentDir( const string &dir ){
	string t=normalise( dir );
	if( isAbsolute( t ) ) return false;
	if( t.size() && t[t.size()-1]!='/' ) t+='/';
	cur_dir=t;
	return true;
}

string gxFileSystem::getCurrentDir()const{
	string t=cur_dir;
	for( size_t k=0;k<t.size();++k ) if( t[k]=='/' ) t[k]=(char)92;
	return t;
}

int gxFileSystem::getFileSize( const string &name )const{
	struct stat st;
	if( stat( mapPath( name,false ).c_str(),&st ) ) return 0;
	return st.st_size;
}

int gxFileSystem::getFileType( const string &name )const{
	string p=mapPath( name,false );
	while( p.size()>1 && p[p.size()-1]=='/' ) p.resize( p.size()-1 );
	if( inContent( p ) ){
		//answer from the content index (stat on /vol/content folders isn't reliable on hardware)
		listDir( p );
		if( content_index_loaded && dir_cache.count( string( CONTENT_ROOT,strlen( CONTENT_ROOT )-1 ) ) ){
			if( dir_cache.count( p ) ) return FILE_TYPE_DIR;
			size_t k=p.find_last_of( '/' );
			map<string,vector<string> >::const_iterator it=dir_cache.find( p.substr( 0,k ) );
			if( it==dir_cache.end() ) return FILE_TYPE_NONE;
			string leaf=p.substr( k+1 );
			for( size_t n=0;n<it->second.size();++n ) if( it->second[n]==leaf ) return FILE_TYPE_FILE;
			return FILE_TYPE_NONE;
		}
	}
	struct stat st;
	if( stat( p.c_str(),&st ) ) return FILE_TYPE_NONE;
	return S_ISDIR( st.st_mode ) ? FILE_TYPE_DIR : FILE_TYPE_FILE;
}

gxDir *gxFileSystem::openDir( const string &name,int flags ){
	//entries of the overlay and the content folder, merged
	string rel=normalise( name );
	if( !isAbsolute( rel ) ) rel=cur_dir+rel;
	vector<string> entries;
	entries.push_back( "." );
	entries.push_back( ".." );
	set<string> seen;
	const char *roots[2]={ WRITE_ROOT,CONTENT_ROOT };
	bool any=false;
	for( int r=0;r<2;++r ){
		string d=isAbsolute( rel ) ? rel : findCaseless( roots[r],rel );
		if( d.empty() ) continue;
		while( d.size()>1 && d[d.size()-1]=='/' ) d.resize( d.size()-1 );
		const vector<string> *names=listDir( d );
		if( !names ) continue;
		any=true;
		for( size_t k=0;k<names->size();++k ){
			if( seen.insert( tolower( (*names)[k] ) ).second ) entries.push_back( (*names)[k] );
		}
		if( isAbsolute( rel ) ) break;
	}
	if( !any ) return 0;
	gxDir *dir=d_new gxDir( entries );
	dir_set.insert( dir );
	return dir;
}

gxDir *gxFileSystem::verifyDir( gxDir *d ){
	return dir_set.count( d ) ? d : 0;
}

void gxFileSystem::closeDir( gxDir *d ){
	if( dir_set.erase( d ) ) delete d;
}
