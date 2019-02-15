########################################################################
# Compile test cases
########################################################################
add_executable(data-test data-test.c)

target_link_libraries(data-test data)

add_test(data-test data-test)

add_executable(baseband-test baseband-test.c ../src/baseband.c)

if(UNIX)
target_link_libraries(baseband-test m)
endif()

#add_test(baseband-test baseband-test)

########################################################################
# Define and build all unit tests
########################################################################
# target_compile_definitions was only added in CMake 2.8.11
add_definitions(-D_TEST)
foreach(testSrc bitbuffer.c fileformat.c optparse.c util.c)
    get_filename_component(testName ${testSrc} NAME_WE)

    add_executable(test_${testName} ../src/${testSrc})

    add_test(${testName}_test test_${testName})
endforeach(testSrc)

########################################################################
# Define integration tests
########################################################################
add_test(rtl_433_help ../src/rtl_433 -h)

########################################################################
# Define style checks
########################################################################
add_executable(style-check style-check.c)
file(GLOB STYLE_CHECK_FILES  ../include/*.h ../src/*.c ../src/devices/*.c)
list(REMOVE_ITEM STYLE_CHECK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/mongoose.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/mongoose.c")
add_test(style-check style-check ${STYLE_CHECK_FILES})
