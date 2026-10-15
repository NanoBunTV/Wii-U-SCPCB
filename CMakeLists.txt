# SCP: Containment Breach for Wii U - top-level build
#
# Configure with the devkitPro Wii U toolchain; on Windows use build.ps1 (see Docs/BUILDING.md).

cmake_minimum_required(VERSION 3.20)

project(SCPCBWiiU C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
	message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake (or run build.ps1)")
endif()

set(APP_NAME       "SCP: Containment Breach")
set(APP_SHORT_NAME "SCP:CB")
set(APP_AUTHOR     "Undertow Games / Wii U port")
# Home Menu icon and boot screens (meta/ : the originals icon.png / boot.png, resized to the Wii U sizes)
set(APP_ICON       "${CMAKE_SOURCE_DIR}/meta/icon_128.png")
set(APP_TV_SPLASH  "${CMAKE_SOURCE_DIR}/meta/boot_tv.png")
set(APP_DRC_SPLASH "${CMAKE_SOURCE_DIR}/meta/boot_drc.png")
set(APP_VERSION    "0.0.1")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wno-unused-parameter -fno-strict-aliasing)

# Folder packed into the .wuhb; readable at run time under fs:/vol/content/
set(CONTENT_DIR "${CMAKE_BINARY_DIR}/content")
file(MAKE_DIRECTORY "${CONTENT_DIR}")
file(WRITE "${CONTENT_DIR}/hello.txt" "content folder works (v${APP_VERSION})\n")

# Shaders : GLSL compiled offline to .gsh with CafeGLSL (see Docs/BUILDING.md), loaded from content/shaders/
set(CAFEGLSL "${CMAKE_SOURCE_DIR}/_ref/tools/cafeglsl/bin/glslcompiler.exe" CACHE FILEPATH "CafeGLSL glslcompiler")
if(NOT EXISTS "${CAFEGLSL}")
	message(FATAL_ERROR "CafeGLSL not found at ${CAFEGLSL} (see Docs/BUILDING.md)")
endif()
file(MAKE_DIRECTORY "${CONTENT_DIR}/shaders")
set(SHADER_OUTPUTS)
function(add_shader_pair name vert frag)
	set(out "${CONTENT_DIR}/shaders/${name}.gsh")
	add_custom_command(
		OUTPUT "${out}"
		COMMAND "${CAFEGLSL}" -vs "${vert}" -ps "${frag}" -o "${out}" --uniform-mode register
		DEPENDS "${vert}" "${frag}"
		COMMENT "Compiling shader ${name}"
		VERBATIM)
	set(SHADER_OUTPUTS ${SHADER_OUTPUTS} "${out}" PARENT_SCOPE)
endfunction()
add_shader_pair(scene "${CMAKE_SOURCE_DIR}/shaders/scene.vert" "${CMAKE_SOURCE_DIR}/shaders/scene.frag")
# Scene pixel shader variants with the 4 texture stage blend ops fixed (scene_<ops>.gsh, see scene.frag and
# gxScene::pickVariant) : the states that cover almost all triangles drawn (measured in the first cell / rooms)
set(SCENE_VARIANTS 5635 5350 2500 2000 0000)
file(READ "${CMAKE_SOURCE_DIR}/shaders/scene.frag" SCENE_FRAG_SRC)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/shaders/scene.frag")
foreach(ops ${SCENE_VARIANTS})
	string(SUBSTRING "${ops}" 0 1 op0)
	string(SUBSTRING "${ops}" 1 1 op1)
	string(SUBSTRING "${ops}" 2 1 op2)
	string(SUBSTRING "${ops}" 3 1 op3)
	string(REPLACE "#version 330" "#version 330\n#define OP0 ${op0}\n#define OP1 ${op1}\n#define OP2 ${op2}\n#define OP3 ${op3}"
		variant_src "${SCENE_FRAG_SRC}")
	file(WRITE "${CMAKE_BINARY_DIR}/shaders/scene_${ops}.frag" "${variant_src}")
	add_shader_pair(scene_${ops} "${CMAKE_SOURCE_DIR}/shaders/scene.vert" "${CMAKE_BINARY_DIR}/shaders/scene_${ops}.frag")
endforeach()
add_shader_pair(canvas2d "${CMAKE_SOURCE_DIR}/shaders/canvas2d.vert" "${CMAKE_SOURCE_DIR}/shaders/canvas2d.frag")
add_custom_target(shaders DEPENDS ${SHADER_OUTPUTS})

# Game assets copied into the .wuhb content folder (from the SCP:CB 1.3.11 release in _ref/game).
# SCPCB_ASSET_DIRS lists the folders (relative to the game folder); trimming it speeds up test builds.
set(SCPCB_GAME_DIR "${CMAKE_SOURCE_DIR}/_ref/game" CACHE PATH "SCP:CB 1.3.11 game folder (assets)")
set(SCPCB_ASSET_DIRS "GFX;SFX;Data;Loadingscreens" CACHE STRING "Asset folders to bundle")
set(ASSET_COMMANDS)
foreach(dir ${SCPCB_ASSET_DIRS})
	list(APPEND ASSET_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
		"${SCPCB_GAME_DIR}/${dir}" "${CONTENT_DIR}/${dir}")
endforeach()
# the Wii U settings file (launcher, update check and startup video off; 1280x720)
list(APPEND ASSET_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different
	"${CMAKE_SOURCE_DIR}/game/options.ini" "${CONTENT_DIR}/options.ini")
# binary / compressed .x models -> text .x (the Wii U .x loader reads text only)
find_program(SCPCB_PYTHON NAMES py python3 python DOC "Python 3 for the asset tools")
if(NOT SCPCB_PYTHON)
	message(FATAL_ERROR "Python 3 not found (asset tools); set SCPCB_PYTHON")
endif()
list(APPEND ASSET_COMMANDS COMMAND "${SCPCB_PYTHON}" -I "${CMAKE_SOURCE_DIR}/tools/convert_x.py" "${CONTENT_DIR}")
# textures -> BC1 / BC3 sidecar files (tools/texconv); 2D-only folders are skipped
set(TEXCONV "${CMAKE_SOURCE_DIR}/tools/texconv/texconv.exe" CACHE FILEPATH "texconv (tools/texconv/build.sh)")
if(NOT EXISTS "${TEXCONV}")
	message(FATAL_ERROR "texconv not built : run tools/texconv/build.sh (see Docs/BUILDING.md)")
endif()
list(APPEND ASSET_COMMANDS COMMAND "${TEXCONV}" "${CONTENT_DIR}" gfx/menu gfx/font loadingscreens shaders)
list(APPEND ASSET_COMMANDS COMMAND "${CMAKE_COMMAND}" "-DCONTENT_DIR=${CONTENT_DIR}" -P "${CMAKE_SOURCE_DIR}/tools/gen_filelist.cmake")
# the folder packed into the .wuhb : content with the map assets zstd-compressed (tools/pack_content.py)
set(PACKED_CONTENT_DIR "${CMAKE_BINARY_DIR}/content_wuhb")
file(MAKE_DIRECTORY "${PACKED_CONTENT_DIR}")
find_program(ZSTD_EXE zstd HINTS "$ENV{DEVKITPRO}/msys2/usr/bin" "F:/Programs/devkitPro/msys2/usr/bin"
	DOC "zstd command line tool (devkitPro's msys2 has one)")
if(NOT ZSTD_EXE)
	message(FATAL_ERROR "zstd not found; set ZSTD_EXE")
endif()
list(APPEND ASSET_COMMANDS COMMAND "${SCPCB_PYTHON}" -I "${CMAKE_SOURCE_DIR}/tools/pack_content.py"
	"${CONTENT_DIR}" "${PACKED_CONTENT_DIR}" "${ZSTD_EXE}")
add_custom_target(assets ${ASSET_COMMANDS} COMMENT "Copying game assets: ${SCPCB_ASSET_DIRS}" VERBATIM)
add_dependencies(assets shaders)	# the file list includes the compiled shaders

# Wii U platform layer (replaces Blitz3D's Win32 / DirectX gxruntime) + Blitz3D's stdutil
file(GLOB GXRUNTIME_SOURCES CONFIGURE_DEPENDS runtime/gxruntime/*.cpp)
add_library(gxruntime STATIC ${GXRUNTIME_SOURCES} runtime/stdutil/stdutil.cpp)
target_include_directories(gxruntime PUBLIC runtime/gxruntime runtime/thirdparty/stb)
target_compile_options(gxruntime PRIVATE -Wno-sign-compare -Wno-unused-variable)
# compressed content files : zstd from devkitPro's portlibs (ppc-zstd)
target_include_directories(gxruntime PRIVATE "${DEVKITPRO}/portlibs/ppc/include")
target_link_libraries(gxruntime PUBLIC "${DEVKITPRO}/portlibs/ppc/lib/libzstd.a")

# Blitz3D scene graph (original source, ported)
file(GLOB BLITZ3D_SOURCES CONFIGURE_DEPENDS runtime/blitz3d/*.cpp)
add_library(blitz3d STATIC ${BLITZ3D_SOURCES})
target_include_directories(blitz3d PUBLIC runtime/blitz3d)
target_link_libraries(blitz3d PUBLIC gxruntime)
target_compile_options(blitz3d PRIVATE -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-reorder
	-Wno-misleading-indentation -Wno-parentheses -Wno-class-memaccess -Wno-deprecated-declarations)

# Blitz runtime library : the commands compiled Blitz code calls (original source, ported)
file(GLOB BBRUNTIME_SOURCES CONFIGURE_DEPENDS runtime/bbruntime/*.cpp)
add_library(bbruntime STATIC ${BBRUNTIME_SOURCES})
target_include_directories(bbruntime PUBLIC runtime/bbruntime)
target_compile_definitions(bbruntime PUBLIC PRO)
target_link_libraries(bbruntime PUBLIC blitz3d gxruntime)
target_compile_options(bbruntime PRIVATE -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable -Wno-reorder
	-Wno-misleading-indentation -Wno-parentheses -Wno-class-memaccess -Wno-deprecated-declarations -Wno-write-strings)

add_executable(scpcb
	source/main.cpp
)
target_link_libraries(scpcb PRIVATE bbruntime blitz3d gxruntime)
add_dependencies(scpcb shaders assets)
target_include_directories(scpcb PRIVATE source)

target_compile_definitions(scpcb PRIVATE APP_VERSION="${APP_VERSION}")

wut_create_rpx(scpcb)
wut_create_wuhb(scpcb
	CONTENT   "${PACKED_CONTENT_DIR}"
	NAME      "${APP_NAME} (room viewer)"
	SHORTNAME "${APP_SHORT_NAME}"
	AUTHOR    "${APP_AUTHOR}"
	ICON      "${APP_ICON}"
	TVSPLASH  "${APP_TV_SPLASH}"
	DRCSPLASH "${APP_DRC_SPLASH}"
)

# ---- The game : BlitzBasic transpiled to C++ by tools/bb2cpp (at configure time) ----
set(BB2CPP "${CMAKE_SOURCE_DIR}/tools/bb2cpp/bb2cpp.exe" CACHE FILEPATH "bb2cpp transpiler (tools/bb2cpp/build.sh)")
set(SCPCB_SRC_DIR "${CMAKE_SOURCE_DIR}/game/src" CACHE PATH "SCP:CB BlitzBasic source with the Wii U changes")
set(GAME_GEN_DIR "${CMAKE_BINARY_DIR}/game")
if(NOT EXISTS "${BB2CPP}")
	message(FATAL_ERROR "bb2cpp not built : run tools/bb2cpp/build.sh (see Docs/BUILDING.md)")
endif()
file(GLOB SCPCB_BB_SOURCES "${SCPCB_SRC_DIR}/*.bb")
file(GLOB SCPCB_DECLS "${SCPCB_SRC_DIR}/*.decls")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	${SCPCB_BB_SOURCES} ${SCPCB_DECLS} "${BB2CPP}" "${CMAKE_SOURCE_DIR}/tools/bbapi.txt")
file(REMOVE_RECURSE "${GAME_GEN_DIR}")
execute_process(
	COMMAND "${BB2CPP}" "${SCPCB_SRC_DIR}/Main.bb" "${GAME_GEN_DIR}" "${CMAKE_SOURCE_DIR}/tools/bbapi.txt" ${SCPCB_DECLS}
	RESULT_VARIABLE BB2CPP_RESULT
	OUTPUT_VARIABLE BB2CPP_OUTPUT
	ERROR_VARIABLE BB2CPP_ERROR)
if(NOT BB2CPP_RESULT EQUAL 0)
	message(FATAL_ERROR "bb2cpp failed:\n${BB2CPP_ERROR}")
endif()
message(STATUS "${BB2CPP_OUTPUT}")
file(GLOB GAME_GEN_SOURCES "${GAME_GEN_DIR}/*.cpp")
file(GLOB USERLIB_SOURCES CONFIGURE_DEPENDS runtime/userlibs/*.cpp)

add_executable(scpcb_game source/game_main.cpp ${GAME_GEN_SOURCES} ${USERLIB_SOURCES})
target_include_directories(scpcb_game PRIVATE "${GAME_GEN_DIR}" source)
target_link_libraries(scpcb_game PRIVATE bbruntime blitz3d gxruntime)
# exact Blitz semantics : int overflow wraps, no fused multiply-add (x87 single precision results)
target_compile_options(scpcb_game PRIVATE -fwrapv -ffp-contract=off -w)
add_dependencies(scpcb_game shaders assets)
wut_create_rpx(scpcb_game)
wut_create_wuhb(scpcb_game
	CONTENT   "${PACKED_CONTENT_DIR}"
	NAME      "${APP_NAME}"
	SHORTNAME "${APP_SHORT_NAME}"
	AUTHOR    "${APP_AUTHOR}"
	ICON      "${APP_ICON}"
	TVSPLASH  "${APP_TV_SPLASH}"
	DRCSPLASH "${APP_DRC_SPLASH}"
)
