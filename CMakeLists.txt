# rtl_433 is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Radio; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.


########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 2.6)
# Fix behavior of CMAKE_C_STANDARD when targeting macOS.
if (POLICY CMP0025)
    cmake_policy(SET CMP0025 NEW)
endif ()

project(rtl433 C)

#select the release build type by default to get optimization flags
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)

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
    message(STATUS "Using Git version tag: ${GIT_VERSION} on ${GIT_BRANCH} at ${GIT_TIMESTAMP} (${GIT_REFSPEC} commit ${GIT_COMMIT})")
    ADD_DEFINITIONS(-DGIT_VERSION=${GIT_VERSION})
    ADD_DEFINITIONS(-DGIT_BRANCH=${GIT_BRANCH})
    ADD_DEFINITIONS(-DGIT_TIMESTAMP=${GIT_TIMESTAMP})
endif()

########################################################################
# Compiler specific setup
########################################################################
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 99)
if (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_C_COMPILER_ID}" MATCHES "Clang") AND NOT WIN32)
    ADD_DEFINITIONS(-Wall)
    ADD_DEFINITIONS(-Wextra)
    ADD_DEFINITIONS(-Wsign-compare)
    ADD_DEFINITIONS(-Wno-unused)
    ADD_DEFINITIONS(-Wno-unused-parameter)
    ADD_DEFINITIONS(-Wno-missing-field-initializers)
    ADD_DEFINITIONS(-std=c99)
    ADD_DEFINITIONS(-pedantic)
    # for strdup, setenv
    ADD_DEFINITIONS(-D_POSIX_C_SOURCE=200809)
    #http://gcc.gnu.org/wiki/Visibility
    add_definitions(-fvisibility=hidden)

    # CMake Release default for GCC/Clang is "-O3 -DNDEBUG"
    # set(CMAKE_C_FLAGS_RELEASE -O2)
    # CMake Debug default for GCC/Clang is "-g -DNDEBUG"
    # set(CMAKE_C_FLAGS_DEBUG -g3 -O0)
endif()
if ("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    # make sure we don't accidentally copy more than an int
    ADD_DEFINITIONS(-Wlarge-by-value-copy=8)
endif()

########################################################################
# Find build dependencies
########################################################################
find_package(PkgConfig)
find_package(LibRTLSDR)
if(LIBRTLSDR_FOUND)
    include_directories(${LIBRTLSDR_INCLUDE_DIRS})
    list(APPEND SDR_LIBRARIES ${LIBRTLSDR_LIBRARIES})
    ADD_DEFINITIONS(-DRTLSDR)
else()
    message(STATUS "RTL-SDR development files not found, RTL-SDR device input won't be possible.")
endif()

find_package(SoapySDR "0.6" NO_MODULE)
if(SoapySDR_FOUND)
    include_directories(${SoapySDR_INCLUDE_DIRS})
    list(APPEND SDR_LIBRARIES ${SoapySDR_LIBRARIES})
    ADD_DEFINITIONS(-DSOAPYSDR)
else()
    message(STATUS "Soapy SDR development files not found, Soapy SDR device input won't be possible.")
endif()

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

if(MINGW)
list(APPEND NET_LIBRARIES ws2_32 mswsock)
endif()

########################################################################
# Setup the include and linker paths
########################################################################
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

########################################################################
# Create uninstall target
########################################################################
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
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

    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    message("Doxygen build started")

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(doc_doxygen ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
    #    install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/doc/html DESTINATION share/doc)
endif()

########################################################################
# Add subdirectories
########################################################################
add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(tests)

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
