// stb_vorbis (Ogg Vorbis decoder) compiled once for gxaudio.cpp. Memory API only (files are read whole with
// gxFileSystem::readWholeFile). Decoded samples are native-endian shorts : big-endian on the Wii U, which is what
// AX's LPCM16 voices read. (Its fast float-to-int trick reads a float's bits as an int : the same on both byte
// orders.)
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_BIG_ENDIAN
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "stb_vorbis.c"
