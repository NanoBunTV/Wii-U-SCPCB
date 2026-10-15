#ifndef WIIU_APP_H
#define WIIU_APP_H

// App lifecycle (ProcUI) : the RPX main thread runs ProcUI (HOME Menu, closing the app) while the game runs on its
// own thread with a big stack (the main thread only has 64 KB).
//
// - When the system takes the screen back (HOME Menu), the main thread asks the game thread to pause; the game
//   thread pauses at its next appPoll() (from gxRuntime::idle, i.e. every Flip / input poll) after waiting for
//   the GPU, and only then does the main thread release the foreground. It resumes when the app is back.
// - When the system closes the app, appPoll() returns false : the Blitz program ends.
// - When the program ends by itself, the main thread asks for the HOME Menu and waits for the system to close
//   the app.

namespace wiiu {

// Main thread : runs game() on the game thread and handles ProcUI until the system closes the app
int appMain( int (*game)() );

// Game thread : false when the app must close. Blocks while the app is in the background.
bool appPoll();

// Game thread, after the program has ended (and freed what it wants to) : waits until the system closes the app.
// GX2 must stay initialised until this returns (the system takes the screen back first).
void appWaitForExit();

// True once the system has asked the app to close (graphics are gone : don't free them)
bool appClosing();

// Game thread : called by appPoll() when the app is back in the foreground (WHBGfx has rebuilt its MEM1 heap, so
// whatever was allocated in MEM1 must be allocated again)
void appSetResumeCallback( void (*fn)() );

// Game thread : called by appPoll() just before pausing for the background, and right after coming back (audio)
void appSetPauseCallbacks( void (*pause)(),void (*resume)() );

}

#endif
