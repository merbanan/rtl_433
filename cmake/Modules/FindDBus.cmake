# - Try to find Dbus

find_package(PkgConfig)
pkg_check_modules(PC_DBus REQUIRED dbus-1)
set(DBus_DEFINITIONS ${PC_DBus_CFLAGS_OTHER})
message(STATUS "Include directories located at ${PC_DBus_INCLUDE_DIRS}")

find_library(DBus_LIBRARY NAMES dbus-1
        HINTS ${PC_DBus_LIBRARY_DIRS}
        )

set(DBus_VERSION ${PC_DBus_VERSION})

include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set DBus_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(DBus
        REQUIRED_VARS DBus_LIBRARY
        VERSION_VAR DBus_VERSION)

mark_as_advanced(DBus_LIBRARY DBus_INCLUDE_DIR DBus_VERSION)

set(DBus_LIBRARIES ${DBus_LIBRARY})
set(DBus_INCLUDE_DIRS ${PC_DBus_INCLUDE_DIRS})