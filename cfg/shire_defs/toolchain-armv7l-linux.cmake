# Cross-compilation toolchain for Zybo7020 (armv7l) using the
# Debian cross-compiler packages (arm-linux-gnueabihf).

# Basic cross system configuration
SET(CMAKE_SYSTEM_NAME           Linux)
SET(CMAKE_SYSTEM_VERSION        1)
SET(CMAKE_SYSTEM_PROCESSOR      arm)

# Allow overrides via environment or cache variables
if(DEFINED ENV{CROSS_COMPILE})
	set(CROSS_COMPILE_PREFIX "$ENV{CROSS_COMPILE}")
else()
	set(CROSS_COMPILE_PREFIX "arm-linux-gnueabihf-")
endif()

# If a full path is desired, CMAKE_C_COMPILER and CMAKE_CXX_COMPILER can be
# set externally (for example: -DCMAKE_C_COMPILER=/usr/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_C_COMPILER "/usr/bin/${CROSS_COMPILE_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/${CROSS_COMPILE_PREFIX}g++")
if(NOT DEFINED CMAKE_SYSROOT)
	set(CMAKE_SYSROOT "/usr/arm-linux-gnueabihf")
endif()

# Make sure CMake treats the sysroot as a cache variable so it's visible to
# try-compile and other internal checks when this toolchain file is loaded.
set(CMAKE_SYSROOT "${CMAKE_SYSROOT}" CACHE PATH "Sysroot for cross compilation" FORCE)

# Ensure CMake knows about the Debian multiarch locations.
set(CMAKE_FIND_ROOT_PATH "/usr/arm-linux-gnueabihf;/usr/lib/arm-linux-gnueabihf;/usr/include/arm-linux-gnueabihf;/usr/include")

# Also append these to CMake's canonical include/library search variables so
# projects that don't consult the find root path still find the multiarch files.
list(APPEND CMAKE_LIBRARY_PATH "/usr/lib/arm-linux-gnueabihf" "${CMAKE_SYSROOT}/lib")
list(APPEND CMAKE_INCLUDE_PATH "/usr/include/arm-linux-gnueabihf" "/usr/include")

# Configure the find commands: programs from host, libraries/includes from
# the target sysroot.
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM   NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY   ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE   ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE   ONLY)

# Ensure the cross-linker is passed the sysroot when linking executables/libraries
set(CMAKE_EXE_LINKER_FLAGS "--sysroot=${CMAKE_SYSROOT}")
set(CMAKE_SHARED_LINKER_FLAGS "--sysroot=${CMAKE_SYSROOT}")
set(CMAKE_MODULE_LINKER_FLAGS "--sysroot=${CMAKE_SYSROOT}")

# Avoid running cross-compiled binaries during configure (use static try-compile)
if(NOT DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
	set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "Try compile target type" FORCE)
endif()

# Make pkg-config report paths relative to the sysroot and prefer target pkg-config
# Ensure the multiarch pkgconfig directory is present so .pc files for armhf are found.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_PATH} "/usr/lib/arm-linux-gnueabihf/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")

# Ensure compiler picks up multiarch include paths early so unqualified includes
# like <gpg-error.h> resolve to the target headers (e.g. /usr/include/arm-linux-gnueabihf).
# Add both the multiarch include and any sysroot include directory.
## Use -isystem for system headers so compiler treats them as system includes.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT} -isystem /usr/include/arm-linux-gnueabihf -isystem /usr/include")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --sysroot=${CMAKE_SYSROOT} -isystem /usr/include/arm-linux-gnueabihf -isystem /usr/include")

# Ensure pkg-config can find target .pc files (including multiarch locations)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_PATH} "/usr/lib/arm-linux-gnueabihf/pkgconfig:${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig:${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf/pkgconfig")

# Also set CMAKE_SYSTEM_INCLUDE_PATH so CMake's include search honors the sysroot
list(APPEND CMAKE_SYSTEM_INCLUDE_PATH "/usr/include/arm-linux-gnueabihf" "/usr/include")

# These variable settings are specific to cFE/OSAL and determine which
# abstraction layers are built when using this toolchain. Adjust PSP/BSP
# names to match your platform if you have a different naming convention.
SET(CFE_SYSTEM_PSPNAME      "pc-linux")
SET(OSAL_SYSTEM_OSTYPE      "posix")

# Helpful message when this toolchain file is loaded
message(STATUS "Using armv7l toolchain: ${CMAKE_C_COMPILER} (sysroot=${CMAKE_SYSROOT})")
