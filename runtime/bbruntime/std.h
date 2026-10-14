
#ifndef STD_H
#define STD_H

//#ifndef _WINSOCKAPI_
//#define _WINSOCKAPI_   /* Prevent inclusion of winsock.h in windows.h */ 
//#endif
#include <cstring>
#include <cstdlib>
#include <cstdio>
//#include <winsock2.h>

#include "../config/config.h"
#include "../stdutil/stdutil.h"

#include <set>
#include <map>
#include <list>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

#include <math.h>

using namespace std;

// Wii U port : the *_link() symbol tables pass function pointers as void* (MSVC allowed the implicit
// conversion). In the port these tables only document the command signatures for the transpiler.
#define rtSym( _sym,_fn ) rtSym( _sym,(void*)(_fn) )

#endif
