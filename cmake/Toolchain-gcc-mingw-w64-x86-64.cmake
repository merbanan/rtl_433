# Linux, Windows, or Darwin
SET(CMAKE_SYSTEM_NAME Windows)

# not really needed
SET(CMAKE_SYSTEM_VERSION 1)

# specify the base directory for the cross compiler
IF(DEFINED ENV{tools})
    SET(tools $ENV{tools})
ELSE()
    SET(tools /usr)
ENDIF()

# specify the cross compiler, choose 32/64
#SET(CMAKE_C_COMPILER ${tools}/bin/i686-w64-mingw32-gcc)
SET(CMAKE_C_COMPILER ${tools}/bin/x86_64-w64-mingw32-gcc)

# where is the target environment, choose 32/64
#SET(CMAKE_FIND_ROOT_PATH ${tools}/lib/gcc/i686-w64-mingw32/4.8)
SET(CMAKE_FIND_ROOT_PATH ${tools}/lib/gcc/x86_64-w64-mingw32/4.8)

# NOTE: use a sysroot with libusb and rtl-sdr if available
IF(DEFINED ENV{CMAKE_SYSROOT})
    SET(CMAKE_SYSROOT $ENV{CMAKE_SYSROOT})
    SET(CMAKE_STAGING_PREFIX $ENV{CMAKE_SYSROOT}/usr)
ENDIF()

# search for programs in the build host directories
SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# for libraries and headers in the target directories
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
