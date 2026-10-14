# Blitz3D runtime surface used by SCP:CB 1.3.11

Generated from the disassembly of `SCP - Containment Breach.exe` (v1.3.11) by B3DDecomp's `Blitz3DDisasm`
(`scpcb_disasm/BuiltIn/BuiltIn.txt`). This is the **exact** list of Blitz3D commands the shipped game calls, so
it's the checklist for the Wii U runtime. Regenerate with `Docs/tools/list_builtins.sh` (see BUILDING.md).

Each group is named after the original Blitz3D runtime source file that implements it
(`blitz-research/blitz3d`, `bbruntime/<group>.cpp`), which is where to look for reference behaviour.

## Commands (302), by module

### bbaudio (10)

channelpan channelpitch channelplaying channelvolume freesound loadsound pausechannel playsound resumechannel 
stopchannel

### bbbank (12)

banksize copybank createbank freebank peekbyte peekint peekshort pokebyte pokeint readbytes resizebank 
writebytes

### bbblitz3d (144)

activetextures addmesh addtriangle addvertex aligntovector ambientlight animtime antialias brushtexture 
cameraclscolor cameraclsmode camerafogcolor camerafogmode camerafogrange camerapick cameraproject 
cameraprojmode camerarange cameraviewport camerazoom clearsurface clearworld collisions collisiony copyentity 
copymesh countchildren countcollisions countsurfaces countvertices createbrush createcamera createcube 
createlight createmesh createpivot createplane createsprite createsurface createtexture deltapitch deltayaw 
entityalpha entityautofade entityblend entitybox entitycollided entitycolor entitydistance entityfx 
entityinview entityname entityorder entityparent entitypick entitypickmode entitypitch entityradius 
entityroll entityshininess entitytexture entitytype entityvisible entityx entityy entityyaw entityz findchild 
flipmesh freebrush freeentity freetexture getbrushtexture getchild getentitytype getparent getsurface 
getsurfacebrush hideentity lightcolor lightconeangles lightrange linepick loadanimmesh loadanimtexture 
loadbrush loadmesh loadsprite loadtexture meshcullbox meshdepth meshheight meshwidth moveentity nameentity 
paintsurface pickedentity pickednx pickedny pickednz pickedx pickedy pickedz pointentity positionentity 
positionmesh positiontexture projectedx projectedy renderworld resetentity rotateentity rotatemesh 
scaleentity scalemesh scalesprite scaletexture setanimtime showentity spriteviewmode textureblend 
texturebuffer texturecoords textureheight texturename texturewidth tformedx tformedy tformedz tformpoint 
tformvector translateentity trisrendered turnentity updatenormals updateworld vectoryaw vertexcolor 
vertexcoords vertextexcoords vertexx vertexy vertexz wireframe

### bbfilesystem (15)

closedir closefile createdir currentdir deletedir deletefile filepos filesize filetype nextfile openfile 
readdir readfile seekfile writefile

### bbgraphics (56)

availvidmem backbuffer cls clscolor color colorblue colorgreen colorred copyrect countgfxdrivers 
countgfxmodes3d createimage drawimage drawimagerect flip fontheight fontwidth freefont freeimage getcolor 
gfxdrivername gfxmodeheight gfxmodewidth graphics3d graphicsbuffer graphicsheight graphicswidth handleimage 
hidepointer imagebuffer imageheight imagerectoverlap imagewidth line loadanimimage loadfont loadimage 
lockbuffer maskimage midhandle oval readpixelfast rect resizeimage rotateimage scaleimage setbuffer setfont 
showpointer stringheight stringwidth text totalvidmem unlockbuffer windowed3d writepixelfast

### bbinput (13)

flushkeys flushmouse getkey keydown keyhit mousedown mousehit mousex mousexspeed mousey mouseyspeed 
mousezspeed movemouse

### bbmath (10)

atan2 ceil cos floor rand rnd rndseed seedrnd sin sqr

### bbruntime (8)

apptitle debuglog delay end execfile millisecs runtimeerror systemproperty

### bbsockets (2)

closetcpstream opentcpstream

### bbstream (13)

eof readavail readbyte readfloat readint readline readshort readstring writebyte writefloat writeint 
writeline writestring

### bbstring (14)

asc chr currentdate currenttime hex instr left len lower mid replace right trim upper

### unknown (5)

errorlog texturebumpenvmat texturebumpenvoffset texturebumpenvscale texturelodbias

`unknown` = commands added in Blitz3D 1.106-1.108 that aren't in the open-source runtime snapshot
(`errorlog`, `texturebumpenv*`, `texturelodbias`). They can be stubs: bump-env mapping isn't used visibly and
`errorlog` is a debug aid.

## Compiler intrinsics (44)

Language-level operations the compiler emits calls for (not user-visible commands). The transpiler and the C++
support library (`bb::` namespace, see ARCHITECTURE.md) provide equivalents :

bbAbs bbDimArray bbFAbs bbFMod bbFPow bbFSgn bbFieldPtrAdd bbFltType bbIntType bbLoadLibs bbMod bbObjCompare 
bbObjDelete bbObjDeleteEach bbObjEachFirst bbObjEachNext bbObjFirst bbObjFromHandle bbObjInsBefore bbObjLast 
bbObjLoad bbObjNew bbObjNext bbObjPrev bbObjRelease bbObjStore bbObjToHandle bbReadInt bbReadStr bbRestore 
bbStrCompare bbStrConcat bbStrConst bbStrFromFloat bbStrFromInt bbStrLoad bbStrRelease bbStrStore bbStrToCStr 
bbStrToFloat bbStrToInt bbStrType bbUndimArray bbVecAlloc bbVecFree 

## External DLLs (via .decls)

| DLL | Used for | Wii U plan |
|---|---|---|
| `fmod.dll` (FMOD 3.75, `FSOUND_*`) | Streamed music (`FMod.bb`, `MusicPlayer`) | Own OGG streaming on the audio backend (stb_vorbis) |
| `blitzmovie.dll` | Startup intro AVIs | Skip, or re-encode to a format we can decode (low priority) |
| `zlibwapi.dll` | Zip API (`Blitz_File_ZipApi.bb`) | zlib/minizip from portlibs, or remove if only used by the updater |
| `kernel32.dll` | `GlobalMemoryStatus`, `GetModuleFileNameA` | Stubs |
| `user32.dll` | Window position/size (`fullscreen_window_fix.bb`) | Stubs (no windows on Wii U) |
