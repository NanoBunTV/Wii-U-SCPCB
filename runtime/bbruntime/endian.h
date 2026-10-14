// Wii U port : Blitz's file streams and banks are little-endian (x86). These helpers read / write that
// layout on the big-endian Espresso CPU.
#ifndef BB_ENDIAN_H
#define BB_ENDIAN_H

#include <cstring>

static inline unsigned bbGetLE16( const void *p ){
	const unsigned char *b=(const unsigned char*)p;
	return b[0]|(b[1]<<8);
}
static inline unsigned bbGetLE32( const void *p ){
	const unsigned char *b=(const unsigned char*)p;
	return b[0]|(b[1]<<8)|(b[2]<<16)|((unsigned)b[3]<<24);
}
static inline void bbPutLE16( void *p,unsigned n ){
	unsigned char *b=(unsigned char*)p;
	b[0]=n&0xff;b[1]=(n>>8)&0xff;
}
static inline void bbPutLE32( void *p,unsigned n ){
	unsigned char *b=(unsigned char*)p;
	b[0]=n&0xff;b[1]=(n>>8)&0xff;b[2]=(n>>16)&0xff;b[3]=(n>>24)&0xff;
}
static inline float bbIntBitsToFloat( unsigned n ){
	float f;memcpy( &f,&n,4 );return f;
}
static inline unsigned bbFloatToIntBits( float f ){
	unsigned n;memcpy( &n,&f,4 );return n;
}

#endif
