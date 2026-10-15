// Wii U software keyboard (nn::swkbd) for the game's text boxes : save name, map seed, console.
#ifndef WIIU_KEYBOARD_H
#define WIIU_KEYBOARD_H

#include <string>

class gxGraphics;

namespace wiiu {

// Shows the system keyboard over the current picture (the back buffer, presented each frame with the keyboard on
// top) until OK or Cancel. Blitz strings are bytes (Windows-1252) : characters above 255 come back as '?'.
// True with *out set on OK; false on Cancel, if the keyboard can't be shown, or when the app is closing.
bool keyboardInput( gxGraphics *graphics,const std::string &initial,const std::string &hint,int max_length,
	std::string *out );

}

#endif
