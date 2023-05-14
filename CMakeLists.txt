########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 2.6)
# Fix behavior of CMAKE_C_STANDARD when targeting macOS.
if(POLICY CMP0025)
    cmake_policy(SET CMP0025 NEW)
endif()
# Only interpret if() arguments as variables or keywords when unquoted.
if(POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()

project(rtl433 C)

#select the release build type by default to get optimization flags
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "")

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

########################################################################
# Get version info from Git
########################################################################
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_COMMIT)
if(GIT_COMMIT) # is a git repo
    # shorten branch spec
    string(REGEX REPLACE ".*/" "" GIT_BRANCH "${GIT_REFSPEC}")
    # use lightweight (non-annotated) tags
    git_describe(GIT_VERSION "--tags")
    git_timestamp(GIT_TIMESTAMP)
    message(STATUS "Found Git version: ${GIT_REFSPEC} commit ${GIT_COMMIT} from ${GIT_TIMESTAMP_ISO}")
    message(STATUS "Using Git version tag: ${GIT_VERSION} on ${GIT_BRANCH} at ${GIT_TIMESTAMP}")
    ADD_DEFINITIONS(-DGIT_VERSION=${GIT_VERSION})
    ADD_DEFINITIONS(-DGIT_BRANCH=${GIT_BRANCH})
    ADD_DEFINITIONS(-DGIT_TIMESTAMP=${GIT_TIMESTAMP})
endif()

########################################################################
# Fallback to get release version info from Changelog
########################################################################
if(NOT GIT_COMMIT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md")
    # parse the CHANGELOG.md, this is clever, i.e. might go wrong ;)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md" CHANGELOG LIMIT_COUNT 42)
    # list(FILTER ...) needs CMake 3.6+
    foreach(item ${CHANGELOG})
        string(REGEX MATCH "^## Release (.+)" item ${item})
        if(CMAKE_MATCH_1)
            list(APPEND RELEASE_VERSIONS ${CMAKE_MATCH_1})
        endif()
    endforeach()
    if(RELEASE_VERSIONS)
        list(GET RELEASE_VERSIONS 0 RELEASE_VERSION)
        message(STATUS "Found Release version: ${RELEASE_VERSION}")
        ADD_DEFINITIONS(-DGIT_VERSION=${RELEASE_VERSION})
    endif()
endif()

########################################################################
# Compiler specific setup
########################################################################
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
if(("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang") AND NOT WIN32)
    ADD_DEFINITIONS(-Wall)
    ADD_DEFINITIONS(-Wextra)
    ADD_DEFINITIONS(-Wvla)
    ADD_DEFINITIONS(-Wsign-compare)
    ADD_DEFINITIONS(-std=c99)
    ADD_DEFINITIONS(-pedantic)
    ADD_DEFINITIONS(-Wshadow)
    ADD_DEFINITIONS(-Wmissing-prototypes)
    if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang" OR NOT "7.0.0" VERSION_GREATER CMAKE_C_COMPILER_VERSION)
        ADD_DEFINITIONS(-Wimplicit-fallthrough)
    endif()
    #ADD_DEFINITIONS(-Wfloat-equal)
    #ADD_DEFINITIONS(-Wbad-function-cast)
    #ADD_DEFINITIONS(-Wdocumentation)

    # for strdup, setenv, use either
    #ADD_DEFINITIONS(-D_POSIX_C_SOURCE=200809) # does not work with uClibc
    ADD_DEFINITIONS(-D_GNU_SOURCE)
    #http://gcc.gnu.org/wiki/Visibility
    add_definitions(-fvisibility=hidden)

    # CMake Release default for GCC/Clang is "-O3 -DNDEBUG"
    # set(CMAKE_C_FLAGS_RELEASE -O2)
    # CMake Debug default for GCC/Clang is "-g -DNDEBUG"
    # set(CMAKE_C_FLAGS_DEBUG -g3 -O0)
    # make use of ASAN
    set(CMAKE_C_FLAGS_DEBUG "-ggdb -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer")
endif()
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    # make sure we don't accidentally copy more than an int
    ADD_DEFINITIONS(-Wlarge-by-value-copy=8)
endif()

# Shut MSVC up about strdup and strtok
if(MSVC)
    ADD_DEFINITIONS(-D_CRT_NONSTDC_NO_DEPRECATE)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
    ADD_DEFINITIONS(-DNOMINMAX)
    # don't warn on type truncation
    add_compile_options("/wd4244")
    add_compile_options("/wd4267")
    add_compile_options("/wd4305")
endif()

# Fix printf %zu
if(MINGW)
    add_definitions(-D__USE_MINGW_ANSI_STDIO)
endif()

# Make sure we get M_PI
if(WIN32)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

########################################################################
# Use pkg-config
########################################################################
find_package(PkgConfig)

########################################################################
# Option to force ANSI-colored build output (for Ninja)
########################################################################
option(FORCE_COLORED_BUILD "Always produce ANSI-colored build output (GNU/Clang only)." FALSE)
if(FORCE_COLORED_BUILD)
    if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
       add_compile_options(-fdiagnostics-color=always)
    elseif("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
       add_compile_options(-fcolor-diagnostics)
    endif()
endif()

########################################################################
# Enable IPv6 support
########################################################################
option(ENABLE_IPV6 "Enable IPv6 support" TRUE)
if(ENABLE_IPV6)
    message(STATUS "IPv6 support enabled.")
    ADD_DEFINITIONS(-DMG_ENABLE_IPV6=1)
    if(MINGW)
        # IPv6 requires at least Vista for inet_pton, inet_ntop
        add_definitions(-D_WIN32_WINNT=0x0600)
    endif()
else()
    message(STATUS "IPv6 support disabled.")
endif()

########################################################################
# Find Threads support build dependencies
########################################################################
set(ENABLE_THREADS AUTO CACHE STRING "Enable Threads support")
set_property(CACHE ENABLE_THREADS PROPERTY STRINGS AUTO ON OFF)
if(ENABLE_THREADS) # AUTO / ON

find_package(Threads)
if(Threads_FOUND)
    message(STATUS "Threads support will be compiled.")
    ADD_DEFINITIONS(-DTHREADS)
elseif(ENABLE_THREADS STREQUAL "AUTO")
    message(STATUS "Threads support not found, some features will be disabled.")
else()
    message(FATAL_ERROR "Threads support not found.")
endif()

else()
    message(STATUS "Threads support disabled.")
endif()

########################################################################
# Find OpenSSL build dependencies
########################################################################
set(ENABLE_OPENSSL AUTO CACHE STRING "Enable OpenSSL TLS support")
set_property(CACHE ENABLE_OPENSSL PROPERTY STRINGS AUTO ON OFF)
if(ENABLE_OPENSSL) # AUTO / ON

find_package(OpenSSL)
# Get actual libs from pkg-config to support edge cases (static on Sparc)
# This will break Win32 builds, exclude that platform
pkg_check_modules(PC_OPENSSL QUIET openssl)
if(PC_OPENSSL_FOUND AND NOT WIN32)
    set(OPENSSL_LIBRARIES ${PC_OPENSSL_LINK_LIBRARIES})
endif()
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL TLS support will be compiled. Found version ${OPENSSL_VERSION}")
    include_directories(${OPENSSL_INCLUDE_DIR})
    list(APPEND SDR_LIBRARIES ${OPENSSL_LIBRARIES})
    ADD_DEFINITIONS(-DOPENSSL)
    ADD_DEFINITIONS(-DMG_ENABLE_SSL)
elseif(ENABLE_OPENSSL STREQUAL "AUTO")
    message(STATUS "OpenSSL development files not found, TLS won't be possible.")
else()
    message(FATAL_ERROR "OpenSSL development files not found.")
endif()

else()
    message(STATUS "OpenSSL TLS disabled.")
endif()

########################################################################
# Find LibRTLSDR build dependencies
########################################################################
set(ENABLE_RTLSDR ON CACHE STRING "Enable RTL-SDR (lbrtlsdr) driver support")
set_property(CACHE ENABLE_RTLSDR PROPERTY STRINGS AUTO ON OFF)
if(ENABLE_RTLSDR) # AUTO / ON

find_package(LibRTLSDR)
find_package(LibUSB)
if(LibRTLSDR_FOUND)
    message(STATUS "RTL-SDR device input will be compiled. Found version ${LibRTLSDR_VERSION}")
    include_directories(${LibRTLSDR_INCLUDE_DIRS})
    list(APPEND SDR_LIBRARIES ${LibRTLSDR_LIBRARIES})
    ADD_DEFINITIONS(-DRTLSDR)

if(LibUSB_FOUND)
    message(STATUS "libusb-1.0 error messages are available. Found version ${LibUSB_VERSION}")
    include_directories(${LibUSB_INCLUDE_DIRS})
    list(APPEND SDR_LIBRARIES ${LibUSB_LIBRARIES})
    ADD_DEFINITIONS(-DLIBUSB1)
else()
    message(STATUS "libusb-1.0 error messages are not available.")
endif()

elseif(ENABLE_RTLSDR STREQUAL "AUTO")
    message(STATUS "RTL-SDR development files not found, RTL-SDR device input won't be possible.")
else()
    message(FATAL_ERROR "RTL-SDR development files not found.")
endif()

else()
    message(STATUS "RTL-SDR device input disabled.")
endif()

########################################################################
# Find SoapySDR build dependencies
########################################################################
set(ENABLE_SOAPYSDR AUTO CACHE STRING "Enable SoapySDR driver support")
set_property(CACHE ENABLE_SOAPYSDR PROPERTY STRINGS AUTO ON OFF)
if(ENABLE_SOAPYSDR) # AUTO / ON

find_package(SoapySDR "0.6" NO_MODULE)
if(SoapySDR_FOUND)
    message(STATUS "SoapySDR device input will be compiled. Found version ${SoapySDR_VERSION}")
    include_directories(${SoapySDR_INCLUDE_DIRS})
    list(APPEND SDR_LIBRARIES ${SoapySDR_LIBRARIES})
    ADD_DEFINITIONS(-DSOAPYSDR)
elseif(ENABLE_SOAPYSDR STREQUAL "AUTO")
    message(STATUS "SoapySDR development files not found, SoapySDR device input won't be possible.")
else()
    message(FATAL_ERROR "SoapySDR development files not found.")
endif()

else()
    message(STATUS "SoapySDR device input disabled.")
endif()

########################################################################
# Setup optional Profiling with GPerfTools
########################################################################
# cmake -DCMAKE_BUILD_TYPE=Profile ..
# CPUPROFILE=prof.out ./src/rtl_433 ...
# pprof -text ./src/rtl_433 prof.out
if("${CMAKE_BUILD_TYPE}" STREQUAL "Profile")
    message(STATUS "Build type set to Profile. Linking GPerfTools.")
    find_package(Gperftools REQUIRED)
    include_directories(${GPERFTOOLS_INCLUDE_DIR})
    list(APPEND SDR_LIBRARIES ${GPERFTOOLS_LIBRARIES} -Wl,-no_pie)
    ADD_DEFINITIONS(-g)
    ADD_DEFINITIONS(-fno-builtin-malloc)
    ADD_DEFINITIONS(-fno-builtin-calloc)
    ADD_DEFINITIONS(-fno-builtin-realloc)
    ADD_DEFINITIONS(-fno-builtin-free)
endif()

########################################################################
# Setup the include and linker paths
########################################################################
if(MINGW OR MSVC)
list(APPEND NET_LIBRARIES ws2_32 mswsock netapi32)
endif()

include_directories(
    BEFORE
    ${PROJECT_SOURCE_DIR}/include
)

########################################################################
# Create uninstall target
########################################################################
configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
@ONLY)

add_custom_target(uninstall
    ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)

########################################################################
# Build documentation with Doxygen
########################################################################
option(BUILD_DOCUMENTATION "Create and install the HTML based API
documentation (requires Doxygen)" OFF)

find_package(Doxygen)
if(BUILD_DOCUMENTATION)
    if(NOT DOXYGEN_FOUND)
        message(FATAL_ERROR "Doxygen is needed to build the documentation.")
    endif()

    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    if(DOXYGEN_DOT_FOUND)
        set(HAVE_DOT "YES")
    else()
        set(HAVE_DOT "NO")
    endif()

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message(STATUS "Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
    #    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc/html DESTINATION share/doc)
endif()

########################################################################
# Build tests with analyzer
########################################################################
option(BUILD_TESTING_ANALYZER "Build the testing tree with static
analyzer (requires Clang)" OFF)

########################################################################
# Build tests
########################################################################
include(CTest) # note: this adds a BUILD_TESTING which defaults to ON

########################################################################
# Add subdirectories
########################################################################
add_subdirectory(include)
add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif(BUILD_TESTING)
add_subdirectory(conf)

# use space-separation format for the pc file
STRING(REPLACE ";" " " RTL433_PC_CFLAGS "${RTL433_PC_CFLAGS}")
STRING(REPLACE ";" " " RTL433_PC_LIBS "${RTL433_PC_LIBS}")

# unset these vars to avoid hard-coded paths to cross environment
IF(CMAKE_CROSSCOMPILING)
    UNSET(RTL433_PC_CFLAGS)
    UNSET(RTL433_PC_LIBS)
ENDIF(CMAKE_CROSSCOMPILING)

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix \${prefix})
set(libdir \${exec_prefix}/lib)
set(includedir \${prefix}/include)

INSTALL(
    FILES
    DESTINATION lib/pkgconfig
)

install(DIRECTORY man
    DESTINATION share
    PATTERN ".md" EXCLUDE)
