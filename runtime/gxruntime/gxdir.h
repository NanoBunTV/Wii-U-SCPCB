// Wii U gxDir : directory listing for ReadDir / NextFile
#ifndef GXDIR_H
#define GXDIR_H

#include <string>
#include <vector>

class gxDir{
public:
	gxDir( const std::vector<std::string> &entries );
	~gxDir();

private:
	std::vector<std::string> entries;
	size_t next;

	/***** GX INTERFACE *****/
public:
	std::string getNextFile();
};

#endif
