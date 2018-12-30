# - Try to find LibUSB
# Once done this will define
#  LIBUSB_FOUND - System has LibUSB
#  LIBUSB_INCLUDE_DIRS - The LibUSB include directories
#  LIBUSB_LIBRARIES - The libraries needed to use LibUSB
#  LIBUSB_DEFINITIONS - Compiler switches required for using LibUSB

find_package(PkgConfig)
pkg_check_modules(LIBUSB_PKG QUIET libusb-1.0)
set(LIBUSB_DEFINITIONS ${LIBUSB_PKG_CFLAGS_OTHER})

find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h
          HINTS ${LIBUSB_PKG_INCLUDE_DIRS}
          PATHS
          /usr/include/libusb-1.0
          /usr/include
          /usr/local/include )

#standard library name for libusb-1.0
set(libusb1_library_names usb-1.0)

#libusb-1.0 compatible library on freebsd
if((CMAKE_SYSTEM_NAME STREQUAL "FreeBSD") OR (CMAKE_SYSTEM_NAME STREQUAL "kFreeBSD"))
    list(APPEND libusb1_library_names usb)
endif()

find_library(LIBUSB_LIBRARY
             NAMES ${libusb1_library_names}
             HINTS ${LIBUSB_PKG_LIBRARY_DIRS}
             PATHS
             /usr/lib
             /usr/local/lib )

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBUSB_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibUSB  DEFAULT_MSG
                                  LIBUSB_LIBRARY LIBUSB_INCLUDE_DIR)

mark_as_advanced(LIBUSB_LIBRARY LIBUSB_INCLUDE_DIR)

set(LIBUSB_LIBRARIES ${LIBUSB_LIBRARY} )
set(LIBUSB_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIR} )
