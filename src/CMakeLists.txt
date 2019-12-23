########################################################################
# Build libraries and executables
########################################################################
# commodity libraries
# consider -fvisibility=hidden
# Proper object library type was only introduced with CMake 2.8.8
add_library(r_lib STATIC
    abuf.c
    am_analyze.c
    baseband.c
    bitbuffer.c
    compat_paths.c
    compat_time.c
    confparse.c
    data.c
    decoder_util.c
    fileformat.c
    list.c
    mongoose.c
    optparse.c
    output_influx.c
    output_mqtt.c
    pulse_demod.c
    pulse_detect.c
    pulse_detect_fsk.c
    r_api.c
    r_util.c
    samp_grab.c
    sdr.c
    term_ctl.c
    util.c
)

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    set_source_files_properties(mongoose.c PROPERTIES COMPILE_FLAGS "-Wno-format")
endif()
if("${CMAKE_C_COMPILER_ID}" MATCHES "Clang")
    set_source_files_properties(mongoose.c PROPERTIES COMPILE_FLAGS "-Wno-format-pedantic -Wno-large-by-value-copy")
endif()

add_executable(rtl_433 rtl_433.c)
target_link_libraries(rtl_433 r_lib r_decoders)

if(MSVC)
    # needs CMake 3.1 but Windows builds should have that
    target_sources(rtl_433 PRIVATE getopt/getopt.c)
endif()

add_library(data data.c abuf.c term_ctl.c mongoose.c)
target_link_libraries(data ${NET_LIBRARIES})

target_link_libraries(rtl_433
    ${SDR_LIBRARIES}
    ${NET_LIBRARIES}
)

set(INSTALL_TARGETS rtl_433)
if(UNIX)
target_link_libraries(rtl_433 m)
endif()

# Explicitly say that we want C99
set_target_properties(rtl_433 r_lib PROPERTIES C_STANDARD 99)

########################################################################
# Device decoders subdirectory
########################################################################
add_subdirectory(devices)

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS ${INSTALL_TARGETS}
    RUNTIME DESTINATION bin              # .dll file
)
