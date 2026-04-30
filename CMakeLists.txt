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
file(MAKE_DIRECTORY "${H2_OUTPUT_DIR}" "${H2_OUTPUT_DIR}/base")

find_package(OpenGL REQUIRED)

find_package(SDL3 CONFIG QUIET)
if(TARGET SDL3::SDL3)
	set(H2_SDL3_TARGET SDL3::SDL3)
else()
	find_library(H2_SDL3_LIBRARY NAMES SDL3 libSDL3.dylib)
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
		SUFFIX ".dylib"
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
add_library(H2Common SHARED
	"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
	${H2COMMON_SOURCES})
h2_common_target(H2Common)
target_compile_definitions(H2Common PRIVATE H2COMMON)
set_target_properties(H2Common PROPERTIES
	PREFIX ""
	SUFFIX ".dylib"
	LIBRARY_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
	RUNTIME_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
	BUILD_WITH_INSTALL_RPATH ON
	INSTALL_RPATH "@loader_path")

# Player logic plugin.
file(GLOB PLAYER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Player/*.c")
add_library(Player MODULE ${PLAYER_SOURCES})
h2_common_target(Player)
target_compile_definitions(Player PRIVATE PLAYER_DLL)
target_link_libraries(Player PRIVATE H2Common)
h2_plugin_target(Player "Player")

# Game logic plugin.
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/game/*.c"
	"${CMAKE_SOURCE_DIR}/src/game/*.cpp")
add_library(gamex86 MODULE
	"${CMAKE_SOURCE_DIR}/src/qcommon/Message.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/p_dll.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/Skeletons.c"
	"${CMAKE_SOURCE_DIR}/src/unix/dlopen_unix.c"
	${GAME_SOURCES})
h2_common_target(gamex86)
target_compile_definitions(gamex86 PRIVATE GAME_DLL)
target_link_libraries(gamex86 PRIVATE H2Common)
h2_plugin_target(gamex86 "gamex86")

# Client effects plugin.
file(GLOB CLIENT_EFFECTS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/client effects/*.c")
add_library(client_effects MODULE
	"${CMAKE_SOURCE_DIR}/src/qcommon/anorms.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/Message.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/netmsg_read.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/turbsin.c"
	${CLIENT_EFFECTS_SOURCES})
h2_common_target(client_effects)
target_include_directories(client_effects PRIVATE "${CMAKE_SOURCE_DIR}/src/client effects")
target_link_libraries(client_effects PRIVATE H2Common)
h2_plugin_target(client_effects "Client Effects")

# OpenGL 3 renderer plugin.
file(GLOB REF_GL3_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/*.c"
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/Skeletons/*.c")
add_library(ref_gl3 MODULE
	"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/anorms.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/Reference.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/Skeletons.c"
	"${CMAKE_SOURCE_DIR}/src/qcommon/turbsin.c"
	${REF_GL3_SOURCES})
h2_common_target(ref_gl3)
target_include_directories(ref_gl3 PRIVATE
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src"
	"${CMAKE_SOURCE_DIR}/src/ref_gl3/src/Skeletons")
target_link_libraries(ref_gl3 PRIVATE H2Common ${H2_SDL3_TARGET} OpenGL::GL)
h2_plugin_target(ref_gl3 "ref_gl3")

# SDL3/OpenAL sound plugin.
file(GLOB SND_SDL3_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/snd_sdl3/src/*.c")
add_library(snd_sdl3 MODULE ${SND_SDL3_SOURCES})
h2_common_target(snd_sdl3)
target_include_directories(snd_sdl3 PRIVATE "${CMAKE_SOURCE_DIR}/src/snd_sdl3/src")
target_link_libraries(snd_sdl3 PRIVATE H2Common ${H2_SDL3_TARGET} ${H2_OPENAL_TARGET})
h2_plugin_target(snd_sdl3 "snd_sdl3")

# Main executable.
file(GLOB CLIENT_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/client/*.c"
	"${CMAKE_SOURCE_DIR}/src/client/menus/*.c")
list(REMOVE_ITEM CLIENT_SOURCES "${CMAKE_SOURCE_DIR}/src/client/cl_mp4.c")
file(GLOB QCOMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/qcommon/*.c")
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/server/*.c")
add_executable(Heretic2R
	"${CMAKE_SOURCE_DIR}/include/libsmacker/smacker.c"
	${CLIENT_SOURCES}
	"${CMAKE_SOURCE_DIR}/src/unix/cl_mp4_stub.c"
	"${CMAKE_SOURCE_DIR}/src/game/q_shared.c"
	${QCOMMON_SOURCES}
	${SERVER_SOURCES}
	"${CMAKE_SOURCE_DIR}/src/win32/clfx_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/snd_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/vid_dll.c"
	"${CMAKE_SOURCE_DIR}/src/win32/vid_Screenshot.c"
	"${CMAKE_SOURCE_DIR}/src/unix/cd_audio_stub.c"
	"${CMAKE_SOURCE_DIR}/src/unix/dlopen_unix.c"
	"${CMAKE_SOURCE_DIR}/src/unix/net_udp.c"
	"${CMAKE_SOURCE_DIR}/src/unix/sys_unix.c")
h2_common_target(Heretic2R)
target_compile_definitions(Heretic2R PRIVATE QUAKE2_DLL)
target_link_libraries(Heretic2R PRIVATE H2Common ${H2_SDL3_TARGET} OpenGL::GL)
set_target_properties(Heretic2R PROPERTIES
	ENABLE_EXPORTS ON
	RUNTIME_OUTPUT_DIRECTORY "${H2_OUTPUT_DIR}"
	BUILD_WITH_INSTALL_RPATH ON
	INSTALL_RPATH "@executable_path")

