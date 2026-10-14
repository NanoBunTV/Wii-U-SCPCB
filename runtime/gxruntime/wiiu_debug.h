#ifndef WIIU_DEBUG_H
#define WIIU_DEBUG_H

// Diagnostics for real hardware, where there's no debugger and OSReport output is invisible :
//  - log file : every WHBLog line also goes to sd:/wiiu/apps/scpcb/log.txt (rewritten on each start)
//  - crash handler : DSI / ISI / program / alignment exceptions log PC, LR, DAR and the stack's return
//    addresses, then show them on screen with OSFatal
//  - watchdog : when no frame has been presented for 10 s, logs where the game thread is (PC, LR, stack)
// Decode addresses with : powerpc-eabi-addr2line -f -C -e build/scpcb_game.elf <addresses>
// (see Docs/BUILDING.md "Hardware").

#include <coreinit/thread.h>
#include <cstdint>
#include <cstdio>

namespace wiiu {

void debugInit();
void debugShutdown();

// Watches this thread for hangs (no frame presented for 10 s : logs where it is)
void debugWatchThread( OSThread *t );

// Called by the presenting code each frame : the watchdog's "still alive" signal
void debugFramePresented();

// Function called every 20 s by the watchdog thread to append a report to sd:/wiiu/apps/scpcb/profile.txt
void debugSetReport( void (*fn)( FILE *f,uint32_t frames,int seconds ) );

}

#endif
