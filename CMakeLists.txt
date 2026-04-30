cmake_minimum_required(VERSION 3.20)
project(Heretic2Remastered LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(H2_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build")
if(APPLE)
	set(H2_DYLIB_SUFFIX ".dylib")
elseif(WIN32)
	set(H2_DYLIB_SUFFIX ".dll")
else()
	set(H2_DYLIB_SUFFIX ".so")
endif()
file(MAKE_DIRECTORY "${H2_OUTPUT_DIR}" "${H2_OUTPUT_DIR}/base")

find_package(OpenGL REQUIRED)

option(H2_ENABLE_FFMPEG "Use FFmpeg/libavcodec for MP4/MKV cinematics on non-Apple Unix platforms when available" ON)
option(H2_STATIC_GAME "Build the engine, game, renderer, sound, player, and client effects into one executable" ON)

set(H2_MP4_SOURCE "${CMAKE_SOURCE_DIR}/src/unix/cl_mp4_stub.c")
set(H2_MP4_LIBS)
if(WIN32)
	set(H2_MP4_SOURCE "${CMAKE_SOURCE_DIR}/src/client/cl_mp4.c")
	list(APPEND H2_MP4_LIBS mf mfplat mfreadwrite mfuuid ole32)
elseif(APPLE)
	enable_language(OBJC)
	set(CMAKE_OBJC_STANDARD 11)
	set(CMAKE_OBJC_STANDARD_REQUIRED ON)
	set(CMAKE_OBJC_EXTENSIONS ON)

	find_library(H2_AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
	find_library(H2_AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
	find_library(H2_COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
	find_library(H2_COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
	find_library(H2_FOUNDATION_FRAMEWORK Foundation REQUIRED)
	set(H2_MP4_SOURCE "${CMAKE_SOURCE_DIR}/src/unix/cl_mp4_avfoundation.m")
	list(APPEND H2_MP4_LIBS
		"${H2_AVFOUNDATION_FRAMEWORK}"
		"${H2_AUDIOTOOLBOX_FRAMEWORK}"
		"${H2_COREMEDIA_FRAMEWORK}"
		"${H2_COREVIDEO_FRAMEWORK}"
		"${H2_FOUNDATION_FRAMEWORK}")
elseif(H2_ENABLE_FFMPEG)
	find_package(PkgConfig QUIET)
	if(PkgConfig_FOUND)
		pkg_check_modules(H2_FFMPEG QUIET IMPORTED_TARGET
			libavformat
			libavcodec
			libavutil
			libswscale)
		if(H2_FFMPEG_FOUND)
			set(H2_MP4_SOURCE "${CMAKE_SOURCE_DIR}/src/unix/cl_mp4_ffmpeg.c")
			list(APPEND H2_MP4_LIBS PkgConfig::H2_FFMPEG)
		endif()
	endif()
endif()

find_package(SDL3 CONFIG QUIET)
if(TARGET SDL3::SDL3)
	set(H2_SDL3_TARGET SDL3::SDL3)
else()
	find_library(H2_SDL3_LIBRARY NAMES SDL3 SDL3.lib libSDL3.dylib)
	if(NOT H2_SDL3_LIBRARY)
		file(GLOB H2_SDL3_CANDIDATES
			"/opt/homebrew/lib/libSDL3.dylib"
			"/usr/local/lib/libSDL3.dylib"
			"/nix/store/*sdl3*/lib/libSDL3.dylib")
		list(LENGTH H2_SDL3_CANDIDATES H2_SDL3_COUNT)
		if(H2_SDL3_COUNT GREATER 0)
			list(GET H2_SDL3_CANDIDATES 0 H2_SDL3_LIBRARY)
		endif()
	endif()
	if(NOT H2_SDL3_LIBRARY)
		message(FATAL_ERROR "SDL3 library not found. Install SDL3 (Homebrew: brew install sdl3) or set H2_SDL3_LIBRARY.")
	endif()
	add_library(SDL3::SDL3 UNKNOWN IMPORTED)
	set_target_properties(SDL3::SDL3 PROPERTIES IMPORTED_LOCATION "${H2_SDL3_LIBRARY}")
	set(H2_SDL3_TARGET SDL3::SDL3)
endif()

find_package(OpenAL QUIET)
if(TARGET OpenAL::OpenAL)
	set(H2_OPENAL_TARGET OpenAL::OpenAL)
else()
	find_library(H2_OPENAL_LIBRARY NAMES openal OpenAL)
	if(NOT H2_OPENAL_LIBRARY)
		file(GLOB H2_OPENAL_CANDIDATES
			"/opt/homebrew/lib/libopenal.dylib"
			"/usr/local/lib/libopenal.dylib"
			"/nix/store/*openal-soft*/lib/libopenal.dylib")
		list(LENGTH H2_OPENAL_CANDIDATES H2_OPENAL_COUNT)
		if(H2_OPENAL_COUNT GREATER 0)
			list(GET H2_OPENAL_CANDIDATES 0 H2_OPENAL_LIBRARY)
		endif()
	endif()
	if(NOT H2_OPENAL_LIBRARY)
		find_library(H2_OPENAL_LIBRARY OpenAL)
	endif()
	if(NOT H2_OPENAL_LIBRARY)
		message(FATAL_ERROR "OpenAL library not found. Install openal-soft or use macOS OpenAL.framework.")
	endif()
	add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
	set_target_properties(OpenAL::OpenAL PROPERTIES IMPORTED_LOCATION "${H2_OPENAL_LIBRARY}")
	set(H2_OPENAL_TARGET OpenAL::OpenAL)
endif()

set(H2_COMMON_INCLUDES
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/src/game"
	"${CMAKE_SOURCE_DIR}/src/qcommon"
	"${CMAKE_SOURCE_DIR}/src/H2Common"
	"${CMAKE_SOURCE_DIR}/src/Player"
	"${CMAKE_SOURCE_DIR}/src/client"
	"${CMAKE_SOURCE_DIR}/src/client/menus"
	"${CMAKE_SOURCE_DIR}/src/server"
	"${CMAKE_SOURCE_DIR}/src/win32"
)

function(h2_common_target target)
	target_include_directories(${target} PRIVATE ${H2_COMMON_INCLUDES})
	target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
	if(H2_STATIC_GAME)
		target_compile_definitions(${target} PRIVATE H2_STATIC_GAME)
	endif()
	if(APPLE)
		target_compile_options(${target} PRIVATE
			-Wno-deprecated-declarations
			-Wno-unused-value
			-Wno-microsoft-anon-tag
			-Wno-incompatible-function-pointer-types
			-Wno-nonportable-include-path)
	endif()
endfunction()

function(h2_plugin_target target output_name)
	set_target_properties(${target} PROPERTIES
		PREFIX ""
		SUFFIX "${H2_DYLIB_SUFFIX}"
		OUTPUT_NAME "${output_name}"
		LIBRARY_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
		BUILD_WITH_INSTALL_RPATH ON
		INSTALL_RPATH "@loader_path")
	if(APPLE)
		target_link_options(${target} PRIVATE "-Wl,-undefined,dynamic_lookup")
	endif()
endfunction()

# Common Raven/Heretic support library.
file(GLOB H2COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/H2Common/*.c")
list(REMOVE_ITEM H2COMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/H2Common/DllMain.c")
set(H2_COMMON_LIBRARY_TYPE SHARED)
if(H2_STATIC_GAME)
	set(H2_COMMON_LIBRARY_TYPE STATIC)
endif()
add_library(H2Common ${H2_COMMON_LIBRARY_TYPE}
	"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
	${H2COMMON_SOURCES})
h2_common_target(H2Common)
target_compile_definitions(H2Common PRIVATE H2COMMON)
if(NOT H2_STATIC_GAME)
	set_target_properties(H2Common PROPERTIES
		PREFIX ""
		SUFFIX "${H2_DYLIB_SUFFIX}"
		LIBRARY_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
		RUNTIME_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
		BUILD_WITH_INSTALL_RPATH ON
		INSTALL_RPATH "@loader_path")
endif()

# Player logic plugin.
file(GLOB PLAYER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Player/*.c")
if(H2_STATIC_GAME)
	add_library(Player STATIC ${PLAYER_SOURCES})
else()
	add_library(Player MODULE ${PLAYER_SOURCES})
endif()
h2_common_target(Player)
target_compile_definitions(Player PRIVATE PLAYER_DLL)
if(H2_STATIC_GAME)
	target_compile_definitions(Player PRIVATE P_Init=H2R_Player_P_Init P_Shutdown=H2R_Player_P_Shutdown)
endif()
target_link_libraries(Player PRIVATE H2Common)
if(NOT H2_STATIC_GAME)
	h2_plugin_target(Player "Player")
endif()

# Game logic plugin.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/game/*.c"
	"${CMAKE_SOURCE_DIR}/src/game/*.cpp")
if(H2_STATIC_GAME)
	list(REMOVE_ITEM GAME_SOURCES "${CMAKE_SOURCE_DIR}/src/game/q_shared.c")
	add_library(gamex86 STATIC ${GAME_SOURCES})
else()
	set(H2_GAME_PLUGIN_LOADER_SOURCE)
	if(NOT WIN32)
		set(H2_GAME_PLUGIN_LOADER_SOURCE "${CMAKE_SOURCE_DIR}/src/unix/dlopen_unix.c")
	endif()
	add_library(gamex86 MODULE
		"${CMAKE_SOURCE_DIR}/src/qcommon/Message.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/p_dll.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/Skeletons.c"
		${H2_GAME_PLUGIN_LOADER_SOURCE}
		${GAME_SOURCES})
endif()
h2_common_target(gamex86)
target_compile_definitions(gamex86 PRIVATE GAME_DLL)
target_link_libraries(gamex86 PRIVATE H2Common)
if(NOT H2_STATIC_GAME AND NOT WIN32)
	target_link_libraries(gamex86 PRIVATE ${CMAKE_DL_LIBS})
endif()
if(NOT H2_STATIC_GAME)
	h2_plugin_target(gamex86 "gamex86")
endif()

# Client effects plugin.
file(GLOB CLIENT_EFFECTS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/client effects/*.c")
if(H2_STATIC_GAME)
	add_library(client_effects STATIC ${CLIENT_EFFECTS_SOURCES})
else()
	add_library(client_effects MODULE
		"${CMAKE_SOURCE_DIR}/src/qcommon/anorms.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/Message.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/netmsg_read.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/turbsin.c"
		${CLIENT_EFFECTS_SOURCES})
endif()
h2_common_target(client_effects)
target_include_directories(client_effects PRIVATE "${CMAKE_SOURCE_DIR}/src/client effects")
if(H2_STATIC_GAME)
	target_compile_definitions(client_effects PRIVATE GetfxAPI=H2R_ClientEffects_GetfxAPI)
endif()
target_link_libraries(client_effects PRIVATE H2Common)
if(NOT H2_STATIC_GAME)
	h2_plugin_target(client_effects "Client Effects")
endif()

# OpenGL 3 renderer plugin.
file(GLOB REF_GL3_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/*.c"
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/Skeletons/*.c")
if(H2_STATIC_GAME)
	add_library(ref_gl3 STATIC
		"${CMAKE_SOURCE_DIR}/src/qcommon/turbsin.c"
		${REF_GL3_SOURCES})
else()
	add_library(ref_gl3 MODULE
		"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/anorms.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/Reference.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/Skeletons.c"
		"${CMAKE_SOURCE_DIR}/src/qcommon/turbsin.c"
		${REF_GL3_SOURCES})
endif()
h2_common_target(ref_gl3)
if(H2_STATIC_GAME)
	target_compile_definitions(ref_gl3 PRIVATE turbsin=gl3_turbsin CreateSkeleton=GL3_CreateSkeleton)
endif()
target_include_directories(ref_gl3 PRIVATE
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src"
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/Skeletons")
target_link_libraries(ref_gl3 PRIVATE H2Common ${H2_SDL3_TARGET} OpenGL::GL)
if(NOT H2_STATIC_GAME)
	h2_plugin_target(ref_gl3 "ref_gl3")
endif()

# SDL3/OpenAL sound plugin.
file(GLOB SND_SDL3_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/snd_sdl3/src/*.c")
if(H2_STATIC_GAME)
	add_library(snd_sdl3 STATIC ${SND_SDL3_SOURCES})
else()
	add_library(snd_sdl3 MODULE ${SND_SDL3_SOURCES})
endif()
h2_common_target(snd_sdl3)
target_include_directories(snd_sdl3 PRIVATE "${CMAKE_SOURCE_DIR}/src/snd_sdl3/src")
target_link_libraries(snd_sdl3 PRIVATE H2Common ${H2_SDL3_TARGET} ${H2_OPENAL_TARGET})
if(NOT H2_STATIC_GAME)
	h2_plugin_target(snd_sdl3 "snd_sdl3")
endif()

# Main executable.
file(GLOB CLIENT_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/client/*.c"
	"${CMAKE_SOURCE_DIR}/src/client/menus/*.c")
list(REMOVE_ITEM CLIENT_SOURCES "${CMAKE_SOURCE_DIR}/src/client/cl_mp4.c")
file(GLOB QCOMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/qcommon/*.c")
list(REMOVE_ITEM QCOMMON_SOURCES "${CMAKE_SOURCE_DIR}/src/qcommon/static_libs.c")
set(H2_LOADER_SOURCE)
if(H2_STATIC_GAME)
	set(H2_LOADER_SOURCE "${CMAKE_SOURCE_DIR}/src/qcommon/static_libs.c")
elseif(NOT WIN32)
	set(H2_LOADER_SOURCE "${CMAKE_SOURCE_DIR}/src/unix/dlopen_unix.c")
endif()
if(WIN32)
	set(H2_PLATFORM_SOURCES
		"${CMAKE_SOURCE_DIR}/src/launcher/main.c"
		"${CMAKE_SOURCE_DIR}/src/win32/cd_audio.c"
		"${CMAKE_SOURCE_DIR}/src/win32/net_wins.c"
		"${CMAKE_SOURCE_DIR}/src/win32/q_shwin.c"
		"${CMAKE_SOURCE_DIR}/src/win32/sys_win.c"
		${H2_LOADER_SOURCE})
else()
	set(H2_PLATFORM_SOURCES
		"${CMAKE_SOURCE_DIR}/src/unix/cd_audio_stub.c"
		${H2_LOADER_SOURCE}
		"${CMAKE_SOURCE_DIR}/src/unix/net_udp.c"
		"${CMAKE_SOURCE_DIR}/src/unix/sys_unix.c")
endif()
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/server/*.c")
set(H2_EXECUTABLE_TYPE)
if(WIN32)
	set(H2_EXECUTABLE_TYPE WIN32)
endif()
add_executable(Heretic2R ${H2_EXECUTABLE_TYPE}
	"${CMAKE_SOURCE_DIR}/include/libsmacker/smacker.c"
	${CLIENT_SOURCES}
	${H2_MP4_SOURCE}
	"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
	${QCOMMON_SOURCES}
	${SERVER_SOURCES}
	"${CMAKE_SOURCE_DIR}/src/win32/clfx_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/snd_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/vid_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/vid_Screenshot.c"
	${H2_PLATFORM_SOURCES})
h2_common_target(Heretic2R)
target_compile_definitions(Heretic2R PRIVATE QUAKE2_DLL)
if(WIN32)
	list(APPEND H2_PLATFORM_LIBS winmm wsock32)
elseif(NOT H2_STATIC_GAME)
	list(APPEND H2_PLATFORM_LIBS ${CMAKE_DL_LIBS})
endif()
if(H2_STATIC_GAME)
	target_link_libraries(Heretic2R PRIVATE gamex86 client_effects ref_gl3 snd_sdl3 Player H2Common ${H2_SDL3_TARGET} ${H2_OPENAL_TARGET} OpenGL::GL ${H2_MP4_LIBS} ${H2_PLATFORM_LIBS})
else()
	target_link_libraries(Heretic2R PRIVATE H2Common ${H2_SDL3_TARGET} OpenGL::GL ${H2_MP4_LIBS} ${H2_PLATFORM_LIBS})
endif()
set_target_properties(Heretic2R PROPERTIES
	ENABLE_EXPORTS ON
	RUNTIME_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
	BUILD_WITH_INSTALL_RPATH ON
	INSTALL_RPATH "@executable_path")

if(H2_STATIC_GAME)
	add_custom_target(H2RemovePluginLibraries ALL
		COMMAND ${CMAKE_COMMAND} -E rm -f
			"${H2_OUTPUT_DIR}/H2Common${H2_DYLIB_SUFFIX}"
			"${H2_OUTPUT_DIR}/Player${H2_DYLIB_SUFFIX}"
			"${H2_OUTPUT_DIR}/gamex86${H2_DYLIB_SUFFIX}"
			"${H2_OUTPUT_DIR}/Client Effects${H2_DYLIB_SUFFIX}"
			"${H2_OUTPUT_DIR}/ref_gl3${H2_DYLIB_SUFFIX}"
			"${H2_OUTPUT_DIR}/snd_sdl3${H2_DYLIB_SUFFIX}")
	add_dependencies(Heretic2R H2RemovePluginLibraries)
endif()

