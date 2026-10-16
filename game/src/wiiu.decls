.lib "wiiu"

; Wii U port : native helpers (runtime/userlibs/userlibs_stubs.cpp, runtime/userlibs/wiiu_rmesh.cpp)

; Length bytes of a bank from offset, as a string (one allocation instead of one per character)
WiiU_PeekString$(bank*,offset%,length%):"WiiU_PeekString"

; LoadRMesh : count vertices / triangles read from a file into a surface in one go (see wiiu_rmesh.cpp)
WiiU_ReadRMeshVertices(surf%,file%,count%,full%):"WiiU_ReadRMeshVertices"
WiiU_ReadRMeshTriangles(surf%,file%,count%,both%):"WiiU_ReadRMeshTriangles"
; Adds every triangle of a mesh again reversed, reusing its vertices (instead of CopyMesh + FlipMesh + AddMesh)
WiiU_MakeDoubleSided(mesh%):"WiiU_MakeDoubleSided"

; Text boxes : the system software keyboard (blocks until OK / Cancel); returns the new text, or text on Cancel
WiiU_TextInput$(text$,hint$,maxLength%):"WiiU_TextInput"

; GamePad HUD (runtime/gxruntime/gxgraphics.cpp "GamePad HUD") : the HUD drawn after WiiU_HUDBegin is drawn again
; bigger on the GamePad (the TV keeps the normal size). Parts scale about an anchor : WiiU_HUDAnchor(fx#, fy#) = a
; point as fractions of the screen (0.5, 0.5 : the centre, the default), fx < 0 = each part's own centre (call it
; before each part); WiiU_HUDPointer = the mouse pointer (follows the centre); WiiU_HUDPlain = not scaled (menus)
WiiU_HUDBegin():"WiiU_HUDBegin"
WiiU_HUDAnchor(fx#,fy#):"WiiU_HUDAnchor"
WiiU_HUDPointer():"WiiU_HUDPointer"
WiiU_HUDPlain():"WiiU_HUDPlain"
; GamePad HUD size (1 = as on the TV)
WiiU_HUDScale(scale#):"WiiU_HUDScale"
; Brightness ("screen gamma", 0..2), applied when the picture is presented
WiiU_ScreenGamma(gamma#):"WiiU_ScreenGamma"
