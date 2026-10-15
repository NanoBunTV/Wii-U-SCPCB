// Wii U gxFileSystem.
//
// Blitz paths are relative to the current directory, use backslashes and are case-insensitive (Windows).
// mapPath() turns them into real paths :
//  - reads look in the writable overlay on the SD card first (sd:/wiiu/apps/scpcb/), then in the read-only
//    content folder of the .wuhb (fs:/vol/content/), matching each path component case-insensitively;
//  - writes always go to the overlay (parent folders are created).
#ifndef GXFILESYSTEM_H
#define GXFILESYSTEM_H

#include <string>
#include <vector>
#include <new>
#include <cstdlib>
#include <malloc.h>
#include "gxdir.h"

// Allocator for file buffers : 64-byte aligned (the console's file system reads straight into aligned memory,
// anything else goes through a small bounce buffer) and no zero fill on resize.
template<class T> struct FileAllocator{
	typedef T value_type;
	FileAllocator(){}
	template<class U> FileAllocator( const FileAllocator<U>& ){}
	T *allocate( size_t n ){
		void *p=memalign( 64,(n*sizeof(T)+63)&~63 );
		if( !p ) throw std::bad_alloc();
		return (T*)p;
	}
	void deallocate( T *p,size_t ){ free( p ); }
	template<class U> void construct( U *p ){ ::new((void*)p) U; }
	template<class U,class A> void construct( U *p,const A &a ){ ::new((void*)p) U( a ); }
	template<class U> struct rebind{ typedef FileAllocator<U> other; };
	bool operator==( const FileAllocator& )const{ return true; }
	bool operator!=( const FileAllocator& )const{ return false; }
};
typedef std::vector<unsigned char,FileAllocator<unsigned char> > FileData;

class gxFileSystem{
public:
	gxFileSystem();
	~gxFileSystem();

	static std::string mapPath( const std::string &path,bool write );
	static std::string resolvePath( const std::string &path ){ return mapPath( path,false ); }
	// Writable folder for saves / options (on the SD card), with a trailing slash
	static std::string writableRoot();
	//true for real paths in the writable overlay (SD card), false for the read-only content
	static bool isWritablePath( const std::string &real_path );
	// Forget cached directory listings of the writable overlay (after creating / deleting files)
	static void invalidateCache();
	// Whole file (real path) into memory with one read call. stdio reads in small pieces (stb_image asks for 128
	// bytes at a time), and every file system call is slow on the console.
	// Files the build stored zstd-compressed (tools/pack_content.py) are decompressed (zstd frame magic first).
	static bool readWholeFile( const std::string &real_path,FileData *out );
	// True for the file types pack_content.py compresses (.wbc .wbc3 .rmesh .b3d) : streams opening them read the
	// whole file through readWholeFile
	static bool maybeCompressed( const std::string &real_path );
	// Read statistics by file type ("file reads, by type :" log lines once a loading burst is over) : bytes read
	// from path in ticks (OSTime), new_file for the first read of a file
	static void countRead( const std::string &path,size_t bytes,unsigned long long ticks,bool new_file );
	static void logReadSummary();

	/***** GX INTERFACE *****/
public:
	enum{
		FILE_TYPE_NONE=0,FILE_TYPE_FILE=1,FILE_TYPE_DIR=2
	};

	bool createDir( const std::string &dir );
	bool deleteDir( const std::string &dir );
	bool createFile( const std::string &file );
	bool deleteFile( const std::string &file );
	bool copyFile( const std::string &src,const std::string &dest );
	bool renameFile( const std::string &src,const std::string &dest );
	bool setCurrentDir( const std::string &dir );

	std::string getCurrentDir()const;
	int getFileSize( const std::string &name )const;
	int getFileType( const std::string &name )const;

	gxDir *openDir( const std::string &name,int flags );
	gxDir *verifyDir( gxDir *d );
	void closeDir( gxDir *dir );
};

#endif
