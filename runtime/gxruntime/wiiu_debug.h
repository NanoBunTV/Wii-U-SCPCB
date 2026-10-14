#ifndef WIIU_DEBUG_H
#define WIIU_DEBUG_H

// Diagnostics for real hardware, where there's no debugger and OSReport output is invisible :
//  - log file : every WHBLog line also goes to sd:/wiiu/apps/scpcb/log.txt (rewritten on each start)
//  - crash handler : DSI / ISI / program / alignment exceptions log PC, LR, DAR and the stack's return
//    addresses, then show them on screen with OSFatal
//  - watchdog : when no frame has been presented for 10 s, logs where the game thread is (PC, LR, stack)
// Decode addresses with : powerpc-eabi-addr2line -f -C -e build/scpcb_game.elf <addresses>
// (see Docs/BUILDING.md "Hardware").

namespace wiiu {

void debugInit();
void debugShutdown();

// Runs fn on a thread with a big stack, on CPU1 like the main thread, and waits for it. The RPX main thread
// only has a 64 KB stack (elf2rpl's default, with no option to change it).
int runOnBigStack( int (*fn)() );

// Called by the presenting code each frame : the watchdog's "still alive" signal
void debugFramePresented();

}

#endif
