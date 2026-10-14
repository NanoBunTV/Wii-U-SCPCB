// Wii U gxMovie : not supported yet (OpenMovie returns 0)
#ifndef GXMOVIE_H
#define GXMOVIE_H

class gxCanvas;
class gxGraphics;

class gxMovie{
public:
	gxMovie( gxGraphics *gfx );
	~gxMovie();

	/***** GX INTERFACE *****/
public:
	bool draw( gxCanvas *dest,int x,int y,int w,int h ){ return false; }
	bool isPlaying()const{ return false; }
	int getWidth()const{ return 0; }
	int getHeight()const{ return 0; }
};

#endif
