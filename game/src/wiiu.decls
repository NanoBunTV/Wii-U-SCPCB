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
