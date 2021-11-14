# - Try to find LibRTLSDR
# Once done this will define
#
#  LibRTLSDR_FOUND - System has librtlsdr
#  LibRTLSDR_INCLUDE_DIRS - The librtlsdr include directories
#  LibRTLSDR_LIBRARIES - The libraries needed to use librtlsdr
#  LibRTLSDR_DEFINITIONS - Compiler switches required for using librtlsdr
#  LibRTLSDR_VERSION - The librtlsdr version
#

find_package(PkgConfig)
pkg_check_modules(PC_LibRTLSDR QUIET librtlsdr)
set(LibRTLSDR_DEFINITIONS ${PC_LibRTLSDR_CFLAGS_OTHER})

find_path(LibRTLSDR_INCLUDE_DIR NAMES rtl-sdr.h
          HINTS ${PC_LibRTLSDR_INCLUDE_DIRS}
          PATHS
          /usr/include
          /usr/local/include )

find_library(LibRTLSDR_LIBRARY NAMES rtlsdr
             HINTS ${PC_LibRTLSDR_LIBRARY_DIRS}
             PATHS
             /usr/lib
             /usr/local/lib )

set(LibRTLSDR_VERSION ${PC_LibRTLSDR_VERSION})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LibRTLSDR_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibRTLSDR
                                  REQUIRED_VARS LibRTLSDR_LIBRARY LibRTLSDR_INCLUDE_DIR
                                  VERSION_VAR LibRTLSDR_VERSION)

mark_as_advanced(LibRTLSDR_LIBRARY LibRTLSDR_INCLUDE_DIR LibRTLSDR_VERSION)

set(LibRTLSDR_LIBRARIES ${LibRTLSDR_LIBRARY} )
set(LibRTLSDR_INCLUDE_DIRS ${LibRTLSDR_INCLUDE_DIR} )
