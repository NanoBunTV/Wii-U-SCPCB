#include "std.h"
#include "gxfont.h"
#include "gxcanvas.h"
#include "gxgraphics.h"

gxFont::gxFont( gxGraphics *g,gxCanvas *c,int h,int b,int e,int d,int *os,int *ws ):
graphics(g),canvas(c),height(h),begin_char(b),end_char(e),def_char(d),offs(os),widths(ws){
	width=0;
	for( int k=0;k<end_char-begin_char;++k ) if( widths[k]>width ) width=widths[k];
}

gxFont::~gxFont(){
	delete[] offs;
	delete[] widths;
	delete canvas;
}

int gxFont::charWidth( int c )const{
	if( c<begin_char || c>=end_char ) c=def_char;
	return widths[c-begin_char];
}

void gxFont::render( gxCanvas *dest,unsigned color_argb,int x,int y,const string &t ){
	int ox,oy,vx,vy,vw,vh;
	dest->getOrigin( &ox,&oy );
	dest->getViewport( &vx,&vy,&vw,&vh );
	gxRect vp( vx,vy,vw,vh );
	x+=ox;y+=oy;

	bool gpu=dest->isRenderTarget();
	const unsigned *atlas_pix=gpu ? 0 : canvas->cpuPixels();
	unsigned *dest_pix=gpu ? 0 : dest->cpuPixels();
	int aw=canvas->getWidth(),dw=dest->getWidth();
	bool drew=false;

	for( size_t k=0;k<t.size();++k ){
		int c=t[k]&0xff;
		if( c<begin_char || c>=end_char ) c=def_char;
		c-=begin_char;
		int sx=(offs[c]>>16)&0xffff,sy=offs[c]&0xffff,w=widths[c];
		gxRect d( x,y,w,height ),s( sx,sy,w,height );
		x+=w;
		//clip to the viewport
		int dx;
		if( (dx=vp.left-d.left)>0 ){ d.left+=dx;s.left+=dx; }
		if( (dx=vp.right-d.right)<0 ){ d.right+=dx;s.right+=dx; }
		if( (dx=vp.top-d.top)>0 ){ d.top+=dx;s.top+=dx; }
		if( (dx=vp.bottom-d.bottom)<0 ){ d.bottom+=dx;s.bottom+=dx; }
		if( d.right<=d.left || d.bottom<=d.top ) continue;

		if( gpu ){
			graphics->draw2DGlyphs( dest,d,canvas,s,color_argb );
		}else{
			for( int j=0;j<d.bottom-d.top;++j ){
				const unsigned *sr=atlas_pix+(s.top+j)*aw+s.left;
				unsigned *dr=dest_pix+(d.top+j)*dw+d.left;
				//software text is 1-bit like Blitz's : a pixel is set when the glyph covers at least ~40% of it
				for( int i=0;i<d.right-d.left;++i ) if( (sr[i]>>24)>=100 ) dr[i]=color_argb;
			}
		}
		drew=true;
	}
	if( !drew ) return;
	if( gpu ) dest->gpuModified();
	else dest->cpuModified();
}

int gxFont::getWidth()const{
	return width;
}

int gxFont::getHeight()const{
	return height;
}

int gxFont::getWidth( const string &t )const{
	int w=0;
	for( size_t k=0;k<t.size();++k ) w+=charWidth( t[k]&0xff );
	return w;
}

bool gxFont::isPrintable( int chr )const{
	return chr>=begin_char && chr<end_char;
}
