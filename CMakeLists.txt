cmake_minimum_required(VERSION 3.16)
project(stmalo VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Use a system raylib when available, otherwise fetch a pinned release.
find_package(raylib 5.5 QUIET)
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

add_executable(stmalo src/main.c)
target_link_libraries(stmalo PRIVATE raylib)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(stmalo PRIVATE -Wall -Wextra -Wpedantic)
endif()
