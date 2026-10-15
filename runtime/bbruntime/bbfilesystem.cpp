
#include "std.h"
#include "bbfilesystem.h"
#include "bbstream.h"
#include <fstream>
#include <malloc.h>
#include <whb/log.h>
#include <coreinit/time.h>

gxFileSystem *gx_filesys;

// Wii U port : OpenFile (read/write) opens the file read-only and only reopens it read/write on the first
// write (copying it from the read-only content to the writable overlay first). The game mostly uses OpenFile to
// read (e.g. loadingscreens.ini), and while a file is open for writing the Wii U file system refuses to open it
// again, which broke the game's ReadFile of the same file (hardware only; Windows allows it).
// Wii U port : a zstd-compressed content file (tools/pack_content.py) is decompressed whole into memory when
// opened for reading and read from there
struct MemBuf : public streambuf{
	FileData data;
	void init(){
		char *b=(char*)data.data();
		setg( b,b,b+data.size() );
	}
	pos_type seekoff( off_type off,ios_base::seekdir dir,ios_base::openmode which ){
		char *p=dir==ios_base::beg ? eback()+off : (dir==ios_base::cur ? gptr()+off : egptr()+off);
		if( p<eback() || p>egptr() ) return pos_type( off_type( -1 ) );
		setg( eback(),p,egptr() );
		return pos_type( p-eback() );
	}
	pos_type seekpos( pos_type pos,ios_base::openmode which ){
		return seekoff( off_type( pos ),ios_base::beg,which );
	}
};

struct bbFile : public bbStream{
	filebuf *buf;		//0 for a decompressed file (mem)
	MemBuf *mem;
	char *io_buf;
	string name;		//Blitz path, for OpenFile's deferred reopen
	bool write_pending;
	bbFile( filebuf *f,char *b ):buf(f),mem(0),io_buf(b),write_pending(false){
	}
	bbFile( MemBuf *m ):buf(0),mem(m),io_buf(0),write_pending(false){
	}
	~bbFile(){
		delete buf;
		delete mem;
		free( io_buf );
	}
	streambuf *sb(){ return mem ? (streambuf*)mem : (streambuf*)buf; }
	bool makeWritable(){
		write_pending=false;
		streampos pos=buf->pubseekoff( 0,ios_base::cur,ios_base::in );
		buf->close();
		string src=gxFileSystem::mapPath( name,false );
		if( !gxFileSystem::isWritablePath( src ) ) gx_filesys->copyFile( name,name );
		string t=gxFileSystem::mapPath( name,true );
		bool ok=buf->open( t.c_str(),ios_base::in|ios_base::out|ios_base::binary )!=0;
		if( !ok ){
			WHBLogPrintf( "file: can't open %s for writing",t.c_str() );
			buf->open( src.c_str(),ios_base::in|ios_base::binary );
		}
		if( pos!=streampos( -1 ) ) buf->pubseekpos( pos,ios_base::in|ios_base::out );
		gxFileSystem::invalidateCache();
		return ok;
	}
	int read( char *buff,int size ){
		if( mem ) return mem->sgetn( buff,size );
		OSTime t0=OSGetSystemTime();
		int n=buf->sgetn( (char*)buff,size );
		gxFileSystem::countRead( name,n>0 ? n : 0,OSGetSystemTime()-t0,false );
		return n;
	}
	int write( const char *buff,int size ){
		if( mem ) return 0;
		if( write_pending && !makeWritable() ) return 0;
		return buf->sputn( (char*)buff,size );
	}
	int avail(){
		return sb()->in_avail();
	}
	int eof(){
		return sb()->sgetc()==EOF;
	}
};

static set<bbFile*> file_set;

static inline void debugFile( bbFile *f ){
	if( debug ){
		if( !file_set.count( f ) ) RTEX( "File does not exist" );
	}
}

static inline void debugDir( gxDir *d ){
	if( debug ){
		if( !gx_filesys->verifyDir( d ) ) RTEX( "Directory does not exist" );
	}
}

static bbFile *open( BBStr *f,int n ){
	//Wii U port : paths go through gxFileSystem (content folder / writable overlay, case-insensitive)
	string name=*f,t;
	delete f;
	bool read_write=(n & ios_base::in) && (n & ios_base::out);
	if( read_write ){
		t=gxFileSystem::mapPath( name,false );
		n=ios_base::in;		//read/write on the first write (see bbFile)
	}else{
		t=gxFileSystem::mapPath( name,(n & ios_base::out)!=0 );
	}
	//file types the build may have compressed : read whole (decompressed) into memory
	if( n==ios_base::in && gxFileSystem::maybeCompressed( t ) ){
		MemBuf *m=d_new MemBuf;
		if( gxFileSystem::readWholeFile( t,&m->data ) ){
			m->init();
			bbFile *file=d_new bbFile( m );
			file->name=name;
			file_set.insert( file );
			return file;
		}
		delete m;
	}
	//big buffer : many small reads are slow on the Wii U file system; 64-byte aligned, so the file system reads
	//straight into it instead of through its own small bounce buffer
	const int IO_BUF=64*1024;
	char *io_buf=(char*)memalign( 64,IO_BUF );
	filebuf *buf=d_new filebuf();
	buf->pubsetbuf( io_buf,IO_BUF );
	if( buf->open( t.c_str(),(ios_base::openmode)(n|ios_base::binary) ) ){
		bbFile *file=d_new bbFile( buf,io_buf );
		file->name=name;
		if( !(n & ios_base::out) ) gxFileSystem::countRead( name,0,0,true );
		file->write_pending=read_write;
		file_set.insert( file );
		if( n & ios_base::out ) gxFileSystem::invalidateCache();
		return file;
	}
	WHBLogPrintf( "file: can't open %s",t.c_str() );
	delete buf;
	free( io_buf );
	return 0;
}

bbFile *bbReadFile( BBStr *f ){
	return open( f,ios_base::in );
}

bbFile *bbWriteFile( BBStr *f ){
	return open( f,ios_base::out|ios_base::trunc );
}

bbFile *bbOpenFile( BBStr *f ){
	return open( f,ios_base::in|ios_base::out );
}

void bbCloseFile( bbFile *f ){
	debugFile( f );
	file_set.erase( f );
	delete f;
}

int bbFilePos( bbFile *f ){
	return f->sb()->pubseekoff( 0,ios_base::cur );
}

int bbSeekFile( bbFile *f,int pos ){
	return f->sb()->pubseekoff( pos,ios_base::beg );
}

gxDir *bbReadDir( BBStr *d ){
	string t=*d;delete d;
	return gx_filesys->openDir( t,0 );
}

void bbCloseDir( gxDir *d ){
	gx_filesys->closeDir( d );
}

BBStr *bbNextFile( gxDir *d ){
	debugDir( d );
	return d_new BBStr( d->getNextFile() );
}

BBStr *bbCurrentDir(){
	return d_new BBStr( gx_filesys->getCurrentDir() );
}

void bbChangeDir( BBStr *d ){
	gx_filesys->setCurrentDir( *d );
	delete d;
}

void bbCreateDir( BBStr *d ){
	gx_filesys->createDir( *d );
	delete d;
}

void bbDeleteDir( BBStr *d ){
	gx_filesys->deleteDir( *d );
	delete d;
}

int bbFileType( BBStr *f ){
	string t=*f;delete f;
	int n=gx_filesys->getFileType( t );
	return n==gxFileSystem::FILE_TYPE_FILE ? 1 : (n==gxFileSystem::FILE_TYPE_DIR ? 2 : 0);
}

int	bbFileSize( BBStr *f ){
	string t=*f;delete f;
	return gx_filesys->getFileSize( t );
}

void bbCopyFile( BBStr *f,BBStr *to ){
	string src=*f,dest=*to;
	delete f;delete to;
	gx_filesys->copyFile( src,dest );
}

void bbDeleteFile( BBStr *f ){
	gx_filesys->deleteFile( *f );
	delete f;
}

bool filesystem_create(){
	if( gx_filesys=gx_runtime->openFileSystem( 0 ) ){
		return true;
	}
	return false;
}

bool filesystem_destroy(){
	while( file_set.size() ) bbCloseFile( *file_set.begin() );
	gx_runtime->closeFileSystem( gx_filesys );
	return true;
}

void filesystem_link( void(*rtSym)(const char*,void*) ){
	rtSym( "%OpenFile$filename",bbOpenFile );
	rtSym( "%ReadFile$filename",bbReadFile );
	rtSym( "%WriteFile$filename",bbWriteFile );
	rtSym( "CloseFile%file_stream",bbCloseFile );
	rtSym( "%FilePos%file_stream",bbFilePos );
	rtSym( "%SeekFile%file_stream%pos",bbSeekFile );

	rtSym( "%ReadDir$dirname",bbReadDir );
	rtSym( "CloseDir%dir",bbCloseDir );
	rtSym( "$NextFile%dir",bbNextFile );
	rtSym( "$CurrentDir",bbCurrentDir );
	rtSym( "ChangeDir$dir",bbChangeDir );
	rtSym( "CreateDir$dir",bbCreateDir );
	rtSym( "DeleteDir$dir",bbDeleteDir );

	rtSym( "%FileSize$file",bbFileSize );
	rtSym( "%FileType$file",bbFileType );
	rtSym( "CopyFile$file$to",bbCopyFile );
	rtSym( "DeleteFile$file",bbDeleteFile );
}
