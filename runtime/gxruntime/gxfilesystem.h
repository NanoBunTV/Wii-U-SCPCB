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
#include "gxdir.h"

class gxFileSystem{
public:
	gxFileSystem();
	~gxFileSystem();

	static std::string mapPath( const std::string &path,bool write );
	static std::string resolvePath( const std::string &path ){ return mapPath( path,false ); }
	// Writable folder for saves / options (on the SD card), with a trailing slash
	static std::string writableRoot();
	// Forget cached directory listings of the writable overlay (after creating / deleting files)
	static void invalidateCache();

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
