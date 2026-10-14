.lib "wiiu"

; Wii U port : native helpers (runtime/userlibs/userlibs_stubs.cpp)

; Length bytes of a bank from offset, as a string (one allocation instead of one per character)
WiiU_PeekString$(bank*,offset%,length%):"WiiU_PeekString"
