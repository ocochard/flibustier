# Cross-compile a 64-bit Windows build with clang and an llvm-mingw
# release (https://github.com/mstorsjo/llvm-mingw) as the MinGW-w64 sysroot.
#
# cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#     -DMINGW_ROOT=$HOME/opt/llvm-mingw [-DMINGW_CLANG_SUFFIX=19]
#
# MINGW_ROOT is the unpacked llvm-mingw directory.  On Linux and macOS its
# own x86_64-w64-mingw32-clang is used.  Elsewhere (FreeBSD) the host clang
# is used with that sysroot: MINGW_CLANG_SUFFIX picks a versioned host LLVM
# (clang19, llvm-ar19...), and the compiler-rt builtins come from
# llvm-mingw, since host clang packages do not ship them for Windows.
# With wine installed, ctest runs the Windows tests through it.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MINGW_ROOT MINGW_CLANG_SUFFIX)

if(NOT MINGW_ROOT)
	set(MINGW_ROOT $ENV{MINGW_ROOT})
endif()
if(NOT IS_DIRECTORY "${MINGW_ROOT}/x86_64-w64-mingw32")
	message(FATAL_ERROR "set MINGW_ROOT to an unpacked llvm-mingw release")
endif()

set(triple x86_64-w64-mingw32)
if(CMAKE_HOST_SYSTEM_NAME MATCHES "Linux|Darwin")
	set(CMAKE_C_COMPILER ${MINGW_ROOT}/bin/${triple}-clang)
	set(CMAKE_CXX_COMPILER ${MINGW_ROOT}/bin/${triple}-clang++)
	set(CMAKE_RC_COMPILER ${MINGW_ROOT}/bin/${triple}-windres)
else()
	set(s "${MINGW_CLANG_SUFFIX}")
	set(CMAKE_C_COMPILER clang${s})
	set(CMAKE_CXX_COMPILER clang++${s})
	set(CMAKE_AR llvm-ar${s} CACHE FILEPATH "")
	set(CMAKE_RANLIB llvm-ranlib${s} CACHE FILEPATH "")
	set(CMAKE_RC_COMPILER llvm-windres${s})
	set(CMAKE_C_COMPILER_TARGET ${triple})
	set(CMAKE_CXX_COMPILER_TARGET ${triple})
	set(CMAKE_SYSROOT ${MINGW_ROOT})

	# A resource directory with the host clang's own headers and
	# llvm-mingw's Windows runtime libraries.
	execute_process(COMMAND ${CMAKE_C_COMPILER} -print-resource-dir
	    OUTPUT_VARIABLE host_res OUTPUT_STRIP_TRAILING_WHITESPACE)
	file(GLOB mingw_res ${MINGW_ROOT}/lib/clang/*)
	list(GET mingw_res 0 mingw_res)
	set(res ${CMAKE_BINARY_DIR}/mingw-resource)
	file(MAKE_DIRECTORY ${res}/lib)
	file(CREATE_LINK ${host_res}/include ${res}/include SYMBOLIC)
	file(CREATE_LINK ${mingw_res}/lib/windows ${res}/lib/windows SYMBOLIC)
	set(CMAKE_C_FLAGS_INIT "-resource-dir=${res}")
	set(CMAKE_CXX_FLAGS_INIT "-resource-dir=${res} -stdlib=libc++")
	set(CMAKE_EXE_LINKER_FLAGS_INIT
	    "-fuse-ld=lld -rtlib=compiler-rt -unwindlib=libunwind")
	set(CMAKE_RC_FLAGS_INIT "--target=pe-x86-64")
	string(APPEND CMAKE_RC_FLAGS_INIT
	    " -I${MINGW_ROOT}/${triple}/include")
endif()

# One self-contained .exe: no libc++, libunwind or winpthread DLLs to ship.
string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " -static")

set(CMAKE_FIND_ROOT_PATH ${MINGW_ROOT}/${triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Keep pkg-config away from the host libraries (a host raylib).
set(ENV{PKG_CONFIG_LIBDIR} ${MINGW_ROOT}/${triple}/lib/pkgconfig)
set(ENV{PKG_CONFIG_PATH} "")

find_program(WINE wine)
if(WINE)
	set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE})
endif()
