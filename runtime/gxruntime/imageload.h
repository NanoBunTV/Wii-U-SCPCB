// Image file loading for canvases (PNG / JPG / BMP / TGA via stb_image), applying Blitz's rules :
// alpha from the file or built from the colour (CANVAS_TEX_ALPHA), black masked out (CANVAS_TEX_MASK),
// textures stretched to power-of-two sizes. Same behaviour as the original ddUtil::loadSurface.
#ifndef IMAGELOAD_H
#define IMAGELOAD_H

#include <string>
#include <vector>

namespace imageload{

// Textures are always power-of-two sized (Blitz forces this on every driver)
void adjustTexSize( int *width,int *height );

// Decodes `path` into ARGB pixels; for flags with CANVAS_TEXTURE the result is resized to adjustTexSize
bool load( const std::string &path,int flags,std::vector<unsigned> *argb,int *width,int *height );

}

#endif
