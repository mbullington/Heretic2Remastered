cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(Heretic2DOS LANGUAGES C CXX)
endif()

if(NOT DOS OR NOT DJGPP)
	message(FATAL_ERROR "DOS targets must be cross-compiled with SDL3's DJGPP toolchain file")
endif()

find_package(SDL3 CONFIG REQUIRED)

get_filename_component(H2_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(H2_DOS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/guest" CACHE PATH "Directory for DOS runtime artifacts")
set(H2_DOS_DEFINITIONS
	H2_DOS
	H2_STATIC_GAME
	H2_NO_FFMPEG
	H2_NO_NETWORK
	H2_NO_OGG
	H2_NO_OPENAL
	H2_NO_OPENGL)
set(H2_DOS_INCLUDES
	"${H2_REPO_ROOT}/include"
	"${H2_REPO_ROOT}/src/game"
	"${H2_REPO_ROOT}/src/qcommon"
	"${H2_REPO_ROOT}/src/H2Common"
	"${H2_REPO_ROOT}/src/Player"
	"${H2_REPO_ROOT}/src/client"
	"${H2_REPO_ROOT}/src/client/menus"
	"${H2_REPO_ROOT}/src/server"
	"${H2_REPO_ROOT}/src/win32"
	"${CMAKE_CURRENT_LIST_DIR}/ref_soft")

function(h2_dos_sources_from_manifest output manifest)
	file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/source-manifests/${manifest}" entries)
	set(inventory)
	set(sources)
	foreach(entry IN LISTS entries)
		if(entry MATCHES "^!")
			string(SUBSTRING "${entry}" 1 -1 source)
		else()
			set(source "${entry}")
			list(APPEND sources "${H2_REPO_ROOT}/${source}")
		endif()
		if(NOT EXISTS "${H2_REPO_ROOT}/${source}")
			message(FATAL_ERROR "DOS source manifest ${manifest} references missing ${source}")
		endif()
		list(APPEND inventory "${H2_REPO_ROOT}/${source}")
	endforeach()

	file(GLOB actual CONFIGURE_DEPENDS LIST_DIRECTORIES false ${ARGN})
	list(SORT actual)
	list(SORT inventory)
	if(NOT "${actual}" STREQUAL "${inventory}")
		message(FATAL_ERROR
			"DOS source manifest ${manifest} is stale. Review platform suitability and update the manifest explicitly.\n"
			"Manifest: ${inventory}\nCurrent: ${actual}")
	endif()
	set(${output} "${sources}" PARENT_SCOPE)
endfunction()

function(h2_dos_executable target)
	target_compile_features(${target} PRIVATE c_std_11)
	target_compile_definitions(${target} PRIVATE ${H2_DOS_DEFINITIONS})
	target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)
	target_link_libraries(${target} PRIVATE SDL3::SDL3-static)
	set_target_properties(${target} PROPERTIES
		OUTPUT_NAME "${target}"
		SUFFIX ".EXE"
		RUNTIME_OUTPUT_DIRECTORY "${H2_DOS_OUTPUT_DIR}")
endfunction()

add_executable(HELLODOS hellodos.c h2dos_ui.c)
h2_dos_executable(HELLODOS)

add_library(h2dos_core STATIC
	"${H2_REPO_ROOT}/src/H2Common/Common.c"
	"${H2_REPO_ROOT}/src/H2Common/Console.c"
	"${H2_REPO_ROOT}/src/H2Common/InfoStrings.c"
	"${H2_REPO_ROOT}/src/qcommon/cmd.c"
	"${H2_REPO_ROOT}/src/qcommon/cvar.c"
	"${H2_REPO_ROOT}/src/qcommon/files.c"
	h2dos_platform.c)
target_include_directories(h2dos_core PRIVATE ${H2_DOS_INCLUDES})
target_compile_definitions(h2dos_core PRIVATE ${H2_DOS_DEFINITIONS})
target_compile_options(h2dos_core PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)

add_executable(H2DOS h2dos.c h2dos_ui.c)
target_include_directories(H2DOS PRIVATE ${H2_DOS_INCLUDES})
target_link_libraries(H2DOS PRIVATE h2dos_core)
h2_dos_executable(H2DOS)

# Production engine closure. Keep these targets separate from the two bootstrap
# diagnostics so their source and runtime contracts remain unchanged.
h2_dos_sources_from_manifest(H2_DOS_H2COMMON_SOURCES h2common.txt
	"${H2_REPO_ROOT}/src/H2Common/*.c"
	"${H2_REPO_ROOT}/src/game/q_shared.c")
add_library(h2dos_H2Common STATIC ${H2_DOS_H2COMMON_SOURCES})

h2_dos_sources_from_manifest(H2_DOS_PLAYER_SOURCES player.txt
	"${H2_REPO_ROOT}/src/Player/*.c")
add_library(h2dos_Player STATIC ${H2_DOS_PLAYER_SOURCES})
target_compile_definitions(h2dos_Player PRIVATE
	PLAYER_DLL
	P_Init=H2R_Player_P_Init
	P_Shutdown=H2R_Player_P_Shutdown)

h2_dos_sources_from_manifest(H2_DOS_GAME_SOURCES game.txt
	"${H2_REPO_ROOT}/src/game/*.c"
	"${H2_REPO_ROOT}/src/game/*.cpp")
add_library(h2dos_gamex86 STATIC ${H2_DOS_GAME_SOURCES})
target_compile_definitions(h2dos_gamex86 PRIVATE GAME_DLL)

h2_dos_sources_from_manifest(H2_DOS_CLIENT_EFFECTS_SOURCES client-effects.txt
	"${H2_REPO_ROOT}/src/client effects/*.c")
add_library(h2dos_client_effects STATIC ${H2_DOS_CLIENT_EFFECTS_SOURCES})
target_compile_definitions(h2dos_client_effects PRIVATE GetfxAPI=H2R_ClientEffects_GetfxAPI)
target_include_directories(h2dos_client_effects PRIVATE "${H2_REPO_ROOT}/src/client effects")

foreach(module h2dos_H2Common h2dos_Player h2dos_gamex86 h2dos_client_effects)
	target_include_directories(${module} PRIVATE ${H2_DOS_INCLUDES})
	target_compile_definitions(${module} PRIVATE ${H2_DOS_DEFINITIONS})
	target_compile_options(${module} PRIVATE -Wall -Wextra -Wno-unknown-pragmas)
endforeach()
target_link_libraries(h2dos_Player PRIVATE h2dos_H2Common)
target_link_libraries(h2dos_gamex86 PRIVATE h2dos_H2Common)
target_link_libraries(h2dos_client_effects PRIVATE h2dos_H2Common)

add_library(h2dos_snd_sdl3 STATIC
	"${H2_REPO_ROOT}/src/snd_sdl3/src/snd_LowpassFilter.c"
	"${H2_REPO_ROOT}/src/snd_sdl3/src/snd_main.c"
	"${H2_REPO_ROOT}/src/snd_sdl3/src/snd_sdl3.c"
	"${H2_REPO_ROOT}/src/snd_sdl3/src/snd_wav.c")
target_include_directories(h2dos_snd_sdl3 PRIVATE
	${H2_DOS_INCLUDES}
	"${H2_REPO_ROOT}/src/snd_sdl3/src")
target_compile_definitions(h2dos_snd_sdl3 PRIVATE ${H2_DOS_DEFINITIONS})
target_compile_options(h2dos_snd_sdl3 PRIVATE -Wall -Wextra -Wpedantic -Wno-unknown-pragmas)
target_link_libraries(h2dos_snd_sdl3 PRIVATE h2dos_H2Common SDL3::SDL3-static)

set(H2_DOS_QCOMMON_SOURCES
	"${H2_REPO_ROOT}/src/qcommon/Debug.c"
	"${H2_REPO_ROOT}/src/qcommon/Message.c"
	"${H2_REPO_ROOT}/src/qcommon/Reference.c"
	"${H2_REPO_ROOT}/src/qcommon/Skeletons.c"
	"${H2_REPO_ROOT}/src/qcommon/anorms.c"
	"${H2_REPO_ROOT}/src/qcommon/cmd.c"
	"${H2_REPO_ROOT}/src/qcommon/cmodel.c"
	"${H2_REPO_ROOT}/src/qcommon/common.c"
	"${H2_REPO_ROOT}/src/qcommon/cvar.c"
	"${H2_REPO_ROOT}/src/qcommon/files.c"
	"${H2_REPO_ROOT}/src/qcommon/md4.c"
	"${H2_REPO_ROOT}/src/qcommon/net_chan.c"
	"${H2_REPO_ROOT}/src/qcommon/netmsg_read.c"
	"${H2_REPO_ROOT}/src/qcommon/netmsg_write.c"
	"${H2_REPO_ROOT}/src/qcommon/p_dll.c"
	"${H2_REPO_ROOT}/src/qcommon/pmove.c"
	"${H2_REPO_ROOT}/src/qcommon/static_libs.c"
	"${H2_REPO_ROOT}/src/qcommon/tokens.c"
	"${H2_REPO_ROOT}/src/qcommon/turbsin.c"
	"${H2_REPO_ROOT}/src/ref_gl1/src/anormtab.c")
h2_dos_sources_from_manifest(H2_DOS_CLIENT_SOURCES client.txt
	"${H2_REPO_ROOT}/src/client/*.c"
	"${H2_REPO_ROOT}/src/client/menus/*.c")
h2_dos_sources_from_manifest(H2_DOS_SERVER_SOURCES server.txt
	"${H2_REPO_ROOT}/src/server/*.c")

add_executable(SILVER
	h2dos_main.c
	sys_dos.c
	net_dos.c
	ref_soft/ref_soft_flex.c
	ref_soft/ref_soft_framebuffer.c
	ref_soft/ref_soft_image.c
	ref_soft/ref_soft_main.c
	ref_soft/ref_soft_model.c
	ref_soft/ref_soft_raster.c
	"${H2_REPO_ROOT}/include/libsmacker/smacker.c"
	${H2_DOS_QCOMMON_SOURCES}
	${H2_DOS_CLIENT_SOURCES}
	${H2_DOS_SERVER_SOURCES}
	"${H2_REPO_ROOT}/src/unix/cd_audio_stub.c"
	"${H2_REPO_ROOT}/src/unix/cl_mp4_stub.c"
	"${H2_REPO_ROOT}/src/win32/clfx_dll.c"
	"${H2_REPO_ROOT}/src/win32/snd_dll.c"
	"${H2_REPO_ROOT}/src/win32/vid_dll.c"
	"${H2_REPO_ROOT}/src/win32/vid_Screenshot.c")
target_include_directories(SILVER PRIVATE
	${H2_DOS_INCLUDES}
	"${H2_REPO_ROOT}/src/snd_sdl3/src")
target_compile_definitions(SILVER PRIVATE QUAKE2_DLL)
target_link_libraries(SILVER PRIVATE
	h2dos_gamex86
	h2dos_client_effects
	h2dos_snd_sdl3
	h2dos_Player
	h2dos_H2Common)
h2_dos_executable(SILVER)
set_target_properties(SILVER PROPERTIES LINKER_LANGUAGE CXX)
