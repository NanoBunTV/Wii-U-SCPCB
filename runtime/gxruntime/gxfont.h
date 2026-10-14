// Wii U gxFont : TrueType font rasterised with stb_truetype into a glyph atlas canvas (1-bit, like Blitz's
// non-antialiased GDI fonts). Characters 0..255 (Windows-1252 code page, as Blitz strings are bytes).
#ifndef GXFONT_H
#define GXFONT_H

#include <string>

class gxCanvas;
class gxGraphics;

class gxFont{
public:
	gxFont( gxGraphics *graphics,gxCanvas *atlas,int height,int begin_char,int end_char,int def_char,
		int *offs,int *widths );
	~gxFont();

	int charWidth( int c )const;
	void render( gxCanvas *dest,unsigned color_argb,int x,int y,const std::string &t );

	gxCanvas *atlas()const{ return canvas; }

private:
	gxGraphics *graphics;
	gxCanvas *canvas;
	int width,height,begin_char,end_char,def_char;
	int *offs,*widths;

	/***** GX INTERFACE *****/
public:
	enum{
		FONT_BOLD=1,
		FONT_ITALIC=2,
		FONT_UNDERLINE=4
	};

	//ACCESSORS
	int getWidth()const;							//width of widest char
	int getHeight()const;							//height of font
	int getWidth( const std::string &text )const;	//width of string
	bool isPrintable( int chr )const;				//printable char?
};

#endif
