########################################################################
# Compile test cases
########################################################################
add_executable(data-test data-test.c ../src/output_file.c ../src/term_ctl.c)

target_link_libraries(data-test data)

add_test(data-test data-test)

add_executable(fft-test fft-test.c ../src/fft.c)
add_test(fft-test fft-test)

add_executable(baseband-test baseband-test.c ../src/baseband.c ../src/logger.c)

if(UNIX)
target_link_libraries(baseband-test m)
endif()

#add_test(baseband-test baseband-test)

########################################################################
# Define and build all unit tests
########################################################################
# target_compile_definitions was only added in CMake 2.8.11
add_definitions(-D_TEST)
foreach(testSrc bitbuffer.c fileformat.c optparse.c bit_util.c r_util.c abuf.c lora.c)
    get_filename_component(testName ${testSrc} NAME_WE)

    # Note that r_util.c needs compat_time.c shims
    add_executable(test_${testName} ../src/${testSrc} ../src/compat_time.c)

    add_test(${testName}_test test_${testName})
endforeach(testSrc)

# lora.c has a test main but uses the production FFT and CRC helpers.
target_link_libraries(test_lora r_433)

########################################################################
# Define integration tests
########################################################################
add_test(rtl_433_help ../src/rtl_433 -h)
add_test(lora_fft_demod_option ../src/rtl_433 -Y lora-fft -h)
add_test(flex_decoder_help ../src/rtl_433 -X)
add_test(flex_decoder bash -c
    "touch empty_file.cu8 && \
    ../src/rtl_433 \
        -X n=name,m=FSK_PCM,s=10,l=10,r=10240 \
        -r empty_file.cu8")
# Borrowed from: https://github.com/merbanan/rtl_433/pull/786#issuecomment-410513334
add_test(flex_decoder_w_getter bash -c
    "touch empty_file.cu8 && \
    ../src/rtl_433 \
        -X 'n=name,m=FSK_PCM,s=10,l=10,r=10240,get=@20:{12}0x7f:status:[10:pir 14:open 7:close 11:tamper 0xf:battery_low]' \
        -r empty_file.cu8")
# Borrowed from: https://github.com/merbanan/rtl_433/pull/1532#issue-728103584
add_test(flex_decoder_w_getter_and_format_specifier bash -c
    "touch empty_file.cu8 && \
    ../src/rtl_433 \
        -X 'n=name,m=FSK_PCM,s=10,l=10,r=10240,get=@2:{32}:code:%x' \
        -r empty_file.cu8")

# Black-box test of the HTTP/WS API server. Starts rtl_433 in manual device
# mode (no SDR), exercises each endpoint, and shuts it down cleanly. Skipped
# on Windows (POSIX shell script) and when python3/curl are unavailable.
if(UNIX)
    find_program(BASH_PROGRAM sh)
    find_program(CURL_PROGRAM curl)
    find_program(PYTHON3_PROGRAM python3)
    if(BASH_PROGRAM AND CURL_PROGRAM)
        add_test(
            NAME http_server_integration
            COMMAND ${BASH_PROGRAM}
                ${CMAKE_CURRENT_SOURCE_DIR}/http-integration-test.sh
                $<TARGET_FILE:rtl_433>)
        # Feeds a synthetic OOK signal over a fake rtl_tcp server so rtl_433 runs
        # its live poll loop, then reads the decode back over the WS API. Needs
        # python3 for the rtl_tcp server and ws probe.
        if(PYTHON3_PROGRAM)
            add_test(
                NAME http_server_rtltcp
                COMMAND ${BASH_PROGRAM}
                    ${CMAKE_CURRENT_SOURCE_DIR}/http-rtltcp-test.sh
                    $<TARGET_FILE:rtl_433>)
        else()
            message(STATUS "Skipping http_server_rtltcp test (need python3)")
        endif()
    else()
        message(STATUS "Skipping http_server_integration test (need sh and curl)")
    endif()
endif()

########################################################################
# Define style checks
########################################################################
add_executable(style-check style-check.c)
file(GLOB STYLE_CHECK_FILES  ../include/*.h ../src/*.c ../src/devices/*.c ../CMakeLists.txt ../*/CMakeLists.txt)
list(REMOVE_ITEM STYLE_CHECK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/jsmn.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/jsmn.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/mongoose.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/mongoose.c")
add_test(style-check style-check ${STYLE_CHECK_FILES})

########################################################################
# Define clang static analyzer checks
########################################################################
if(BUILD_TESTING_ANALYZER)
file(GLOB ANALYZER_CHECK_FILES  ../include/*.h ../src/*.c ../src/devices/*.c)
list(REMOVE_ITEM ANALYZER_CHECK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/jsmn.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/jsmn.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/mongoose.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/mongoose.c")
add_test(clang-analyzer
    ${CMAKE_CURRENT_SOURCE_DIR}/exitcode-for-output.sh
    clang
    -I${CMAKE_CURRENT_SOURCE_DIR}/../include
    --analyze
    -Xanalyzer
    -analyzer-output=text
    -Xanalyzer
    ${ANALYZER_CHECK_FILES})
endif()
