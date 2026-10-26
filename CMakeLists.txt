cmake_minimum_required(VERSION 3.16)
project(flibustier VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
	set(FLIBUSTIER_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Engine core: data access and asset decoding, no raylib dependency, so
# headless tools and tests can use it.
add_library(flibustier_core STATIC
	src/data/exe.c
	src/data/iso9660.c
	src/data/pack.c
	src/data/sha256.c
	src/assets/font.c
	src/assets/fonts.c
	src/assets/image.c
	src/assets/img.c
	src/assets/pcx.c
	src/assets/digital.c
	src/assets/fmmusic.c
	src/audio/music.c
	src/audio/opl3.cpp
	src/external/ymfm/ymfm_opl.cpp
	src/external/ymfm/ymfm_adpcm.cpp
	src/external/ymfm/ymfm_pcm.cpp
	src/assets/pic.c
	src/assets/png.c
	src/assets/rsd.c
	src/assets/sprite.c
	src/assets/text.c
	src/assets/tileset.c
	src/assets/tiles.c
	src/assets/scenario.c
	src/assets/sheet.c
	src/assets/tiled.c
	src/assets/worldfiles.c
	src/assets/towns.c
	src/assets/world.c
	src/sim/battle.c
	src/sim/sword.c
	src/sim/land.c
	src/sim/treasure.c
	src/sim/mission.c
	src/sim/crew.c
	src/sim/prison.c
	src/sim/town.c
	src/sim/hof.c
	src/sim/json.c
	src/sim/proto.c
	src/sim/rng.c
	src/sim/save.c
	src/sim/sim.c
	src/gfx/fb.c
	src/gfx/sea.c
	src/gfx/text.c
	src/ui/game.c
	src/ui/screens.c
	src/ui/script.c
	src/view/view.c)
target_include_directories(flibustier_core PUBLIC src)
target_compile_options(flibustier_core PRIVATE ${FLIBUSTIER_WARNINGS})
# The vendored stb_image is not ours to keep warning-free.
set_source_files_properties(src/assets/png.c PROPERTIES COMPILE_OPTIONS -w)
# The vendored ymfm (an OPL3 emulator, C++14) neither: the only C++ in the
# engine, with its C wrapper.
set_source_files_properties(src/external/ymfm/ymfm_opl.cpp
	src/external/ymfm/ymfm_adpcm.cpp src/external/ymfm/ymfm_pcm.cpp
	PROPERTIES COMPILE_OPTIONS -w)
set_source_files_properties(src/audio/opl3.cpp PROPERTIES COMPILE_OPTIONS
	-Wno-unused-parameter)
set_property(TARGET flibustier_core PROPERTY CXX_STANDARD 14)

# FreeType draws the open pack's fonts: hardened for untrusted font files,
# which packs from third parties are.  Use a system FreeType when
# available, otherwise fetch a pinned release.
find_package(Freetype QUIET)
if(FREETYPE_FOUND)
	target_link_libraries(flibustier_core PRIVATE Freetype::Freetype)
else()
	message(STATUS "System FreeType not found, fetching FreeType 2.14.3")
	include(FetchContent)
	set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
	FetchContent_Declare(freetype
		GIT_REPOSITORY https://github.com/freetype/freetype.git
		GIT_TAG VER-2-14-3
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(freetype)
	target_link_libraries(flibustier_core PRIVATE freetype)
endif()
if(UNIX)
	target_link_libraries(flibustier_core PRIVATE m)
endif()

# Use a system raylib 5.5 or later when available (CMake config or
# pkg-config), otherwise fetch a pinned release.
set(FLIBUSTIER_RAYLIB raylib)
find_package(raylib QUIET)
if(raylib_FOUND AND raylib_VERSION AND raylib_VERSION VERSION_LESS 5.5)
	set(raylib_FOUND FALSE)
endif()
if(NOT raylib_FOUND)
	find_package(PkgConfig QUIET)
	if(PKG_CONFIG_FOUND)
		pkg_check_modules(RAYLIB QUIET IMPORTED_TARGET raylib>=5.5)
		if(RAYLIB_FOUND)
			set(FLIBUSTIER_RAYLIB PkgConfig::RAYLIB)
			set(raylib_FOUND TRUE)
		endif()
	endif()
endif()
if(NOT raylib_FOUND)
	message(STATUS "System raylib not found, fetching raylib 5.5")
	include(FetchContent)
	set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(raylib
		GIT_REPOSITORY https://github.com/raysan5/raylib.git
		GIT_TAG 5.5
		GIT_SHALLOW TRUE)
	FetchContent_MakeAvailable(raylib)
endif()

# Some raylib builds (FreeBSD raylib-6.0 package) omit libm and pthreads
# from their own dependencies, so link them explicitly.
find_package(Threads REQUIRED)
add_executable(flibustier src/main.c src/play.c src/crt.c src/viewer.c)
target_link_libraries(flibustier PRIVATE flibustier_core ${FLIBUSTIER_RAYLIB}
	Threads::Threads)
if(UNIX)
	target_link_libraries(flibustier PRIVATE m)
endif()
target_compile_options(flibustier PRIVATE ${FLIBUSTIER_WARNINGS})

# Headless simulation, driven by JSON lines (bots, AI agents, tests).
add_executable(flibustier-sim src/sim_main.c)
target_link_libraries(flibustier-sim PRIVATE flibustier_core)
target_compile_options(flibustier-sim PRIVATE ${FLIBUSTIER_WARNINGS})

# Tests.  test_original needs the game data: set FLIBUSTIER_DATA to the GOG
# DATA.DAT (or an install directory) in the environment, otherwise it is
# reported as skipped.
include(CTest)
if(BUILD_TESTING)
	foreach(t test_formats test_original test_sim test_ui)
		add_executable(${t} tests/${t}.c)
		target_link_libraries(${t} PRIVATE flibustier_core)
		target_compile_options(${t} PRIVATE ${FLIBUSTIER_WARNINGS})
		add_test(NAME ${t} COMMAND ${t})
	endforeach()
	set_tests_properties(test_original test_ui PROPERTIES
	    SKIP_RETURN_CODE 77)
	file(GLOB replays ${CMAKE_SOURCE_DIR}/tests/replays/*.jsonl)
	foreach(r ${replays})
		get_filename_component(name ${r} NAME_WE)
		add_test(NAME replay_${name} COMMAND ${CMAKE_COMMAND}
		    -DSIM=$<TARGET_FILE:flibustier-sim> -DREPLAY=${r}
		    -P ${CMAKE_SOURCE_DIR}/tests/replay.cmake)
		set_tests_properties(replay_${name} PROPERTIES
		    SKIP_REGULAR_EXPRESSION "SKIP: ")
	endforeach()
endif()
