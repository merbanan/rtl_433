# - Try to find LibGPSD
# Once done this will define
#  LIBGPSD_FOUND - System has LibGPSD
#  LIBGPSD_INCLUDE_DIRS - The LibGPSD include directories
#  LIBGPSD_LIBRARIES - The libraries needed to use LibGPSD
#  LIBGPSD_DEFINITIONS - Compiler switches required for using LibGPSD

find_package(PkgConfig)
set(LIBGPSD_DEFINITIONS ${LIBGPSD_PKG_CFLAGS_OTHER})

find_path(LIBGPSD_INCLUDE_DIR NAMES gps.h
          HINTS ${LIBGPSD_PKG_INCLUDE_DIRS}
          PATHS
          /usr/include
          /usr/local/include )


find_library(LIBGPSD_LIBRARY NAMES gps
             HINTS ${LIBGPSD_PKG_LIBRARY_DIRS}
             PATHS
             /usr/lib
             /usr/local/lib
             /usr/lib${LIB_SUFFIX}
             /usr/local/lib${LIB_SUFFIX})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LIBGPSD_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LibGPSD  DEFAULT_MSG
                                  LIBGPSD_LIBRARY LIBGPSD_INCLUDE_DIR)

mark_as_advanced(LIBGPSD_LIBRARY LIBGPSD_INCLUDE_DIR)


set(LIBGPSD_LIBRARIES ${LIBGPSD_LIBRARY}  )
set(LIBGPSD_INCLUDE_DIRS ${LIBGPSD_INCLUDE_DIR} )


IF (LIBGPSD_FOUND)
    MESSAGE(STATUS "Found gpsd: ${LIBGPSD_LIBRARY}")
    MESSAGE(STATUS " include: ${LIBGPSD_INCLUDE_DIR}")
ELSE()
    MESSAGE(STATUS "Could not find gpsd library")
ENDIF()
